# Runbook

Operational steps that are easy to get wrong at 2am. Design rationale stays
in `docs/DESIGN.md`; this file is the checklist you actually run.

## dummynet on loopback (Phase 5)

Uniform random loss/delay is a kernel pipe. The Phase 5 MAVLink proxy owns
burst loss, type-selective drops, corruption, stuck sensors, and skew.

### Bring up (example: 15% loss on port 14555)

```sh
# pipe 1, plr 0.15. numbers match results/dummynet_*.csv.gz in DESIGN.md.
sudo dnctl pipe 1 config plr 0.15
echo "dummynet in quick on proto udp from any to any port 14555 pipe 1" \
  | sudo pfctl -f -
sudo pfctl -e
```

Confirm with a short `loop_bench` + sender run and check `# rx_total=` against
the clean baseline.

### Tear down — order matters

```sh
# 1. pf first: stop referencing the pipe
sudo pfctl -d
sudo pfctl -f /etc/pf.conf   # or: sudo pfctl -F all  if that is your restore path

# 2. then flush dummynet
sudo dnctl -q flush
```

**Footgun:** `dnctl -q flush` (or `dnctl -q pipe flush`) while a pf rule still
points at the pipe blackholes the port instead of restoring it. The next run
shows `rx_total=0` and looks like 100% loss. That is not the injector; it is
teardown order. pf first, then dnctl.

If you already flushed early:

```sh
sudo pfctl -d
sudo pfctl -f /etc/pf.conf
sudo dnctl -q flush
# re-add the pipe only if you still need it
```

### Sanity after teardown

A clean drain against the sender should recover the no-loss `rx_total` (±1).
If it stays zero, pf is still diverting — fix pf before touching the proxy.

## Control / trajectory notes (Phase 3)

- Control runs every 5th 250 Hz tick (50 Hz). `tick_class` marks those rows.
- Trajectory setpoints are `trajectory_setpoint(ctrl_tick, ...)`. Same tick
  index → same setpoint. Do not drive the path from wall time if you need a
  bit-identical nominal for fault comparison.
- Velocity D differentiates the measurement, not the tracking error. The
  velocity integrator freezes while the accel command is at `amax`.
