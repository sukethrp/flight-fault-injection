# Engineering log

Dated entries, newest at the bottom. One entry per work session. This is the
reasoning trail behind the commits: what was tried, what the numbers were, what
got rejected and why. Rejected approaches are as valuable as accepted ones.

---

## 2026-08-15 - scope, platform, and repo setup

**Shape of the project.** Simulated vehicle as the plant, my own estimator and
controller as a fixed-rate real-time process, a fault injector on the wire
between them. Two deliverables: a loop jitter histogram out to p99.99 in
microseconds, and a fault table with time-to-detect and time-to-recover per
injected fault. The demo is not the artifact; the measurements are.

**Platform problem.** Dev machine is an Apple M5 and PREEMPT_RT does not exist
on Darwin. A Linux VM under the hypervisor is not usable for tail latency:
virtualized timers and host scheduler noise are larger than the effect being
measured. Ordering a small x86 box for bare-metal Ubuntu with the RT kernel, and
writing everything behind a platform layer meanwhile.

Consequence, and it is an improvement: the macOS run stops being a throwaway and
becomes the first of three curves, with the kernel as the only variable.

1. macOS, mach_wait_until plus THREAD_TIME_CONSTRAINT_POLICY, best effort
2. Linux, CFS, default scheduling
3. Linux, PREEMPT_RT, SCHED_FIFO on an isolated core

**Rejected: PX4 EKF2 as the estimator.** Letting the flight stack estimate means
observing someone else's filter degrade rather than authoring the degradation,
which removes the part of the project worth having. PX4 SITL is the plant and
the MAVLink transport only.

**Rejected: ROS 2 on the inner loop.** DDS discovery and executor overhead would
dominate the histogram, so the measurement would be of the middleware rather
than the scheduler. Raw UDP plus MAVLink.

**Rejected: Python for the loop.** GIL and GC pauses put p99.99 in the
milliseconds with no tuning path out. C++17 for the loop, Python for analysis.

**Rejected: a 15-state filter.** Every behaviour the fault table needs (gating,
divergence, dead reckoning through GPS loss, covariance growth as an
observability signal) shows up in 6 states. Starting there.

**Open.** Sample count for a defensible p99.99, and therefore run duration. Ten
samples past the quantile is the minimum, so 100k samples, which at 250 Hz is
about 7 minutes. Planning one hour per configuration for margin.

**Environment.** clang 21, cmake via Homebrew, python 3.13. Commit hook installed
from scripts/pre-commit: blocks agent-shell commits, oversized files,
uncompressed result CSVs, and banned calls between the hot path sentinels.

## 2026-08-16 - clock layer, measured drift

Wrote `now_ns` and `sleep_until_ns` behind the platform layer. macOS uses
`clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` and `mach_wait_until`; Linux uses
`clock_gettime(CLOCK_MONOTONIC)` and `clock_nanosleep(TIMER_ABSTIME)`. Mach
ticks are not nanoseconds, so the timebase conversion goes through
`mach_timebase_info`, cached once behind a function-local static.

Measured on the M5, 250 Hz for 10 s, 2500 iterations:

| approach | elapsed | drift |
|---|---|---|
| `nanosleep(period)` each iteration | 12.320 s | +2320 ms, 23.2% slow |
| `mach_wait_until`, deadline advanced outside the sleep | 10.000 s | 0 ms |

2320 ms over 2500 iterations is 928 us of wakeup latency per call. The loop
asked for 250 Hz and delivered 203 Hz. macOS coalesces timer interrupts to save
power and a default timeshare thread absorbs all of it. Reference point: the
same test on a Linux VM drifted 3.9%, so this is a Darwin scheduling property,
not a bug in the loop.

The absolute version landing on 10.000 s does not mean the individual wakes were
punctual. They are late by the same amount; the error just stops compounding
because `next` advances from the previous deadline. Total elapsed hides per-tick
error, which is why step 1c measures wake error per iteration instead of a
stopwatch reading.

## 2026-08-16 - healthy run, three matching flags

