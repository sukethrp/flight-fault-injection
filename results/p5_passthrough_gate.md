# Passthrough gate (Phase 5)

Same recipe: 60 s @ 250 Hz, `--rt --ekf`, noise/bias off, seed=1.
Baseline: plant→loop direct. Passthrough: plant→proxy→loop.

Packet accounting (`results/p5_base_loop.err`, `results/p5_pt_loop.err`,
`results/p5_pt_events.csv`):

| | baseline | passthrough |
|---|---|---|
| samples | 15000 | 15000 |
| rx_total | 27300 | 27300 |
| seq_gaps | 0 | 0 |
| setpoint_tx | 3400 | 3400 |
| proxy drop/delay/reorder | — | 0 / 0 / 0 |
| inject events | — | 0 |

Wake error µs (`results/p5_base.md`, `results/p5_pt.md`):

| | n | p50 | p99 | p99.9 | max |
|---|---|---|---|---|---|
| baseline | 15000 | 9 | 18 | 25 | 54 |
| passthrough | 15000 | 8 | 19 | 26 | 122 |
| Phase 3 closed (`results/p3_closed.md`) | 60000 | 9 | 20 | 30 | 135 |

Estimator |pos| error m (`results/p5_base_ekf.md`, `results/p5_pt_ekf.md`).

**Provisional.** `Ekf::predict` / `Ekf::correct` are still AUTHOR stubs
(trace_P stays at the reset diagonal sum). Base and passthrough agree
because both sides compare the same uninitialized estimate against truth —
identity under a broken filter is not estimator invariance. Re-run this
block once the update equations land. The rows below are retained only as
a record of what the gate printed.

| axis | base p50 | pt p50 | base p95 | pt p95 |
|---|---|---|---|---|
| N | 1.5222 | 1.5222 | 1.9850 | 1.9850 |
| E | 1.2608 | 1.2608 | 1.9717 | 1.9717 |
| D | 0.9999 | 0.9999 | 1.0000 | 1.0000 |

Steady tracking RMS m (`results/p5_base_track.md`, `results/p5_pt_track.md`):

| axis | base RMS | pt RMS | Phase 3 steady (`results/p3_tracking.md`) |
|---|---|---|---|
| N | 0.3060 | 0.3060 | 0.3053 |
| E | 0.3005 | 0.3005 | 0.3023 |
| D | 0.0007 | 0.0007 | 0.0003 |

Verdict: injector idle is invisible within run-to-run noise on wake
p50/p99, bit-identical on `rx_total` / `seq_gaps` / `setpoint_tx`, and
bit-identical on steady tracking RMS. Estimator p50/p95 identity is
**not yet meaningful** — gate provisional on that axis until
`predict`/`correct` exist.
