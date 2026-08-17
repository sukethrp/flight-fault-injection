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

**Status:** step 1 of 7, building the measurement instrument. Empty cells below
mean not measured yet, never estimated.

## Result

Wake-up error in microseconds, 250 Hz loop. Identical application code in
every row; the kernel and its scheduling policy are the only variable. Empty
cells are not measured yet. macOS rows are 20 s / 5000 samples from
`results/healthy.csv` and `results/healthy_rt.csv`. p99.9 and p99.99 stay
blank until there are enough samples above the quantile (p99.99 needs ~100,000).

| Configuration | Samples | p50 | p99 | p99.9 | p99.99 | max | overruns |
|---|---|---|---|---|---|---|---|
| macOS, best effort | 5000 | 708 | 872 | | | 5235 | 3 |
| macOS, THREAD_TIME_CONSTRAINT_POLICY | 5000 | 9 | 23 | | | 99 | 0 |
| Linux, CFS | | | | | | | |
| Linux, PREEMPT_RT, SCHED_FIFO, isolated core | | | | | | | |

`cyclictest` on the same machine gives the platform floor, plotted as a
reference line alongside these curves.

## Fault table

Time-to-detect is measured from the injector stamping an event to a detector
firing, both from the same monotonic clock. The injector runs on the real-time
host beside the flight software for exactly this reason: across two machines the
interval would be contaminated by clock offset.

| Fault | Injection | Detector | TTD p50 | TTD p95 | TTD max | Floor | TTR | End state |
|---|---|---|---|---|---|---|---|---|

## Layout

Target structure. Files appear as their step lands.

```
src/
  rt_platform.h/.cpp   monotonic clock + RT scheduling, Linux and Darwin
  ring_log.h           preallocated sample sink, no I/O in the loop
  loop_bench.cpp       the fixed-rate loop and its own instrumentation
  ekf.h/.cpp           6-state filter with NIS gating (step 4)
  detectors.h/.cpp     staleness, sequence, skew, divergence (step 6)
  failsafe.h/.cpp      fallback state machine (step 6)
injector/              MAVLink proxy, runs on the RT host (step 5)
analysis/              percentile tables and figures
scripts/pre-commit     hot path and authorship enforcement
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
