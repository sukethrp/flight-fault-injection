| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 9 | 9 | 18 | 25 | n/a | 54 |
| p5-passthrough | 15000 | 9 | 8 | 19 | 26 | n/a | 122 |
| p3-closed | 60000 | 10 | 9 | 20 | 30 | n/a | 135 |

exec_ns, microseconds. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 18.3 | 16.3 | 45.3 | 60.4 | n/a | 270.3 |
| p5-passthrough | 15000 | 14.4 | 12.5 | 37.2 | 47.6 | n/a | 71.6 |
| p3-closed | 60000 | 18.5 | 16.9 | 43.1 | 57.8 | n/a | 126.1 |

rx_ns, microseconds. Drain+parse+slot in-run. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 14.7 | 13.8 | 29.9 | 37.8 | n/a | 203.3 |
| p5-passthrough | 15000 | 11.8 | 11.0 | 26.4 | 34.8 | n/a | 70.5 |
| p3-closed | 60000 | 15.1 | 14.3 | 32.7 | 40.5 | n/a | 82.5 |

age_imu_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 0.0 | 0.0 | 0.0 | 0.0 | n/a | 0.0 |
| p5-passthrough | 15000 | 1.1 | 0.0 | 0.0 | 0.0 | n/a | 4037.1 |
| p3-closed | 60000 | 6.5 | 0.0 | 0.0 | 3997.8 | n/a | 20001.9 |

age_pos_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 8000.0 | 8000.0 | 16006.5 | 16011.7 | n/a | 16024.0 |
| p5-passthrough | 15000 | 8001.0 | 8000.1 | 16006.4 | 16011.9 | n/a | 19999.6 |
| p3-closed | 60000 | 8005.6 | 7999.9 | 16007.5 | 16032.1 | n/a | 39995.0 |

age_gps_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 15000 | 98000.4 | 98003.8 | 195999.6 | 196006.7 | n/a | 196026.9 |
| p5-passthrough | 15000 | 98000.0 | 97996.4 | 196001.0 | 196007.2 | n/a | 196012.8 |
| p3-closed | 60000 | 97999.9 | 97995.8 | 195998.8 | 196007.2 | n/a | 200010.0 |

ctrl_ns, microseconds. Non-control ticks (ctrl_ns==0) dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 3000 | 17.5 | 16.6 | 32.3 | n/a | n/a | 66.9 |
| p5-passthrough | 3000 | 11.8 | 11.1 | 21.3 | n/a | n/a | 28.2 |
| p3-closed | 12000 | 16.7 | 16.2 | 30.3 | 45.3 | n/a | 92.9 |

wake_err_ns by tick_class, microseconds. plain / ctrl / telem — asks whether the heavier control tick pushes the following deadline.

plain (tick_class==0):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 12000 | 9 | 9 | 19 | 25 | n/a | 36 |
| p5-passthrough | 12000 | 9 | 8 | 19 | 25 | n/a | 122 |
| p3-closed | 48000 | 10 | 9 | 20 | 30 | n/a | 93 |

ctrl (TICK_CTRL):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 3000 | 9 | 9 | 18 | n/a | n/a | 54 |
| p5-passthrough | 3000 | 9 | 8 | 18 | n/a | n/a | 58 |
| p3-closed | 12000 | 10 | 9 | 20 | 27 | n/a | 135 |

telem (TICK_TELEM):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p5-base | 600 | 9 | 8 | n/a | n/a | n/a | 20 |
| p5-passthrough | 600 | 9 | 9 | n/a | n/a | n/a | 58 |
| p3-closed | 2400 | 9 | 8 | 20 | n/a | n/a | 135 |

