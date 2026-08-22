# Passthrough gate (Phase 5)

Idle proxy vs direct plant↔loop. 60 s, 250 Hz, `--rt --ekf`, plant noise off,
seed 1. Sources: `results/p5_base.csv` / `p5_pt.csv`, truths, events.
No Phase 4 estimator campaign CSVs in `results/` yet; the A/B here is the
gate. EKF `predict`/`correct` are still AUTHOR stubs, so absolute estimator
error is large and identical on both paths (trace_P stuck at 6).

## Counts

| | base (direct) | passthrough |
|---|---|---|
| samples | 15000 | 15000 |
| rx_total | 27300 | 27300 |
| parse_ok | 27300 | 27300 |
| seq_gaps | 0 | 0 |
| setpoint_tx | 3400 | 3400 |
| overruns | 0 | 0 |
| proxy drop/delay/reorder | — | 0/0/0 |

## Wake jitter (µs)

From `analysis/percentiles.py` → `results/p5_passthrough_jitter.md`.
p3-closed (240 s) included as the last published closed-loop reference.

| configuration | n | p50 | p99 | p99.9 | max |
|---|---|---|---|---|---|
| p5-base | 15000 | 9 | 18 | 25 | 54 |
| p5-passthrough | 15000 | 8 | 19 | 26 | 122 |
| p3-closed | 60000 | 9 | 20 | 30 | 135 |

p50/p99 within 1 µs of each other and of Phase 3. max is a single-sample
tail; not a body shift.

## Estimator error (EKF state vs plant truth)

`analysis/estimator_error.py`, TICK_CTRL rows only. Absolute error is the
stub floor (x stays at reset); the gate is base ↔ passthrough equality.

| quantity | axis | base p50 | pt p50 | base p95 | pt p95 | base max | pt max |
|---|---|---|---|---|---|---|---|
| pos_err_m | N | 1.5222 | 1.5222 | 1.9850 | 1.9850 | 1.9875 | 1.9875 |
| pos_err_m | E | 1.2608 | 1.2608 | 1.9717 | 1.9717 | 1.9825 | 1.9825 |
| pos_err_m | D | 0.9999 | 0.9999 | 1.0000 | 1.0000 | 1.0000 | 1.0000 |
| vel_err_mps | N | 0.3960 | 0.3960 | 0.6192 | 0.6192 | 0.6221 | 0.6221 |
| vel_err_mps | E | 0.3590 | 0.3590 | 0.6134 | 0.6134 | 0.6212 | 0.6212 |
| vel_err_mps | D | 0.0000 | 0.0000 | 0.0258 | 0.0263 | 0.0546 | 0.0556 |
| trace_P | - | 6.0000 | 6.0000 | 6.0000 | 6.0000 | 6.0000 | 6.0000 |

In-loop `est_err_um` (||p_hat − p_meas||): both n=15000, p50=2.2139 m,
p95=2.2249 m, max=2.2280 m.

## Tracking (plant truth vs trajectory)

Steady window after one settle cycle. Matches Phase 3 hold/lap split within
noise on the short 60 s window (2 cycles).

| | base N/E/D RMS | pt N/E/D RMS | p3 N/E/D RMS |
|---|---|---|---|
| steady | 0.3060 / 0.3005 / 0.0007 | 0.3060 / 0.3005 / 0.0007 | 0.3053 / 0.3023 / 0.0003 |

## Verdict

Passthrough is invisible on counts, wake body, estimator residual, and
tracking. Gate opens for fault injection campaigns.
