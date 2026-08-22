#!/usr/bin/env python3
"""Time-to-detect box plots per fault, with theoretical floor marked.

Input is a long-format CSV (one row per injection): fault_id,ttd_ns,floor_ns.
Produced by analysis/fault_table.py --long-out. Floor is the detector's
ceil(limit/period)*period value, not a measured TTD. Log y so the IMU
millisecond floor and the GPS hundreds-of-ms floor share one axis.
"""

from __future__ import annotations

import argparse
import sys

import matplotlib.pyplot as plt
import numpy as np


def load_long(path):
    faults = []
    ttd_us = []
    floor_us = []
    header = None
    with open(path, "rt") as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if header is None:
                header = parts
                continue
            rec = {header[i]: parts[i] for i in range(min(len(header), len(parts)))}
            faults.append(rec["fault_id"])
            ttd_us.append(float(rec["ttd_ns"]) / 1000.0)
            floor_us.append(float(rec["floor_ns"]) / 1000.0)
    return np.asarray(faults), np.asarray(ttd_us), np.asarray(floor_us)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("ttd_csv", help="long-format fault_id,ttd_ns,floor_ns")
    p.add_argument("--out", required=True)
    args = p.parse_args()

    faults, ttd_us, floor_us = load_long(args.ttd_csv)
    if faults.size == 0:
        print(f"{args.ttd_csv}: empty", file=sys.stderr)
        return 1

    order = []
    for f in faults:
        if f not in order:
            order.append(f)

    data = []
    floors = []
    labels = []
    for f in order:
        mask = faults == f
        vals = ttd_us[mask]
        # drop non-positive / missing detections
        vals = vals[np.isfinite(vals) & (vals >= 0)]
        data.append(vals)
        fl = floor_us[mask]
        floors.append(float(np.median(fl)) if fl.size else float("nan"))
        labels.append(f"{f}\nn={vals.size}")

    fig, ax = plt.subplots(figsize=(max(8, 1.1 * len(order)), 5))
    bp = ax.boxplot(data, labels=labels, showfliers=True, whis=(5, 95))
    for i, fl in enumerate(floors):
        if not np.isfinite(fl) or fl <= 0:
            continue
        ax.hlines(fl, i + 0.6, i + 1.4, colors="C3", linestyles="--", lw=1.2)
    ax.plot([], [], color="C3", ls="--", label="theoretical floor")
    ax.set_yscale("log")
    ax.set_ylabel("time-to-detect (µs)")
    ax.set_xlabel("fault")
    ax.legend()
    fig.text(
        0.5,
        0.01,
        "floor = ceil(limit / loop_period) * loop_period from the run header",
        ha="center",
    )
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    fig.savefig(args.out, dpi=150)
    plt.close(fig)
    # silence unused; keeps flake quiet if styles change
    _ = bp
    print(args.out, file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