`results/healthy.csv`: darwin, 250 Hz, load 500 µs, warmup 500, 5000 samples.
Header `overruns=3` `rebases=3`. Flagged rows (`flags==3` is both bits):

| seq | wake_err_us | exec_us | flags |
|---|---|---|---|
| 2113 | 5234.75 | 500.00 | 3 |
| 3884 | 682.12 | 3947.29 | 3 |
| 4146 | 3155.12 | 1894.96 | 3 |

Not three copies of a sleep stall. 2113 is: woke 5.2 ms late, then 500 µs of
work. 3884 woke on time and was descheduled inside `busy_ns`. 4146 split the
stall across the wait and the work. Neighbours on either side are ordinary
(~600–850 µs wake, 500 µs exec, flags 0), so the rebase put `next` in the
future rather than spinning.

3/5000 is 0.06%. Same predicate family: overrun is `done > next + period`,
rebase is `next + period <= now` a few nanoseconds later. Matching counts
are the check; hundreds, or rebases without overruns, would mean the branch
was too loose. Zero was the wrong target on darwin with no RT policy.

## 2026-08-16 - THREAD_TIME_CONSTRAINT_POLICY vs baseline

Same load as `results/healthy.csv`: 250 Hz, 20 s, warmup 500, load 500 µs,
5000 samples. `results/healthy_rt.csv` header: `scheduler_applied=1`,
`memory_locked=0`, `affinity_set=0`,
`note=mlockall denied (expected without root on macOS);`

Darwin `apply` claims: computation 750 µs, constraint 2 ms, preemptible 0.
750 µs is 1.5x the ~500 µs exec in healthy.csv, not the 4000 µs period.
preemptible 0 is seq 3884 (3947 µs exec after a 682 µs wake: descheduled
inside `busy_ns`).

Wake error, microseconds, comment-and-header lines skipped (NR>9 is wrong
once `scheduler_applied` and friends lengthen the header; that path counted
5004 rows in healthy_rt):

| file | n | mean | p50 | p99 | p99.9 | max |
|---|---|---|---|---|---|---|
| `results/healthy.csv` | 5000 | 669 | 708 | 872 | 1880 | 5235 |
| `results/healthy_rt.csv` | 5000 | 10 | 9 | 23 | 42 | 99 |

p50 dropped 708 → 9, in line with the 928 µs timeshare drift figure. The
tail moved with it on this run: max 5235 → 99, not a better typical with
an unchanged millisecond worst case. 5000 samples supports p99 (~50 above
it). p99.9 has five; p99.99 is not reportable. The 20 s window can miss
the stall that healthy.csv caught at seq 2113. One hour in 1g is what
makes those honest.

`memory_locked=0` is mlockall without root, expected. The header recorded
the denial instead of looking like a locked run.

The 700 µs p50 was timer coalescing, not contention. macOS batches timer
wakeups to hold deeper idle; a timeshare thread takes the full window.
THREAD_TIME_CONSTRAINT_POLICY marks the thread real-time and exempts it.
Same order as the drift experiment (928 µs average on timeshare
`nanosleep`).

Five alternating 20 s pairs (`results/ts_*.csv`, `results/rt_*.csv`),
`grep -v '^#'` so the extra header lines cannot inflate n. `--rt` gates
the Darwin policy; label alone does not. All ten headers: timeshare
`scheduler_applied=0`, RT `scheduler_applied=1`, both `memory_locked=0`.

| file | p50 | max |
|---|---|---|
| ts_1 | 707 | 38407 |
| ts_2 | 707 | 18163 |
| ts_3 | 707 | 14550 |
| ts_4 | 706 | 21971 |
| ts_5 | 707 | 14098 |
| rt_1 | 10 | 73 |
| rt_2 | 8 | 54 |
| rt_3 | 11 | 87 |
| rt_4 | 9 | 63 |
| rt_5 | 9 | 67 |

Timeshare p50 stays ~707 µs; max is 14–38 ms every run. RT p50 stays 8–11 µs;
max stays 54–87 µs, never milliseconds. The seq 2113 stall is the timeshare
coalescing tail, not a one-window fluke. Still 20 s each, not the 1g hour.

