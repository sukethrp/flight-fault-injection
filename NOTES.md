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

The 707 µs p50 is consistent with timer coalescing on a roughly 1 ms grid, not
directly measured. A 1 ms grid would be roughly uniform on [0, 1000) with p50
near 500; the measured p50 is 707 with mean 665, which is skewed. Plot is log x
and log y, first bin at 1 µs (`results/jitter.png`).

Rate sweep, timeshare, 60 s, load 200 µs (`results/rates.md`):

| hz | period_us | n | p50 | mean |
|---|---|---|---|---|
| 100 | 10000 | 6000 | 1883 | 1600 |
| 250 | 4000 | 15000 | 767 | 715 |
| 500 | 2000 | 30000 | 367 | 375 |
| 1000 | 1000 | 60000 | 166 | 180 |

p50 scales with the period (about 0.17–0.19 of it), so it is not a fixed 1 ms
coalescing window. THREAD_TIME_CONSTRAINT_POLICY still steps off that timeshare
body to 9–10 µs at 250 Hz in the 600 s campaign.
