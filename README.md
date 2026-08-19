# flight-fault-injection

A flight control loop under deliberate attack. A fixed-rate estimator and
controller run as a real-time process against a simulated vehicle, and a fault
injector sits on the wire between them corrupting sensors, dropping packets,
skewing clocks, and starving the loop. The result is a measured answer to two
questions: how tight is the loop's timing, and how fast does it notice and
recover when something breaks.

Companion project: [epuck-edge-ai](https://github.com/sukethrp/epuck-edge-ai),
which measures on-device inference latency. This one asks what happens when that
inference arrives late, wrong, or not at all.

**Status:** step 2 of 7, IMU staleness accounting and in-run rx_ns
bracketing. Empty cells below mean not measured yet, never estimated.

## Result

Wake-up error in microseconds, 250 Hz loop. Identical application code in
every row; the kernel and its scheduling policy are the only variable. Empty
cells are not measured yet. macOS rows are six alternating 600-second runs
pooled (`results/hour_ts_1.csv`–`hour_ts_6.csv`,
`results/hour_rt_1.csv`–`hour_rt_6.csv`; percentiles in `results/pooled.md`).
900,000 samples per configuration. Overruns are the sum of the `overruns=`
header fields on those twelve files.

| Configuration | Samples | p50 | p99 | p99.9 | p99.99 | max | overruns |
|---|---|---|---|---|---|---|---|
| macOS, best effort | 900000 | 707 | 880 | 2028 | 8522 | 39127 | 613 |
| macOS, THREAD_TIME_CONSTRAINT_POLICY | 900000 | 10 | 24 | 42 | 95 | 225 | 0 |
| Linux, CFS | | | | | | | |
| Linux, PREEMPT_RT, SCHED_FIFO, isolated core | | | | | | | |

`cyclictest` on the same machine gives the platform floor, plotted as a
reference line alongside these curves.

![Wake-error histogram](results/jitter.png)

Log x and log y. First bin starts at 1 µs; smaller samples sit there.

Socket drain, same RT policy, 600 s (`results/p2.md`). 400 Hz × 76-byte
loopback UDP against a dummy sender; PX4 is still a later drop-in.
`rx_total=240000` (1.6 per tick), `drain_full=36`.

| Configuration | Samples | p50 | p99 | p99.9 | p99.99 | max | rx_total | drain_full |
|---|---|---|---|---|---|---|---|---|
| macOS RT, no socket | 150000 | 8 | 22 | 40 | 77 | 343 | 0 | 0 |
| macOS RT, UDP drain | 150000 | 7 | 20 | 34 | 79 | 536 | 240000 | 36 |

The Phase 2 pair is internally comparable; cross-phase max comparisons are not.
Phase 1 RT max 225 µs and Phase 2 no-socket max 343 µs differ by machine state
between days, not by the socket.

Darwin timeshare leeway is about 0.18 of the requested interval
(`results/rates.md`). The RT floor is absolute, 8–12 µs across 100–1000 Hz
(`results/rates_rt.md`), so the relative win shrinks at higher rates. 250 Hz
was a conservative choice for the histogram, not a lucky one.

| Hz | period µs | timeshare p50 | RT p50 | RT / period |
|---|---|---|---|---|
| 100 | 10000 | 1883 | 12 | 0.12% |
| 250 | 4000 | 767 | 10 | 0.25% |
| 500 | 2000 | 367 | 8 | 0.40% |
| 1000 | 1000 | 166 | 8 | 0.80% |

Darwin computation/constraint now scale as 3/16 and 1/2 of the period
(`results/rates_rt2.md`), so the 250 Hz claim stays 750 µs / 2 ms and
1 kHz is 188 µs / 500 µs. Same 60 s, load 200 µs, `--rt`: wake p50 is
13 / 10 / 10 / 8 µs at 100 / 250 / 500 / 1000 Hz. At 1 kHz, exec p99
200 µs is 107% of the 188 µs claim; `scheduler_applied` stays 1.

MAVLink v2 HIGHRES_IMU, 75 bytes on the wire, 400 Hz. Same RT policy, 600 s
(`results/p2_mavlink.md`). Compared to the 2a dummy UDP drain
(`results/p2_socket.csv.gz`). `parse_ok=240000` matches `rx_total`.
`seq_gaps=0`. exec p50 511.3 → 515.4 µs.

| Configuration | Samples | p50 | p99 | p99.9 | p99.99 | max | parse_ok | seq_gaps |
|---|---|---|---|---|---|---|---|---|
| macOS RT, UDP drain (2a) | 150000 | 7 | 20 | 34 | 79 | 536 | | |
| macOS RT, HIGHRES_IMU parse | 150000 | 10 | 23 | 40 | 74 | 477 | 240000 | 0 |

A same-sender parse on/off pair (`results/p2b.md`) moved exec p50 the
other way by 4 µs, so the +4.1 here is scatter, not a parse tax. Cross-run
exec p50 is not a stage cost. Parse cost is unresolved; measure it with
the in-run `rx_ns` column instead.

Latest-value IMU slot plus `skew_ns`, same RT policy, 600 s
(`results/p2_slots.md`). `parse_ok=rx_total=240000`, `seq_gaps=0` with
the uint8 wrap mask, `drain_full=15`. exec p50 515.4 → 515.0 µs.

| Configuration | Samples | p50 | p99 | p99.9 | p99.99 | max | parse_ok | seq_gaps |
|---|---|---|---|---|---|---|---|---|
| macOS RT, HIGHRES_IMU parse (2b) | 150000 | 10 | 23 | 40 | 74 | 477 | 240000 | 0 |
| macOS RT, latest-value slots (2c) | 150000 | 10 | 22 | 38 | 82 | 171 | 240000 | 0 |

`skew_ns` (IMU ticks only, n=149804): min -18872666, max -9166. First vs
last 10% of the run differs by -21 µs over ~480 s. The detector uses that
slope; the fixture offset is transport plus wait-until-next-tick, not a
second clock.

`drain_full` alone is not a freshness signal (`results/p2_slots.csv.gz`).
Bound hit with skew improving or flat is sender coalescing. Bound hit
with skew walking one loop period per tick is backlog; the stale leftover
shows on the tick after the streak.

Age is sampled at tick boundaries. The IMU stale limit is 7.5 ms
(`expected_period * staleness_limit_periods`); the theoretical detection
floor is 8 ms (`staleness_floor_ns` in the CSV header). The fault table
Floor column is that number, once a fault is injected.

## Fault table

Time-to-detect is measured from the injector stamping an event to a detector
firing, both from the same monotonic clock. The injector runs on the real-time
host beside the flight software for exactly this reason: across two machines the
interval would be contaminated by clock offset. Floor is
`ceil(limit / loop_period) * loop_period` from the run header, not a
measured TTD.

| Fault | Injection | Detector | TTD p50 | TTD p95 | TTD max | Floor | TTR | End state |
|---|---|---|---|---|---|---|---|---|

## Layout

Target structure. Files appear as their step lands.

```
src/
  rt_platform.h/.cpp   monotonic clock + RT scheduling, Linux and Darwin
  ring_log.h           preallocated sample sink, no I/O in the loop
  msg_slots.h          latest-value MAVLink payloads, named per msgid (step 2)
  udp_rx.h/.cpp        non-blocking loopback bind and recv (step 2)
  loop_bench.cpp       the fixed-rate loop and its own instrumentation
  ekf.h/.cpp           6-state filter with NIS gating (step 4)
  detectors.h/.cpp     staleness, sequence, skew, divergence (step 6)
  failsafe.h/.cpp      fallback state machine (step 6)
tools/udp_sender.cpp   HIGHRES_IMU v2 UDP source until PX4 SITL (step 2)
third_party/mavlink    vendored MAVLink v2 headers (common dialect)
injector/              MAVLink proxy, runs on the RT host (step 5)
analysis/              percentile tables and figures
scripts/pre-commit     hot path and authorship enforcement
docs/DESIGN.md         sequencing, including why the socket is measured before PX4
results/               gzipped CSVs and figures
```

## Running it

```bash
cmake -B build && cmake --build build

# smoke test, unprivileged, either platform
./build/loop_bench --hz 250 --seconds 60 --label smoke --out results/smoke.csv

# a real run. one hour at 250 Hz is 900k samples, which is what p99.99 needs.
sudo ./build/loop_bench --hz 250 --seconds 3600 --core 3 --prio 80 \
     --load-us 800 --label "linux rt" --out results/loop_linux_rt_250hz.csv

# platform floor, Linux only
sudo cyclictest -m -a 3 -t 1 -p 80 -i 4000 -h 400 -q -D 1h > results/cyclictest_rt.txt
```

`loop_bench` never aborts when it cannot get the scheduling policy it asked for.
It records what actually stuck in the CSV header, and the analysis warns on any
curve where real-time scheduling was requested and denied, so a figure cannot
silently claim a configuration it did not have.

## Kernel setup, Linux RT host

```
isolcpus=3 nohz_full=3 rcu_nocbs=3 intel_idle.max_cstate=1 idle=poll
cpupower frequency-set -g performance
```

Isolating the core is what separates this from a loop that merely asks nicely.
`nohz_full` stops the timer tick there, `rcu_nocbs` moves RCU callbacks off it,
and the C-state and governor settings stop the CPU sleeping or downclocking
between periods, which produces tens-of-microseconds outliers that look exactly
like scheduler jitter and are not.

## Limitations

- Everything is in simulation. The point is the timing and fault behaviour of
  the software, and those numbers do not depend on the vehicle being real.
- No hardware in the loop and no microcontroller port. The inner loop is written
  to be portable to one, which is a later upgrade, not a prerequisite.
- macOS cannot pin a thread to a core, and `mlockall` typically fails there
  without root. The macOS curve is a best-effort baseline, labelled as such.