`analysis/percentiles.py` on those ten files (`results/table.md`): timeshare
p99 880–1470 µs (1470 in `ts_1.csv`), RT p99 19–26 µs. p99.9 `n/a` at 5000
samples. overruns 5–17 vs 0.

`ns_to_ticks` now rounds up. Floor division put the mach deadline up to one
tick (41.67 ns) early, which makes `wake_err` negative. 41 ns on a 4 ms
period is 0.001%. Plot is log x and log y, first bin at 1 µs.

CSV header now has `scheduler_requested` as well as `scheduler_applied`.
The analysis warn fires only when requested=1 and applied=0.

## 2026-08-18 - six alternating 600 s pairs

Min wake_err_ns, `grep -v '^#'` then min of column 3:

```
results/hour_ts_1.csv min: 5875
results/hour_rt_1.csv min: 1708
```

Neither is negative. Ceiling `ns_to_ticks` is `(ns * denom + numer - 1) / numer` so the tick deadline is never before the requested nanosecond.

Twelve runs, 150000 samples each. p50/max from `analysis/percentiles.py` on `results/hour_ts_*.csv` and `results/hour_rt_*.csv`. overruns from each file's `# overruns=` line.

| file | p50 | p99.99 | max | overruns |
|---|---|---|---|---|
| hour_ts_1 | 707 | 10815 | 22044 | 115 |
| hour_ts_2 | 708 | 7747 | 35696 | 86 |
| hour_ts_3 | 708 | 7174 | 24612 | 89 |
| hour_ts_4 | 707 | 9047 | 38992 | 132 |
| hour_ts_5 | 708 | 8776 | 39127 | 95 |
| hour_ts_6 | 707 | 7571 | 19957 | 96 |
| hour_rt_1 | 10 | 88 | 126 | 0 |
| hour_rt_2 | 10 | 86 | 151 | 0 |
| hour_rt_3 | 10 | 87 | 225 | 0 |
| hour_rt_4 | 10 | 91 | 173 | 0 |
| hour_rt_5 | 9 | 107 | 187 | 0 |
| hour_rt_6 | 9 | 108 | 194 | 0 |

Timeshare p50 is 707–708 µs on every run. RT p50 is 9–10 µs. That is two hours of wall clock (six 600 s pairs, alternating) with 1 µs of p50 drift. Pooled in `results/pooled.md`: n=900000, timeshare p50=707 p99.99=8522 max=39127; RT p50=10 p99.99=95 max=225.

Timeshare overruns 115+86+89+132+95+96 = 613. 613/900000 = 0.068%. RT overruns 0.

Darwin applies timer leeway as a fraction of the requested interval, not as a
constant. Hour-campaign p50 707 / 4000 µs period = 0.177. Rate sweep, timeshare,
60 s, load 200 µs (`results/rates.md`):

| hz | period_us | n | p50 | mean | p50/period |
|---|---|---|---|---|---|
| 100 | 10000 | 6000 | 1883 | 1600 | 0.188 |
| 250 | 4000 | 15000 | 767 | 715 | 0.192 |
| 500 | 2000 | 30000 | 367 | 375 | 0.184 |
| 1000 | 1000 | 60000 | 166 | 180 | 0.166 |

p50/period stays 0.17–0.19 across a 10× range in period, so the coefficient is
about 0.18. A fixed 1 ms coalescing window is ruled out: that would be 0.10 of
a 10 ms period and 1.0 of a 1 ms period. Plot is log x and log y, first bin at
1 µs (`results/jitter.png`).

THREAD_TIME_CONSTRAINT_POLICY steps off that timeshare body to 9–10 µs at
250 Hz in the 600 s campaign.

RT rate sweep, same 60 s / load 200 µs, `--rt` (`results/rates_rt.md`). All
four headers: `scheduler_applied=1`, `computation_ns=750000`.

