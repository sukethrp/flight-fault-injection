#!/usr/bin/env python3
"""One representative run: injection, detection, FSM transition, recovery.

Three inputs on the same monotonic clock (rt::now_ns on one host):

  --events   injector event CSV (inject_mono_ns, fault_id, action, ...)
  --detects  detector/failsafe event CSV (mono_ns, kind, detail)
             kind ∈ {detect, transition, recover} (and aliases)
  --loop     optional loop CSV; plots |pos error| if ekf_* and a truth pair
             are not required — uses ekf_trace_p as a stand-in state signal

All markers share one time axis origin = first inject_mono_ns.
"""

from __future__ import annotations

import argparse
import gzip
import sys

import matplotlib.pyplot as plt
import numpy as np


def open_text(path):
    return gzip.open(path, "rt") if path.endswith(".gz") else open(path, "rt")


def load_csv(path):
    meta = {}
    rows = []
    header = None
    with open_text(path) as f:
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
            rows.append({header[i]: parts[i] for i in range(min(len(header), len(parts)))})
    return meta, rows


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--events", required=True, help="injector event CSV")
    p.add_argument("--detects", required=True, help="detector/failsafe event CSV")
    p.add_argument("--loop", default="", help="optional loop CSV for trace_P")
    p.add_argument("--out", required=True)
    args = p.parse_args()

    _, inj = load_csv(args.events)
    _, det = load_csv(args.detects)
    if not inj:
        print(f"{args.events}: no injection rows", file=sys.stderr)
        return 1

    t0 = min(int(r["inject_mono_ns"]) for r in inj)

    def sec(ns):
        return (int(ns) - t0) * 1e-9

    fig, ax = plt.subplots(figsize=(10, 4))

    # background state signal if present
    if args.loop:
        _, loop_rows = load_csv(args.loop)
        if loop_rows and "ekf_trace_p" in loop_rows[0] and "deadline_ns" in loop_rows[0]:
            t = np.asarray([sec(r["deadline_ns"]) for r in loop_rows], dtype=np.float64)
            tr = np.asarray([float(r["ekf_trace_p"]) for r in loop_rows], dtype=np.float64)
            ax.plot(t, tr, color="0.7", lw=0.6, label="trace(P)")

    y_inj, y_det, y_tr, y_rec = 3, 2, 1, 0
    for r in inj:
        ax.plot(sec(r["inject_mono_ns"]), y_inj, "v", color="C3", ms=8)
    ax.plot([], [], "v", color="C3", label="injection")

    for r in det:
        kind = r.get("kind", r.get("event", "")).lower()
        mono = r.get("mono_ns", r.get("detect_mono_ns", r.get("t_ns")))
        if mono is None:
            continue
        x = sec(mono)
        if kind in ("detect", "detector", "fire"):
            ax.plot(x, y_det, "o", color="C1", ms=7)
        elif kind in ("transition", "fsm", "state"):
            ax.plot(x, y_tr, "s", color="C0", ms=7)
        elif kind in ("recover", "recovery", "nominal"):
            ax.plot(x, y_rec, "^", color="C2", ms=8)

    ax.plot([], [], "o", color="C1", label="detection")
    ax.plot([], [], "s", color="C0", label="transition")
    ax.plot([], [], "^", color="C2", label="recovery")

    ax.set_yticks([y_rec, y_tr, y_det, y_inj])
    ax.set_yticklabels(["recovery", "transition", "detection", "injection"])
    ax.set_xlabel("t (s) from first injection")
    ax.set_title("state timeline (one representative run)")
    ax.legend(loc="upper right")
    fig.tight_layout()
    fig.savefig(args.out, dpi=150)
    plt.close(fig)
    print(args.out, file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
