#!/usr/bin/env python3
"""Peak and RMS tracking error: plant truth vs tick-indexed trajectory.

Must match src/trajectory.cpp (hold + lap periods). Aligns on the first truth
sample whose |a_cmd| leaves the open-loop idle band — that is when the loop's
setpoints start arriving — then indexes ctrl_tick at 50 Hz for exactly
setpoint_rx samples (the plant keeps the last a_cmd after the loop exits, so
wall-clock truth past that window is not a tracking measurement).
"""

from __future__ import annotations

import argparse
import math
import sys

import numpy as np

# Keep in lockstep with src/trajectory.cpp.
CTRL_DT = 0.02
RADIUS = 2.0
ALT = -1.0
LAP_TICKS = 1000
HOLD_TICKS = 200
CYCLE_TICKS = LAP_TICKS + HOLD_TICKS
OMEGA = (2.0 * math.pi) / (LAP_TICKS * CTRL_DT)


def trajectory_setpoint(ctrl_tick: int) -> np.ndarray:
    phase = ctrl_tick % CYCLE_TICKS
    if phase < HOLD_TICKS:
        return np.array([RADIUS, 0.0, ALT], dtype=np.float64)
    lap_t = (phase - HOLD_TICKS) * CTRL_DT
    th = OMEGA * lap_t
    return np.array([RADIUS * math.cos(th), RADIUS * math.sin(th), ALT], dtype=np.float64)


def load_truth(path: str):
    meta = {}
    rows = []
    with open(path, "rt") as f:
        header = None
        for line in f:
            if line.startswith("#"):
                body = line[1:].strip()
                if "=" in body:
                    k, _, v = body.partition("=")
                    meta[k.strip()] = v.strip()
                continue
            parts = [p.strip() for p in line.split(",")]
            if header is None:
                header = parts
                continue
            rec = {header[i]: parts[i] for i in range(min(len(header), len(parts)))}
            rows.append(
                (
                    int(rec["t_ns"]),
                    float(rec["px"]),
                    float(rec["py"]),
                    float(rec["pz"]),
                    float(rec["ax_cmd"]),
                    float(rec["ay_cmd"]),
                    float(rec["az_cmd"]),
                )
            )
    return meta, np.asarray(rows, dtype=np.float64)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("truth_csv")
    p.add_argument("--out", required=True)
    p.add_argument(
        "--cmd-idle",
        type=float,
        default=0.05,
        help="|a_cmd| below this counts as open-loop idle before the loop connects",
    )
    p.add_argument(
        "--settle-cycles",
        type=float,
        default=1.0,
        help="drop this many hold+lap cycles before steady peak/RMS (catch-up from free-fall)",
    )
    args = p.parse_args()

    meta, rows = load_truth(args.truth_csv)
    if rows.size == 0:
        print("empty truth", file=sys.stderr)
        return 1

    cmd_norm = np.linalg.norm(rows[:, 4:7], axis=1)
    active = np.flatnonzero(cmd_norm > args.cmd_idle)
    if active.size == 0:
        print("no active a_cmd; loop never connected?", file=sys.stderr)
        return 1
    start = int(active[0])

    n_tx = None
    if "setpoint_rx" in meta:
        try:
            n_tx = int(meta["setpoint_rx"])
        except ValueError:
            n_tx = None
    if n_tx is None or n_tx <= 0:
        n_tx = int(active.size)

    end = min(start + n_tx, rows.shape[0])
    body = rows[start:end]
    n = body.shape[0]
    if n == 0:
        print("empty active window", file=sys.stderr)
        return 1

    err = np.empty((n, 3), dtype=np.float64)
    hold_mask = np.zeros(n, dtype=bool)
    for i in range(n):
        sp = trajectory_setpoint(i)
        err[i] = body[i, 1:4] - sp
        hold_mask[i] = (i % CYCLE_TICKS) < HOLD_TICKS

    settle_n = int(args.settle_cycles * CYCLE_TICKS)
    steady = err[settle_n:] if settle_n < n else err
    steady_hold = hold_mask[settle_n:] if settle_n < n else hold_mask

    def peak_rms(e):
        return np.max(np.abs(e), axis=0), np.sqrt(np.mean(e * e, axis=0))

    peak_all, rms_all = peak_rms(err)
    peak, rms = peak_rms(steady)
    hold_err = steady[steady_hold]
    lap_err = steady[~steady_hold]
    hold_rms = (
        np.sqrt(np.mean(hold_err * hold_err, axis=0))
        if hold_err.size
        else np.full(3, float("nan"))
    )
    lap_rms = (
        np.sqrt(np.mean(lap_err * lap_err, axis=0))
        if lap_err.size
        else np.full(3, float("nan"))
    )
    n_laps = n // CYCLE_TICKS

    lines = [
        f"# truth={args.truth_csv}",
        f"# samples={n}",
        f"# cycles_completed={n_laps}",
        f"# settle_cycles={args.settle_cycles}",
        f"# settle_dropped={settle_n}",
        f"# setpoint_rx={meta.get('setpoint_rx', '')}",
        f"# window=first_active..+setpoint_rx (excludes post-loop coast)",
        "",
        "| window | axis | peak |e| (m) | RMS (m) | hold RMS (m) | lap RMS (m) |",
        "|---|---|---|---|---|---|",
    ]
    for i, ax in enumerate("NED"):
        lines.append(
            f"| full | {ax} | {peak_all[i]:.4f} | {rms_all[i]:.4f} |  |  |"
        )
    for i, ax in enumerate("NED"):
        lines.append(
            f"| steady | {ax} | {peak[i]:.4f} | {rms[i]:.4f} | "
            f"{hold_rms[i]:.4f} | {lap_rms[i]:.4f} |"
        )
    text = "\n".join(lines) + "\n"
    sys.stdout.write(text)
    with open(args.out, "w") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