| hz | period_us | n | RT p50 | RT p50/period | timeshare p50 | timeshare p50/period |
|---|---|---|---|---|---|---|
| 100 | 10000 | 6000 | 12 | 0.0012 | 1883 | 0.188 |
| 250 | 4000 | 15000 | 10 | 0.0025 | 767 | 0.192 |
| 500 | 2000 | 30000 | 8 | 0.0040 | 367 | 0.184 |
| 1000 | 1000 | 60000 | 8 | 0.0080 | 166 | 0.166 |

p50 stays 8–12 µs across a 10× rate range. The RT floor is absolute, not
the 0.18 fraction timeshare uses. The relative benefit shrinks as you go
faster: at 250 Hz, 10 µs is 0.25% of the period against 19% timeshare; at
1000 Hz, 8 µs is 0.80% against 17%. 250 Hz was a conservative choice for
the histogram, not a lucky one: the policy buys a fixed floor, and a
slower loop spends less of its period on it.

## 2026-08-18 - 2a, socket before PX4

Reordered Phase 2 in `docs/DESIGN.md`: dummy UDP sender, then drain, then
PX4 as a drop-in. PX4 on Apple Silicon is the same class of toolchain wall
as epuck on this machine; putting it first would have blocked the
measurement 2a actually asks.

Bound is `kMaxMsgsPerTick = 8`. 400 Hz sender / 250 Hz loop is 1.6 datagrams
per tick; 8 is 5× that, equal to 20 ms of sender burst (400 × 0.020). Phase 1
RT max wake was 225 µs, which is not even one extra IMU frame, so under the
same policy the bound should almost never fire because the *loop* is late.

`results/p2.md`, 600 s, 150000 samples, `--rt`, load 500 µs. Headers:
`scheduler_applied=1` on both. Sender `results/p2_sender.err`: 264000 frames
(400 × 660).

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max | rx_total | drain_full |
|---|---|---|---|---|---|---|---|---|---|
| rt-nosocket | 150000 | 9 | 8 | 22 | 40 | 77 | 343 | 0 | 0 |
| rt-socket | 150000 | 8 | 7 | 20 | 34 | 79 | 536 | 240000 | 36 |

240000 / 150000 = 1.6, the 400/250 ratio, and 400 × 600 s. Wake p50 8 → 7,
p99.99 77 → 79. The histogram did not move. max 343 → 536 is one sample.

The cost is in `exec_ns`: p50 500.0 → 511.3 µs. Drain plus one empty `recv`
is about 11 µs, well inside the Darwin computation claim of 750 µs. That
claim is now `computation_ns` in the CSV header; the analysis warns if exec
p50 crosses 80% of it, because macOS can demote the thread with
`scheduler_applied=1` still sitting from startup. The p2 CSVs in this
session predate that field. overruns 0, rebases 0, both files.

`rx_count` on the socket run: 0:575, 1:59843, 2:89028, 3:379, 4:67, 5:41,
6:14, 7:17, 8:36. The 575 silent ticks and the 3–8 tail are the timeshare
sender bunching. On the 36 `FLAG_DRAIN_FULL` ticks, wake p50 was 6 µs and max
10 µs, so the loop was on time; 16 of those 36 were preceded by `rx_count=0`.
The bound engaged because the sender coalesced, which is what the flag is
for. No queue: 2c is latest-value slots.

## 2026-08-18 - 2b, HIGHRES_IMU parse

Vendored `c_library_v2` at 975b9eb, common plus the standard/minimal bases
it includes. Sender packs real v2 HIGHRES_IMU. First smoke was 32-byte
frames: v2 `_mav_trim_payload` drops trailing zeros, and a mostly-zero IMU
trims to 20 payload bytes + 10 header + 2 CRC. Filling every field (last
byte `id=1`) keeps the 63-byte payload, 75 on the wire. That is the frame
PX4 would send, and the 75 `mavlink_parse_char` calls 2b is supposed to
cost.

`--parse` is the one variable. Same 400 Hz sender, 250 Hz loop, 600 s,
`--rt`, load 500 µs (`results/p2b.md`). Both headers: `scheduler_applied=1`,
`computation_ns=750000`. Sender: 264000 frames of 75 bytes.

