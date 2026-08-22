# truth=results/p3_truth.csv
# samples=12400
# cycles_completed=10
# settle_cycles=1.0
# settle_dropped=1200
# setpoint_rx=12400
# window=first_active..+setpoint_rx (excludes post-loop coast)

| window | axis | peak |e| (m) | RMS (m) | hold RMS (m) | lap RMS (m) |
|---|---|---|---|---|---|
| full | N | 2.0000 | 0.3205 |  |  |
| full | E | 0.4829 | 0.3018 |  |  |
| full | D | 12.5947 | 1.0333 |  |  |
| steady | N | 0.4804 | 0.3053 | 0.0253 | 0.3366 |
| steady | E | 0.4829 | 0.3023 | 0.1263 | 0.3283 |
| steady | D | 0.0035 | 0.0003 | 0.0007 | 0.0001 |
