#!/usr/bin/env python3
"""Percentile table from loop_bench CSVs. Metadata from the # block, not the path."""

import argparse
import gzip
import sys

import numpy as np


def parse_csv(path):
    meta = {}
    wake = []
    exec_us = []
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt") as f:
        for line in f:
            if line.startswith("#"):
                body = line[1:].strip()
                if "=" in body:
                    key, _, val = body.partition("=")
                    meta[key.strip()] = val.strip()
                continue
            if line.startswith("seq"):
                continue
            parts = line.split(",")
            if len(parts) < 3:
                continue
            wake.append(float(parts[2]) / 1000.0)
            if len(parts) >= 4:
                exec_us.append(float(parts[3]) / 1000.0)
    return meta, np.asarray(wake, dtype=np.float64), np.asarray(exec_us, dtype=np.float64)


def display_name(meta, path):
    label = meta.get("label", "").strip()
    applied = meta.get("scheduler_applied", "")
    if applied == "0":
        return f"best effort ({label})" if label else "best effort"
    if label:
        return label
    return path


def policy_denied(meta):
    return meta.get("scheduler_requested") == "1" and meta.get("scheduler_applied") == "0"


def warn_if_denied(path, meta, name):
    if policy_denied(meta):
        print(
            f"{path}: scheduler requested but not applied, labeling {name!r}",
            file=sys.stderr,
        )


def warn_if_computation(path, meta, exec_us):
    raw = meta.get("computation_ns", "0")
    try:
        comp_ns = int(raw)
    except ValueError:
        return
    if comp_ns <= 0 or exec_us.size == 0:
        return
    p50_ns = float(np.quantile(exec_us, 0.50)) * 1000.0
    frac = p50_ns / comp_ns
    if frac > 0.80:
        print(
            f"{path}: exec p50 {p50_ns / 1000.0:.0f} µs is {frac:.0%} of "
            f"computation_ns={comp_ns}; macOS can demote the thread",
            file=sys.stderr,
        )


def pct(a, q, n_above_needed=10):
    n = a.size
    n_above = round(n * (1.0 - q))
    if n_above < n_above_needed:
        return None
    return float(np.quantile(a, q))


def fmt(x):
    return "n/a" if x is None else f"{x:.0f}"


def summarize(path):
    meta, a, exec_us = parse_csv(path)
    n = a.size
    name = display_name(meta, path)
    warn_if_denied(path, meta, name)
    warn_if_computation(path, meta, exec_us)
    p50 = pct(a, 0.50)
    p99 = pct(a, 0.99)
    p999 = pct(a, 0.999)
    p9999 = pct(a, 0.9999) if n >= 100_000 else None
    if n < 100_000:
        print(
            f"{path}: n={n}, p99.99 needs ~100000 samples "
            f"({100000 / 250:.0f}s at 250 Hz)",
            file=sys.stderr,
        )
    if p999 is None:
        print(f"{path}: n={n}, p99.9 n/a (need ~10 samples above it)", file=sys.stderr)
    return {
        "path": path,
        "name": name,
        "n": n,
        "mean": float(np.mean(a)) if n else float("nan"),
        "p50": p50,
        "p99": p99,
        "p999": p999,
        "p9999": p9999,
        "max": float(np.max(a)) if n else float("nan"),
        "wake_us": a,
        "meta": meta,
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csvs", nargs="+")
    p.add_argument("--out", required=True)
    args = p.parse_args()

    rows = [summarize(path) for path in args.csvs]

    lines = [
        "| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for r in rows:
        lines.append(
            f"| {r['name']} | {r['n']} | {r['mean']:.0f} | {fmt(r['p50'])} | "
            f"{fmt(r['p99'])} | {fmt(r['p999'])} | {fmt(r['p9999'])} | {r['max']:.0f} |"
        )
    text = "\n".join(lines) + "\n"
    sys.stdout.write(text)
    with open(args.out, "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()