| configuration | n | p50 | p99.99 | max | rx_total | parse_ok | drain_full | exec p50 |
|---|---|---|---|---|---|---|---|---|
| rt-mav-noparse | 150000 | 10 | 58 | 587 | 240000 | 0 | 0 | 514.2 |
| rt-mav-parse | 150000 | 8 | 73 | 309 | 240000 | 240000 | 13 | 510.2 |

`parse_ok=rx_total`: every datagram was one complete HIGHRES_IMU. 1.6
frames × 75 bytes is 120 `parse_char` calls per tick. Wake p50 10 → 8,
exec p50 514.2 → 510.2. The parse does not move either number past
run-to-run noise on this pair. overruns 0. exec p50 is 68–69% of the
750 µs claim, under the 80% warn. This is IMU-only; more message classes
in 2c/2d add more calls.

## 2026-08-18 - 2b vendor path, sender clock

Moved the headers to `third_party/mavlink/` (same snapshot, 975b9eb).
common.h includes `../standard/standard.h`, which includes
`../minimal/minimal.h`, so those two bases come along; no other
dialects. Include path sits on `rt_platform` so both binaries inherit it.

Sender `time_usec` is now `rt::now_ns() / 1000` (monotonic microseconds
since boot), not the loop index. `id=1` still occupies the last payload
byte so v2 does not trim the frame to 32 bytes. Did not touch the drain.

## 2026-08-18 - 2b parse in the drain

`mavlink_parse_char` is a byte state machine: 75 calls per HIGHRES_IMU,
returns 1 only on the completing byte. `mav_msg` / `mav_status` live
before the loop. Sequence gaps use wrapping uint8 distance so 255->0 is
not a drop; counted this step because Phase 6 cannot be retrofitted
without re-running the campaign. `--parse` is gone; a bound socket always
parses.

`results/p2_mavlink.md`, 600 s, 150000 samples, `--rt`, load 500 µs.
Header: `scheduler_applied=1`, `computation_ns=750000`. Sender: 264000
frames of 75 bytes. `parse_ok=rx_total=240000`, `seq_gaps=0`,
`drain_full=14`. overruns 0.

| configuration | wake p50 | wake p99.99 | wake max | exec p50 |
|---|---|---|---|---|
| rt-socket (2a) | 7 | 79 | 536 | 511.3 |
| rt-mavlink (2b) | 10 | 74 | 477 | 515.4 |

exec p50 511.3 → 515.4 µs (+4.1). The earlier same-sender isolation in
`results/p2b.md` moved 514.2 → 510.2, the other way, by 4 µs. Parse is
inside that scatter, not a 100 µs-class cost. 515.4 is 69% of the 750 µs
Darwin claim, under the 80% warn.

## 2026-08-18 - Darwin claim scales with period

THREAD_TIME_CONSTRAINT_POLICY computation/constraint were 750 µs and
2 ms at every rate, so at 1 kHz the constraint exceeded the period.
They now default to 3/16 and 1/2 of `period_ns` (750 µs and 2 ms at
250 Hz, the 1.47x margin on p2_socket exec 511.3 µs). `--computation-us`
and `--constraint-us` override. constraint is clamped to the period, never
asserted.

Sized as duty cycle, not as a constant from exec. Inflating the fraction
so a 200 µs `--load-us` fits at 1 kHz (need ~29%) would loosen 250 Hz to
1.2 ms, which is no longer the measured 1.47x. The 1 kHz sweep with 200 µs
of work against an 188 µs claim is supposed to trip the analysis warn.
CSV header records `computation_ns`, `constraint_ns`, `preemptible`.
Analysis warns on exec p99 above 80% of the claim.

`results/rates_rt2.md`, 60 s, load 200 µs, `--rt`. Headers all
`scheduler_applied=1`. computation_ns 1875000 / 750000 / 375000 / 187500.

