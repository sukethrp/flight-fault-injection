#!/usr/bin/env python3
"""Per-axis estimator error: loop CSV EKF state vs plant --truth-out.

Aligns on monotonic time (deadline_ns ↔ t_ns). Keeps TICK_CTRL rows only —
those carry the 50 Hz EKF snapshot. Same # metadata block and percentile
sample-count gates as percentiles.py.
"""

from __future__ import annotations

import argparse
import gzip
import sys

import numpy as np

TICK_CTRL = 1 << 0


def pct(a, q, n_above_needed=10):
    n = a.size
    if n == 0:
        return None
    n_above = round(n * (1.0 - q))
    if n_above < n_above_needed:
        return None
    return float(np.quantile(a, q))


def fmt(x, prec=4):
    if x is None:
        return "n/a"
    return f"{x:.{prec}f}"


def load_meta_rows(path, numeric_cols):
    meta = {}
    rows = []
    header = None
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt") as f:
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
            rows.append(rec)
    if not rows:
        return meta, {c: np.asarray([], dtype=np.float64) for c in numeric_cols}
    out = {}
    for c in numeric_cols:
        if c not in rows[0]:
            out[c] = np.asarray([], dtype=np.float64)
            continue
        out[c] = np.asarray([float(r[c]) for r in rows], dtype=np.float64)
    return meta, out


def warn_tail(label, n, name):
    if n < 100_000 and name in ("p99.99",):
        print(
            f"{label}: n={n}, p99.99 needs ~100000 samples",
            file=sys.stderr,
        )


def axis_stats(err):
    n = err.size
    return {
        "n": n,
        "p50": pct(err, 0.50),
        "p95": pct(err, 0.95),
        "max": float(np.max(err)) if n else float("nan"),
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("loop_csv")
    p.add_argument("truth_csv")
    p.add_argument("--out", required=True)
    args = p.parse_args()

    loop_cols = [
        "deadline_ns",
        "tick_class",
        "ekf_pn",
        "ekf_pe",
        "ekf_pd",
        "ekf_vn",
        "ekf_ve",
        "ekf_vd",
        "ekf_trace_p",
    ]
    truth_cols = ["t_ns", "px", "py", "pz", "vx", "vy", "vz"]

    loop_meta, loop = load_meta_rows(args.loop_csv, loop_cols)
    truth_meta, truth = load_meta_rows(args.truth_csv, truth_cols)

    if loop["deadline_ns"].size == 0:
        print("empty loop csv", file=sys.stderr)
        return 1
    if truth["t_ns"].size == 0:
        print("empty truth csv", file=sys.stderr)
        return 1
    if "ekf_pn" not in loop or loop["ekf_pn"].size == 0:
        print("loop csv has no ekf_* columns; re-run with --ekf", file=sys.stderr)
        return 1

    ctrl = (loop["tick_class"].astype(np.int64) & TICK_CTRL) != 0
    # Non-control rows leave ekf_* at 0; do not treat those as estimates.
    t_est = loop["deadline_ns"][ctrl]
    if t_est.size == 0:
        print("no TICK_CTRL rows with EKF snapshot", file=sys.stderr)
        return 1

    pn = loop["ekf_pn"][ctrl]
    pe = loop["ekf_pe"][ctrl]
    pd = loop["ekf_pd"][ctrl]
    vn = loop["ekf_vn"][ctrl]
    ve = loop["ekf_ve"][ctrl]
    vd = loop["ekf_vd"][ctrl]
    tr = loop["ekf_trace_p"][ctrl]

    t_truth = truth["t_ns"]
    # Nearest-neighbour in time; both clocks are rt::now_ns() on one host.
    idx = np.searchsorted(t_truth, t_est, side="left")
    idx = np.clip(idx, 0, t_truth.size - 1)
    # Prefer the closer of idx and idx-1 when both exist.
    prev = np.clip(idx - 1, 0, t_truth.size - 1)
    use_prev = np.abs(t_truth[prev] - t_est) < np.abs(t_truth[idx] - t_est)
    idx = np.where(use_prev, prev, idx)

    err_p = np.column_stack(
        [
            pn - truth["px"][idx],
            pe - truth["py"][idx],
            pd - truth["pz"][idx],
        ]
    )
    err_v = np.column_stack(
        [
            vn - truth["vx"][idx],
            ve - truth["vy"][idx],
            vd - truth["vz"][idx],
        ]
    )

    n = err_p.shape[0]
    warn_tail(args.loop_csv, n, "p99.99")
    if loop_meta.get("ekf", "0") == "0":
        print(
            f"{args.loop_csv}: ekf=0 in header; numbers may be all-zero stubs",
            file=sys.stderr,
        )

    lines = [
        f"# loop={args.loop_csv}",
        f"# truth={args.truth_csv}",
        f"# samples={n}",
        f"# ekf={loop_meta.get('ekf', '')}",
        f"# label={loop_meta.get('label', '')}",
        f"# truth_setpoint_rx={truth_meta.get('setpoint_rx', '')}",
        "",
        "| quantity | axis | n | p50 | p95 | max |",
        "|---|---|---|---|---|---|",
    ]
    for i, ax in enumerate("NED"):
        s = axis_stats(np.abs(err_p[:, i]))
        lines.append(
            f"| pos_err_m | {ax} | {s['n']} | {fmt(s['p50'])} | "
            f"{fmt(s['p95'])} | {fmt(s['max'])} |"
        )
    for i, ax in enumerate("NED"):
        s = axis_stats(np.abs(err_v[:, i]))
        lines.append(
            f"| vel_err_mps | {ax} | {s['n']} | {fmt(s['p50'])} | "
            f"{fmt(s['p95'])} | {fmt(s['max'])} |"
        )
    tr_s = axis_stats(tr)
    lines.append(
        f"| trace_P | - | {tr_s['n']} | {fmt(tr_s['p50'])} | "
        f"{fmt(tr_s['p95'])} | {fmt(tr_s['max'])} |"
    )
    text = "\n".join(lines) + "\n"
    sys.stdout.write(text)
    with open(args.out, "w") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
