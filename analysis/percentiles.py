#!/usr/bin/env python3
"""Percentile table from loop_bench CSVs. Skips /^#/; header length is not fixed."""

import argparse
import sys

import numpy as np


def load_wake_us(path):
    vals = []
    with open(path) as f:
        for line in f:
            if line.startswith("#") or line.startswith("seq"):
                continue
            parts = line.split(",")
            if len(parts) < 3:
                continue
            vals.append(float(parts[2]) / 1000.0)
    return np.asarray(vals, dtype=np.float64)


def pct(a, q, n_above_needed=10):
    n = a.size
    n_above = int(n * (1.0 - q))
    if n_above < n_above_needed:
        return None
    return float(np.quantile(a, q))


def fmt(x):
    return "n/a" if x is None else f"{x:.0f}"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csvs", nargs="+")
    p.add_argument("--out", required=True)
    args = p.parse_args()

    rows = []
    for path in args.csvs:
        a = load_wake_us(path)
        n = a.size
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
        rows.append(
            (path, n, float(np.mean(a)), p50, p99, p999, p9999, float(np.max(a)))
        )

    lines = [
        "| file | n | mean | p50 | p99 | p99.9 | p99.99 | max |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for path, n, mean, p50, p99, p999, p9999, mx in rows:
        lines.append(
            f"| `{path}` | {n} | {mean:.0f} | {fmt(p50)} | {fmt(p99)} | "
            f"{fmt(p999)} | {fmt(p9999)} | {mx:.0f} |"
        )
    text = "\n".join(lines) + "\n"
    sys.stdout.write(text)
    with open(args.out, "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()