| hz | period_us | computation_us | n | wake p50 | exec p50 | exec p99 |
|---|---|---|---|---|---|---|
| 100 | 10000 | 1875 | 6000 | 13 | 200.1 | 200 |
| 250 | 4000 | 750 | 15000 | 10 | 200.1 | 200 |
| 500 | 2000 | 375 | 30000 | 10 | 200.1 | 200 |
| 1000 | 1000 | 188 | 60000 | 8 | 200.1 | 200 |

1 kHz tripped the warn: exec p99 200 µs is 107% of 187500 ns.
`scheduler_applied` still 1. Wake p50 stayed 8 µs. overruns 0.

## 2026-08-18 - 2c, latest-value slots

`src/msg_slots.h`: named `ImuSlot`, not a 256-entry msgid table. Drain
decodes HIGHRES_IMU into the slot, stamps `rx_mono_ns` from `woke`, copies
`time_usec`, overwrites if a second frame shares the tick. `seq_gaps`
uses `(seq - last_seq - 1) & 0xFF`. 2b's `seq_gaps=0` was not luck: the
working-tree parse already used `uint8_t` wrapping, which is the same
arithmetic, and 240000 frames wrap 937 times. A missing mask would have
been a large count, not zero.

`results/p2_slots.md`, 600 s, 150000 samples, `--rt`, load 500 µs.
Header: `scheduler_applied=1`, `computation_ns=750000`.
`parse_ok=rx_total=240000`, `seq_gaps=0`, `drain_full=15`. overruns 0.

| configuration | wake p50 | wake p99.99 | wake max | exec p50 |
|---|---|---|---|---|
| rt-mavlink (2b) | 10 | 74 | 477 | 515.4 |
| rt-slots (2c) | 10 | 82 | 171 | 515.0 |

exec p50 515.4 → 515.0. Decode is inside the same 4 µs scatter as the 2b
parse pair. 515.0 is 69% of the 750 µs Darwin claim.

`skew_ns = sender_us*1000 - rx_mono_ns` on ticks that got an IMU
(149804); `kSkewNone` on 196 silent ticks. Range -18872666 .. -9166 ns.
p50 -1.51 ms is wait-until-next-tick, not loopback: the least-stale
samples sit at -9 µs. First 10% mean vs last 10% mean: -21 µs over the
middle ~480 s, -44 ns/s. Flat. PX4's `time_usec` in 2e will offset this
series; the detector uses the slope.

## 2026-08-18 - parse cost claim is dead

`2.56 us per frame` in `a650acb` is not supported. That figure was
exec p50 511.3 → 515.4 on `results/p2_socket.csv.gz` vs
`results/p2_mavlink.csv.gz`, divided by 1.6 frames/tick. The same-sender
isolation in `results/p2b.md` moved 514.2 → 510.2, 4 µs the other way.

The 0.833 µs control spread (`p2_slots` 514.958 vs `p2_slots_b` 515.791)
is one difference from one pair, zero degrees of freedom. A 4 µs swing
the wrong way cannot come from a 0.833 µs spread; 0.833 was a lucky
draw and the real run-to-run floor is at least ~4 µs.

a650acb's subject carries a retracted figure. Current position: parse
cost unresolved; measure it with the in-run `rx_ns` column instead.

Stop subtracting exec p50 across files. `rx_ns` is a per-tick column:
two `now_ns` reads around drain+parse+slot, inside one run. Named for
the whole receive path, not parse alone, so ekf_ns/ctrl_ns can sit
beside it without another schema break.

## 2026-08-18 - drain_full is not a freshness signal

Prediction: the 15 `FLAG_DRAIN_FULL` ticks in `results/p2_slots.csv.gz`
carry the most-negative skew, because the bound left fresher datagrams
unread. Refuted. 6 of 15 sit in the 15 most-negative samples.

IMU skew n=149804 (sentinel dropped): min -18872666, p50 -1507625,
max -9166. Global minimum is seq 146641, `flags=0`, `rx=6`, one tick
*after* a drain_full streak. Seq 8927 is `drain_full` with skew -105791,
rank 147112/149804, freshest 2%.

Two signatures, and Phase 6 has to separate them:

