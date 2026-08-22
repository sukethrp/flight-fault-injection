#!/usr/bin/env python3
"""False-positive rate from a clean (passthrough) loop CSV.

A one-hour clean run with the injector in passthrough must produce zero
detector fires. Rate is rising-edge count / sample count (dimensionless).
A detector with a good TTD and a 2% false-positive rate is not a good detector.
"""

from __future__ import annotations

import argparse
import gzip
import sys

import numpy as np

DET_NAMES = [
    (1 << 0, "stale_imu"),
    (1 << 1, "stale_pos"),
    (1 << 2, "stale_gps"),
    (1 << 3, "seq_gap"),
    (1 << 4, "clock_skew"),
    (1 << 5, "deadline_miss"),
    (1 << 6, "stuck_sensor"),
    (1 << 7, "est_diverge"),
]


def open_text(path: str):
    return gzip.open(path, "rt") if path.endswith(".gz") else open(path, "rt")


def load_det_mask(path: str):
    meta = {}
    masks = []
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
            rec = {header[i]: parts[i] for i in range(min(len(header), len(parts)))}
            masks.append(int(rec.get("det_mask", 0)))
    return meta, np.asarray(masks, dtype=np.uint32)


def rising_edges(mask: np.ndarray, bit: int) -> int:
    if mask.size == 0:
        return 0
    on = (mask & bit) != 0
    prev = np.concatenate(([False], on[:-1]))
    return int(np.count_nonzero(on & ~prev))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("loop_csv", help="clean / passthrough loop_bench CSV")
    p.add_argument("--out", required=True)
    args = p.parse_args()

    meta, masks = load_det_mask(args.loop_csv)
    n = int(masks.size)
    if n == 0:
        print("empty loop CSV", file=sys.stderr)
        return 1

    lines = [
        f"# false_positive_rate samples={n}",
        f"# label={meta.get('label', '')}",
        f"# scheduler_applied={meta.get('scheduler_applied', '')}",
        "| Detector | Rising edges | Rate | 95% upper (rule of 3) |",
        "|---|---|---|---|",
    ]
    total_rising = 0
    for bit, name in DET_NAMES:
        r = rising_edges(masks, bit)
        total_rising += r
        rate = r / n
        # Zero events: 95% CL upper bound ≈ 3/n (rule of three), not "0".
        upper = (3.0 / n) if r == 0 else ""
        upper_s = f"{upper:.6e}" if upper != "" else "n/a"
        lines.append(f"| {name} | {r} | {rate:.6e} | {upper_s} |")

    any_on = int(np.count_nonzero(masks != 0))
    overall = total_rising / n
    all_upper = f"{3.0 / n:.6e}" if total_rising == 0 else "n/a"
    lines.append(
        f"| **all (rising sum)** | {total_rising} | {overall:.6e} | {all_upper} |"
    )
    lines.append(f"| ticks_with_any_det | {any_on} | {any_on / n:.6e} |  |")
    lines.append("")
    lines.append(f"n={n}")
    lines.append(f"false_positive_rate={overall:.6e}")
    if total_rising == 0:
        lines.append(f"false_positive_rate_95cl_upper={3.0 / n:.6e}")
    else:
        print(
            f"warn: clean run produced {total_rising} rising detector edges "
            f"(rate={overall:.6e}, n={n}); passthrough target is zero",
            file=sys.stderr,
        )

    text = "\n".join(lines) + "\n"
    with open(args.out, "w") as f:
        f.write(text)
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
