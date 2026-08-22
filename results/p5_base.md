| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 9 | 9 | 18 | 25 | n/a | 54 |

exec_ns, microseconds. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 18.3 | 16.3 | 45.3 | 60.4 | n/a | 270.3 |

rx_ns, microseconds. Drain+parse+slot in-run. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 14.7 | 13.8 | 29.9 | 37.8 | n/a | 203.3 |

age_imu_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 0.0 | 0.0 | 0.0 | 0.0 | n/a | 0.0 |

age_pos_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 8000.0 | 8000.0 | 16006.5 | 16011.7 | n/a | 16024.0 |

age_gps_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 98000.4 | 98003.8 | 195999.6 | 196006.7 | n/a | 196026.9 |

ctrl_ns, microseconds. Non-control ticks (ctrl_ns==0) dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 3000 | 17.5 | 16.6 | 32.3 | n/a | n/a | 66.9 |

wake_err_ns by tick_class, microseconds. plain / ctrl / telem — asks whether the heavier control tick pushes the following deadline.

plain (tick_class==0):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 12000 | 9 | 9 | 19 | 25 | n/a | 36 |

ctrl (TICK_CTRL):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 3000 | 9 | 9 | 18 | n/a | n/a | 54 |

telem (TICK_TELEM):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 600 | 9 | 8 | n/a | n/a | n/a | 20 |