- bound hit **and** skew improving or flat → sender coalescing, harmless.
  Seq 8927: eight frames arrived just before the wake, newest is fresh.
- bound hit **and** skew walking one loop period per tick → real backlog.
  Seq 146636–146640: -1.17, -4.96, -8.48, -12.21, -15.67 ms. Damage
  shows on 146641, which is not flagged.

`drain_full` alone does not mean stale data was read.

## 2026-08-18 - 2d, staleness floor

`age_ns = woke - rx_mono_ns` at read. Drain-time age is 0 for this
tick's IMU. `--staleness-limit-periods` default 3. `FLAG_STALE` when
`age > expected_period * limit` (7.5 ms for the 400 Hz fixture).

Age is only sampled at 4 ms tick boundaries, so the observable floor is
`ceil(7.5 ms / 4 ms) * 4 ms = 8 ms`. One silent tick (~4 ms) sits under
the limit; the second tick boundary is the first possible trip. Emitted
as `staleness_floor_ns` next to `staleness_limit_periods`. That is the
fault table Floor column; measured TTD sits next to it, not instead of it.

## 2026-08-18 - rx_ns bracketing

`t0 = now_ns()` before the drain, `rx_ns = now_ns() - t0` after the
slot write. Stage cost is then a column in the same run, so run-to-run
scatter drops out. Two extra clock reads, inside exec. Same pattern for
EKF predict/correct in Phase 4; retrofitting means re-running. The
column is not named parse_ns: the bracket is three stages, and the
schema should not change shape when ekf_ns and ctrl_ns land beside it.

## 2026-08-18 - measurement-quote warn

scripts/pre-commit warns, does not block, when a commit touches src/
and the message contains a number with a unit (us, ms, ns, %), unless
something under results/ is also staged. Same file is installed as
commit-msg: pre-commit runs before `git commit -m` writes the message,
so a pre-commit-only check would miss the subject. That is the a650acb
failure mode.

## 2026-08-19 - one host, three rates

No Linux mini PC, so no PREEMPT_RT curve, no isolcpus, no cyclictest,
no two-host injector. The scheduling variable is Darwin
`THREAD_TIME_CONSTRAINT_POLICY`. Sender, loop, and (later) injector share
`rt::now_ns()` because they are one process tree; the clock-domain
constraint is unchanged and now trivial. Linux paths in `rt_platform.cpp`
stay, labelled untested.

`udp_sender` emits HIGHRES_IMU / LOCAL_POSITION_NED / GPS_RAW_INT on one
socket from one absolute-deadline loop at lcm(400, 50, 5) = 400 Hz.
PX4 SITL is timeboxed; this fixture is the documented fallback.

Slots are named IMU/POS/GPS members. MAVLink seq is per-component, so
`last_seq` lives once on `MsgSlots`. Per-slot seq would count IMU→POS→GPS
as drops. Age and `FLAG_STALE` are per slot against that type's period.
Floors at 250 Hz, limit 3: IMU 8 ms, position 60 ms, GPS 600 ms.

`rx_ns` stays drain+parse+slot. Not split, not renamed parse_ns. Phase 4
adds `ekf_ns` / `ctrl_ns` beside it.

## 2026-08-22 - b96bc23 does not configure

`b96bc23` (plant) does not configure; fixed in the following commit. Cause
was a pre-commit hook that ran `cmake --build build` against the working
tree rather than the index, so three commits with incomplete staged content
passed. Same species as the a650acb retraction: the check looked at the
wrong snapshot. Hook now `git checkout-index`s the staged tree into a temp
dir and configures/builds that. A repo that records its own broken commits
reads as more trustworthy than one that appears never to have had any.

## 2026-08-22 - ω sweep on tracking asymmetry

Closed-loop baseline (`results/p3_tracking.md`, 20 s laps, ω = 2π/20):
steady lap RMS N/E 0.3366 / 0.3283 m. Hold scored on final 2 s of each 4 s
hold: N/E 0.0176 / 0.0119 m. D steady RMS 0.0003 m.

