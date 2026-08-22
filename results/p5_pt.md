| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-passthrough | 15000 | 9 | 8 | 19 | 26 | n/a | 122 |

exec_ns, microseconds. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-passthrough | 15000 | 14.4 | 12.5 | 37.2 | 47.6 | n/a | 71.6 |

rx_ns, microseconds. Drain+parse+slot in-run. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-passthrough | 15000 | 11.8 | 11.0 | 26.4 | 34.8 | n/a | 70.5 |

age_imu_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-passthrough | 15000 | 1.1 | 0.0 | 0.0 | 0.0 | n/a | 4037.1 |

age_pos_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-passthrough | 15000 | 8001.0 | 8000.1 | 16006.4 | 16011.9 | n/a | 19999.6 |

age_gps_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-passthrough | 15000 | 98000.0 | 97996.4 | 196001.0 | 196007.2 | n/a | 196012.8 |

ctrl_ns, microseconds. Non-control ticks (ctrl_ns==0) dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-passthrough | 3000 | 11.8 | 11.1 | 21.3 | n/a | n/a | 28.2 |

wake_err_ns by tick_class, microseconds. plain / ctrl / telem — asks whether the heavier control tick pushes the following deadline.

plain (tick_class==0):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-passthrough | 12000 | 9 | 8 | 19 | 25 | n/a | 122 |

ctrl (TICK_CTRL):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-passthrough | 3000 | 9 | 8 | 18 | n/a | n/a | 58 |

telem (TICK_TELEM):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-passthrough | 600 | 9 | 9 | n/a | n/a | n/a | 58 |

