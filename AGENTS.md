# Agent instructions

Fixed-rate flight control loop under deliberate fault injection. Deliverables are
a loop jitter histogram out to p99.99 in microseconds and a fault table with
time-to-detect and time-to-recover per injected fault.

Hot path (`src/`, inside the loop body): no allocation, no I/O, no locks, no
exceptions. Monotonic clocks only. Absolute deadlines only. Platform differences
live behind `rt_platform.h`.

Do not author the loop timing math, the estimator update, the fallback state
machine, or the fault injector's corruption logic. Review those instead.

Comment the why, never the what. No step narration, no section banners, no
filler. Zero comments beats filler comments.

Do not invent measurement numbers. Every number must trace to a file in
`results/`. Do not commit; the author writes every commit message.

Full detail in `.cursor/rules/`.