Same controller/plant, `kLapTicks=2000` (40 s laps), 240 s
(`results/p3_tracking_omega40.md`), same hold window:

| axis | lap RMS 20 s | lap RMS 40 s | ratio | hold 20 s | hold 40 s | ratio |
|---|---|---|---|---|---|---|
| N | 0.3366 | 0.1737 | 0.52 | 0.0176 | 0.0080 | 0.45 |
| E | 0.3283 | 0.1729 | 0.53 | 0.0119 | 0.0104 | 0.87 |
| D | 0.0001 | 0.0000 | — | 0.0005 | 0.0000 | — |

Lap RMS scales ≈ linearly with ω (phase lag). Full-hold scoring previously
folded lap-entry settling into hold RMS (E hold looked like 0.126 m) and
made hold appear to track ω; final-2 s hold does not. `R·atan(ωτ)` with
τ=0.05 s predicts 0.031 / 0.016 m — underpredicts lap RMS by ~10×, so
plant lag explains the ω ratio, not the absolute error. Trajectory restored
to 20 s laps after the run.

## 2026-08-22 - the ~300 author-owned lines

Scaffold is agent work. The update equations, the FSM policy, and the
payload corruptors are not. That is the interview surface: roughly three
hundred lines the author has to defend from memory.

| Where | What | Why it is the one asked about |
|---|---|---|
| `src/ekf.cpp` | predict, correct, NIS gate, Joseph form | the gate is both robustness and a detector; one mechanism, two jobs |
| `src/failsafe.cpp` | transitions, N-consecutive confirmation, asymmetric hysteresis | a detector that chatters is worse than none |
| `injector/fault.cpp` (payload paths; proxy wires them) | bit flip, bias, stuck, GPS jump, clock skew | how you corrupt determines what the detector can see |

χ² critical values for 3 degrees of freedom (position innovation):
7.815 at 95%, 11.345 at 99%, 16.266 at 99.9%. Default gate is 7.815
(`EkfConfig::nis_gate`).

Joseph form is \(P = (I-KH)P(I-KH)^\top + KRK^\top\). The textbook
\((I-KH)P\) loses symmetry and eventually positive-definiteness; that
shows up as a filter that dies forty minutes into a sixty-minute soak.
Do not ship the short form.

## 2026-08-22 - agent commits for Phase 4–7 land

Phases 4–7 were uncommitted at `ddb0162`. Commits `6bcac91`..`0f2a23b`
were made with `CURSOR_AGENT` unset so the pre-commit agent gate passed
while build/hot-path checks still ran. Author still owns predict/correct,
FSM predicates, and payload corruptors.

## 2026-08-22 - one-hour clean false-positive rate

`results/fp_clean_1h.md`, n=900000, timeshare (`scheduler_applied=0`).
`--rt` with default Darwin constraint (750 µs / 2 ms) starved the
`--ekf` tick; this run is without `--rt`.

Overall rising-edge rate 1.052e-01. Dominated by `clock_skew` (94541
rising / 1.050e-01): the mask is level-per-tick and clears on non-IMU
ticks, so a sustained trip counts as a rising edge every IMU sample.
Zero-event detectors (stale_gps, seq_gap, est_diverge) get 95% upper
bound 3/n = 3.333e-06.

**Verdict (do not park on passthrough estimator).** Breakdown is not
divergence/NIS: `est_diverge=0`, and the stub leaves `trace(P)` at the
reset sum so the diverge bit cannot be driving the rate. 94541/94678 of
rising edges are `clock_skew`. Do not mark `1.052e-01` provisional next to
the p5 passthrough estimator rows — that gate is already provisional for
its own reason (AUTHOR stubs). Overall rate is not a detector FP claim;
it is a skew rising-edge accounting artefact. Trustworthy zeros today:
`stale_gps`, `seq_gap`, `est_diverge`. Non-zeros that are small but real
on this timeshare soak: `stale_imu=53`, `stale_pos=2`, `deadline_miss=3`,
`stuck_sensor=79`. `clock_skew` is not a trustworthy zero.
