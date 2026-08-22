#!/usr/bin/env python3
"""Estimator position error vs plant truth: clean control and per fault class.

One panel per fault class plus clean. Log |e| is not used — error is signed
meters on a linear axis so bias direction stays visible. Legend from CSV
# metadata. Explicit input paths only.
"""

from __future__ import annotations

import argparse
import os
import sys

import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))

from estimator_error import load_meta_rows, TICK_CTRL
from percentiles import display_name, warn_if_denied


def align_pos_err(loop_csv, truth_csv):
    loop_cols = [
        "deadline_ns",
        "tick_class",
        "ekf_pn",
        "ekf_pe",
        "ekf_pd",
    ]
    truth_cols = ["t_ns", "px", "py", "pz"]
    loop_meta, loop = load_meta_rows(loop_csv, loop_cols)
    _, truth = load_meta_rows(truth_csv, truth_cols)
    if loop["deadline_ns"].size == 0 or truth["t_ns"].size == 0:
        return loop_meta, None
    ctrl = (loop["tick_class"].astype(np.int64) & TICK_CTRL) != 0
    t_est = loop["deadline_ns"][ctrl]
    if t_est.size == 0:
        return loop_meta, None
    pn = loop["ekf_pn"][ctrl]
    pe = loop["ekf_pe"][ctrl]
    pd = loop["ekf_pd"][ctrl]
    t_truth = truth["t_ns"]
    idx = np.searchsorted(t_truth, t_est, side="left")
    idx = np.clip(idx, 0, t_truth.size - 1)
    prev = np.clip(idx - 1, 0, t_truth.size - 1)
    use_prev = np.abs(t_truth[prev] - t_est) < np.abs(t_truth[idx] - t_est)
    idx = np.where(use_prev, prev, idx)
    err = np.column_stack(
        [pn - truth["px"][idx], pe - truth["py"][idx], pd - truth["pz"][idx]]
    )
    t_s = (t_est - t_est[0]) * 1e-9
    return loop_meta, (t_s, err)


def main():
    p = argparse.ArgumentParser()
    p.add_argument(
        "--pair",
        action="append",
        nargs=3,
        metavar=("LABEL", "LOOP_CSV", "TRUTH_CSV"),
        required=True,
        help="repeatable: class label, loop csv, truth csv",
    )
    p.add_argument("--out", required=True)
    args = p.parse_args()

    n = len(args.pair)
    cols = 2
    rows = (n + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(10, 3.2 * rows), sharex=False)
    axes = np.atleast_1d(axes).ravel()

    for i, (label, loop_csv, truth_csv) in enumerate(args.pair):
        ax = axes[i]
        meta, aligned = align_pos_err(loop_csv, truth_csv)
        name = display_name(meta, loop_csv)
        warn_if_denied(loop_csv, meta, name)
        if aligned is None:
            ax.set_title(f"{label}: no data")
            ax.text(0.5, 0.5, "n/a", ha="center", va="center", transform=ax.transAxes)
            continue
        t_s, err = aligned
        for j, ax_name in enumerate("NED"):
            ax.plot(t_s, err[:, j], lw=0.8, label=f"{ax_name} n={err.shape[0]}")
        ax.set_title(f"{label} ({name})")
        ax.set_xlabel("t (s)")
        ax.set_ylabel("pos error (m)")
        ax.legend(fontsize=8)
        ax.axhline(0.0, color="k", lw=0.4)

    for j in range(i + 1, len(axes)):
        axes[j].set_visible(False)

    fig.tight_layout()
    fig.savefig(args.out, dpi=150)
    plt.close(fig)
    print(args.out, file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main() or 0)
