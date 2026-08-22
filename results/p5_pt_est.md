# loop=results/p5_pt.csv
# truth=results/p5_pt_truth.csv
# samples=3000
# ekf=1
# label=p5-passthrough
# truth_setpoint_rx=3400

| quantity | axis | n | p50 | p95 | max |
|---|---|---|---|---|---|
| pos_err_m | N | 3000 | 1.5222 | 1.9850 | 1.9875 |
| pos_err_m | E | 3000 | 1.2608 | 1.9717 | 1.9825 |
| pos_err_m | D | 3000 | 0.9999 | 1.0000 | 1.0000 |
| vel_err_mps | N | 3000 | 0.3960 | 0.6192 | 0.6221 |
| vel_err_mps | E | 3000 | 0.3590 | 0.6134 | 0.6212 |
| vel_err_mps | D | 3000 | 0.0000 | 0.0263 | 0.0556 |
| trace_P | - | 3000 | 6.0000 | 6.0000 | 6.0000 |
