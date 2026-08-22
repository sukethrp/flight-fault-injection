# Design

The repo exists to produce two artifacts: a loop jitter histogram out to
p99.99, and a fault table with time-to-detect and time-to-recover. This
file is the sequencing of work that produces them. The host is this Mac.
There is no PREEMPT_RT box and no second machine.

## Phase 1 — instrument

Fixed-rate loop, per-iteration wake error, monotonic clock, absolute
deadlines, RT policy with honest status. Measured. Numbers in
`results/pooled.md`. The scheduling variable is Darwin
`THREAD_TIME_CONSTRAINT_POLICY` versus timeshare, not a three-kernel
comparison.

## Phase 2 — plant I/O

The intended plant is PX4 SITL speaking MAVLink over UDP. The measurement
that matters first is what non-blocking UDP I/O does to the histogram.
PX4 on Apple Silicon is a toolchain unknown; this machine already hit
that wall in epuck-edge-ai. Standing SITL up before the drain would put
that unknown in front of the number.

So the socket is characterized against a dummy sender. Any UDP source
answers 2a. PX4 replacing that sender is timeboxed, not a gate: if SITL
does not build and talk in the window, `tools/plant.cpp` is the
documented fallback: a 1 kHz RK4 double integrator that emits the same
three MAVLink types on loopback. The drain does not change in either
case.

| | | owner |
|---|---|---|
| 2a | UDP sender fixture + bounded non-blocking drain, jitter re-measured | you (drain) |
| 2b | MAVLink v2 headers, real frame parse; per-frame cost unresolved at cross-run resolution | split |
| 2c | latest-value slots, dual timestamps, sequence gaps | you |
| 2d | staleness accounting per message class | split |
| 2e | PX4 SITL timeboxed; self-written plant is the documented fallback | you |
| 2f | final campaign with whichever plant 2e produced | you |

Socket was the first measured delta. Parse cost is unresolved at
cross-run resolution; the in-run `rx_ns` column is the instrument.

`rx_ns` is drain+parse+slot as one receive-path column, not a parse-only
bracket misnamed. Splitting those three would add clock reads without a
separate budget. That name stays; Phase 4 adds `ekf_ns` and `ctrl_ns`
beside it.

The drain is bounded. An unbounded `recv` loop turns a traffic burst
into a deadline miss. Latest-value slots in 2c, not a queue: a queue
turns a timing problem into a memory problem and then into an allocation
in the hot path. Slots are named members (IMU, position, GPS), not a
msgid table.

MAVLink `seq` is per-sending-component, not per-message-type. A shared
`last_seq` on `MsgSlots` is the tracker; per-slot sequence would count
IMU→POS→GPS as drops every cycle.

2d records `age_*_ns` at the tick boundary per slot and a theoretical
detection floor `ceil(limit / loop_period) * loop_period` per type in
the CSV header, next to `staleness_limit_periods`. At 250 Hz with limit
3: IMU 8 ms (limit 7.5 ms), position 60 ms, GPS 600 ms. Measured TTD
belongs next to that floor, not presented bare.

## Phase 4 — estimator (author-owned update)

Scaffold: 6-state NED position+velocity, fixed-size float matrices,
`--ekf` wiring, `ekf_ns`, CSV state/NIS/rejects. The author writes
`predict`, `correct`, the NIS gate, and the Joseph-form covariance
update in `src/ekf.cpp`. The gate is both robustness and a detector;
reject count is mirrored into the detector bitfield, not recomputed.

χ² critical values, 3 dof: 7.815 (95%), 11.345 (99%), 16.266 (99.9%).
Joseph form \(P=(I-KH)P(I-KH)^\top+KRK^\top\) — the short \((I-KH)P\)
loses symmetry and positive-definiteness on long soaks. Defense crib:
`NOTES.md` 2026-08-22.

## Phase 5 — kernel loss vs proxy

Measured on this machine: a `dnctl` pipe with `plr 0.15` on port 14555
dropped 15.07% of loopback UDP, 23186 received
(`results/dummynet_loss.csv.gz`, `rx_total=23186`) against 27300 sent
(`results/dummynet_clean.csv.gz`, `rx_total=27300`). MAVLink sequence
gaps counted 4113 of the 4114 accounting drops; the missing one is a
drop after the last received frame, with no successor to reveal it.

dummynet owns uniform random loss and delay from the kernel. The Phase 5
proxy owns burst loss, type-selective drops, payload corruption, stuck
sensors, and timestamp skew. The pipe has one bucket and no flow
separation, so HIGHRES_IMU, LOCAL_POSITION_NED, and GPS_RAW_INT take the
same Bernoulli rate.

Teardown is pf first, then dnctl. `dnctl -q flush` while the pf rule
still references the pipe blackholes the port rather than restoring it,
producing `rx_total=0` that reads as 100% loss.

Payload corruptors (bit flip, bias, stuck, GPS jump, clock skew) stay
`// AUTHOR: implement` in `injector/fault.cpp`. How you corrupt
determines what the detector can see; that mapping is the interview
question, not the proxy's poll loop.

## Phase 6 — detectors and failsafe

Detectors are agent-owned and fully implemented (staleness, seq gap,
skew slope, deadline miss, stuck variance, `trace(P)` divergence; NIS
reject count mirrored from the EKF). Failsafe scaffold:
`NOMINAL/DEGRADED/SAFE/LOCKED`, confirmation counters, asymmetric hold
timers, transition event log. Transition predicates and hysteresis
policy in `src/failsafe.cpp` are author-owned: a detector that chatters
is worse than none.

## Phase 7 — campaign on one host

The fault table's time-to-detect is injector-stamp to detector-fire.
Those two timestamps have to be the same monotonic epoch. Split across
two machines, every TTD sample includes clock offset, and that offset is
not smaller than the IMU floor. The injector therefore has to share a
clock domain with the flight software.

That constraint still holds. It is now satisfied without a sync
protocol: sender, injector, and `loop_bench` are one process tree on
one Mac and all call `rt::now_ns()` (`CLOCK_UPTIME_RAW` on Darwin). No
second host, no shared `CLOCK_MONOTONIC` across NICs, no isolated core.

The campaign is this machine, loopback, Darwin timeshare versus
`THREAD_TIME_CONSTRAINT_POLICY`. PREEMPT_RT, `SCHED_FIFO`, `isolcpus`,
and `cyclictest` platform-floor numbers are out of scope; there is no
Linux host.
