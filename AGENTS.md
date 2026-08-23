# Agent instructions

Fixed-rate flight control loop under deliberate fault injection.
Deliverables: loop jitter histogram (p99.99, µs) and a fault table
(time-to-detect / time-to-recover).

Hot path (`src/`, inside the loop body): no allocation, no I/O, no locks,
no exceptions. Monotonic clocks only. Absolute deadlines only. Platform
differences live behind `rt_platform.h`.

Hand-authored (review, do not rewrite): loop timing in `src/loop_bench.cpp`;
`src/ekf.cpp` predict/correct/NIS/Joseph; `src/failsafe.cpp`
transitions/confirmation/hysteresis; `injector/fault.cpp` payload
corruptors. Defense crib: `NOTES.md` 2026-08-22.

Full standards: `docs/ENGINEERING.md`.
