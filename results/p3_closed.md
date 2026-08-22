| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p3-closed | 60000 | 10 | 9 | 20 | 30 | n/a | 135 |

exec_ns, microseconds. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p3-closed | 60000 | 18.5 | 16.9 | 43.1 | 57.8 | n/a | 126.1 |

rx_ns, microseconds. Drain+parse+slot in-run. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p3-closed | 60000 | 15.1 | 14.3 | 32.7 | 40.5 | n/a | 82.5 |

age_imu_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p3-closed | 60000 | 6.5 | 0.0 | 0.0 | 3997.8 | n/a | 20001.9 |

age_pos_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p3-closed | 60000 | 8005.6 | 7999.9 | 16007.5 | 16032.1 | n/a | 39995.0 |

age_gps_ns, microseconds. kAgeNone rows dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p3-closed | 60000 | 97999.9 | 97995.8 | 195998.8 | 196007.2 | n/a | 200010.0 |

ctrl_ns, microseconds. Non-control ticks (ctrl_ns==0) dropped. Same files.

| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p3-closed | 12000 | 16.7 | 16.2 | 30.3 | 45.3 | n/a | 92.9 |

wake_err_ns by tick_class, microseconds. plain / ctrl / telem — asks whether the heavier control tick pushes the following deadline.

plain (tick_class==0):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p3-closed | 48000 | 10 | 9 | 20 | 30 | n/a | 93 |

ctrl (TICK_CTRL):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p3-closed | 12000 | 10 | 9 | 20 | 27 | n/a | 135 |

telem (TICK_TELEM):
| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|---|
| p3-closed | 2400 | 9 | 8 | 20 | n/a | n/a | 135 |

