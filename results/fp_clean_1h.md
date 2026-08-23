# false_positive_rate samples=900000
# label=fp-clean-1h
# scheduler_applied=0
| Detector | Rising edges | Rate | 95% upper (rule of 3) |
|---|---|---|---|
| stale_imu | 53 | 5.888889e-05 | n/a |
| stale_pos | 2 | 2.222222e-06 | n/a |
| stale_gps | 0 | 0.000000e+00 | 3.333333e-06 |
| seq_gap | 0 | 0.000000e+00 | 3.333333e-06 |
| clock_skew | 94541 | 1.050456e-01 | n/a |
| deadline_miss | 3 | 3.333333e-06 | n/a |
| stuck_sensor | 79 | 8.777778e-05 | n/a |
| est_diverge | 0 | 0.000000e+00 | 3.333333e-06 |
| **all (rising sum)** | 94678 | 1.051978e-01 | n/a |
| ticks_with_any_det | 757494 | 8.416600e-01 |  |

n=900000
false_positive_rate=1.051978e-01
