# Engineering standards

This file is the source of truth for engineering conventions in this
repository. Any local agent configuration is derived from it; a fresh
clone does not carry untracked editor copies under `.cursor/`.

## Scope and deliverables

This repo is a fixed-rate flight control loop under deliberate fault
injection. The deliverables are two measurements:

1. A loop jitter histogram out to p99.99 in microseconds.
2. A fault table with time-to-detect and time-to-recover per injected
   failure.

Everything else — plant, injector, analysis scripts, tests, docs —
exists to produce those two artifacts. Numbers quoted in the README,
comments, or commit messages must trace to a file in `results/`. If a
number is not measured yet, the cell stays empty.

The C++ side is standard library plus POSIX. Dependencies are not
added casually. Instrumentation branches that make the tail look worse
(overrun and rebase flags) stay; removing them would make the histogram
look better and be wrong.

## Hand-authored components

Roughly three hundred lines carry the design decisions this project is
meant to demonstrate. Those lines are written and defended by hand
rather than generated, because the portfolio value is being able to
defend them from memory. Defense crib: `NOTES.md` (2026-08-22).

| File | Hand-authored surface | Why it is the interview surface |
|---|---|---|
| `src/loop_bench.cpp` | loop timing math and deadline policy | absolute deadlines vs relative sleep; overrun/rebase honesty |
| `src/ekf.cpp` | predict, correct, NIS gate, Joseph-form P update | the gate is both robustness and a detector |
| `src/failsafe.cpp` | transition predicates, N-consecutive confirmation, asymmetric hysteresis | a chattering FSM is worse than none |
| `injector/fault.cpp` | payload corruption: bit flip, bias, stuck, GPS jump, clock skew | how you corrupt determines what the detector can see |

Scaffold around those surfaces (wiring, CSV columns, configs, tests) is
ordinary engineering work. Reviewing, critiquing, and explaining
tradeoffs in the hand-authored files is welcome; rewriting them is not
the point of the repo.

## Hot path invariants

Code inside the fixed-rate loop body runs 250 times per second with a
hard deadline. A single violation shows up as a spike in the p99.99
tail — the number this repo exists to report. These are not style
preferences.

Inside the loop body, nothing may:

- allocate or free (`new`, `malloc`, `push_back`, `std::string`
  construction, `std::vector` resize, `std::function`). Buffers are
  sized and touched before the loop starts.
- perform I/O (`printf`, `std::cout`, `fprintf`, or any logging call).
  stdio takes a lock and can fault a page. Samples go into the
  preallocated `RingLog` and are written after the run ends.
- take a lock or block (mutex, condition variable, `std::async`).
- throw. Exception unwinding is unbounded. Return status instead.
- call anything whose worst-case duration cannot be stated.

Standing rules for `src/`:

- **Monotonic clocks only.** `CLOCK_MONOTONIC` on Linux,
  `CLOCK_UPTIME_RAW` on Darwin. Never wall clock, never
  `std::chrono::system_clock`: an NTP correction mid-run corrupts the
  whole dataset.
- **Absolute deadlines only.** `clock_nanosleep(TIMER_ABSTIME)` and
  `mach_wait_until`. A relative sleep folds each wake error into the
  following period. Measured on Darwin at 250 Hz for 10 s
  (`NOTES.md` 2026-08-16): relative `nanosleep(period)` ran 12.320 s
  (23.2% slow); absolute `mach_wait_until` with the deadline advanced
  outside the sleep landed on 10.000 s. The same relative test on a
  Linux VM drifted 3.9%.
- **Platform differences live behind `rt_platform.h`** and nowhere
  else. No `#ifdef __APPLE__` in loop or estimator code.
- **Privilege failures downgrade to a recorded warning, never an
  abort.** The same binary must run unprivileged on a laptop and
  privileged on an RT host; the CSV header records which configuration
  actually applied (`scheduler_applied`, `memory_locked`,
  `affinity_set`).
- Prefer `-O2` over `-O3` and keep the generated code legible.

Mark the loop body with sentinels so `scripts/pre-commit` can scan
between them:

```
// HOT PATH BEGIN
// HOT PATH END
```

The hook greps the staged blob between those markers for banned calls
and refuses the commit on a hit.

## Measurement conventions

Statistical honesty is part of the deliverable, not a plotting
preference.

- A percentile needs roughly ten samples above it to mean anything. Do
  not report p99.99 from fewer than 100,000 samples (about 400 s at
  250 Hz; hour-scale runs in `results/hour_*.csv.gz` and
  `results/pooled.md` use 150,000 per file / 900,000 pooled). Below
  that threshold, print `n/a` and warn with how long a run would need
  to be.
- Report the sample count beside every percentile, in tables and plot
  legends.
- Latency and jitter distributions use a log y-axis. A linear axis
  flattens the tail, and the tail is the finding.
- Figure labels come from the CSV `#`-prefixed metadata header, never
  the filename. If `scheduler_applied=0`, the curve is labelled best
  effort and the script warns.
- Never smooth, clip, or drop outliers. The outliers are the result.
- Report max alongside percentiles. p99.99 with no max hides the worst
  case.
- Zero-event rates report the 95% upper bound as 3/n (rule of three),
  not zero. Example: `results/fp_clean_1h.md`, n=900000, zero-event
  detectors (`stale_gps`, `seq_gap`, `est_diverge`) bound at
  3.333e-06.

Analysis scripts take explicit input paths and an `--out` path. No
hardcoded filenames, no globbing inside the script. Same CSVs in, same
numbers out; seed anything random. Analysis dependencies are numpy and
matplotlib; ask before adding pandas, scipy, or seaborn. Percentile
tables emit as markdown to stdout for pasting into the README; figures
and warnings go to stderr or to disk.

## Comment and code style

Comment the why, not the what. The code already says what it does. A
comment earns its place by recording a decision, a constraint, a
measured number, a rejected alternative, or a trap.

Banned:

- section banners (`// ===== HELPERS =====`)
- step narration (`// Step 1: initialize`)
- hedging filler ("Note that", "It's important to", "This ensures that",
  "Here we", "We can now", "Simply")
- docstrings that restate the signature
- emoji, decorative ASCII, exclamation marks
- sentences that teach a reader rather than warn a maintainer

Prefer a measured number over an adjective. Name the trap: if deleting
a line would make the numbers look better and be wrong, say so.
Lowercase after `//` unless the sentence starts with an identifier or
acronym. One or two lines; longer rationale belongs in `NOTES.md` or
`docs/DESIGN.md` with a pointer from the code. Comment density is low;
when unsure, leave the comment out.

Code stays short and concrete (`drain_socket`, not `process_data`). No
defensive scaffolding nobody asked for: no logging framework, no config
layer, no abstract base class with one implementation, no `utils.h`. No
try/catch in `src/`; return status.

## Commit discipline

Every commit builds. After `b96bc23` shipped a plant commit that did not
configure — because the hook ran `cmake --build build` against the
working tree while unstaged files completed the compile —
`scripts/pre-commit` now `git checkout-index`s the staged tree into a
temp directory and configures/builds that. The working tree being green
is not enough.

A commit that quotes a measurement must carry the measurement. After
`a650acb` quoted a per-frame parse cost (`2.56 us`) derived from
cross-run exec p50 that `results/p2b.md` later retracted, the hook
warns (as `commit-msg`) when `src/` changes, the subject contains a
number with a unit, and nothing under `results/` is staged.

Read the diff before staging. The author writes every commit message;
agent shells are refused by the hook when `CURSOR_AGENT` is set.
