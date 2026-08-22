#!/usr/bin/env python3
"""Percentile table from loop_bench CSVs. Metadata from the # block, not the path."""

import argparse
import gzip
import sys

import numpy as np

FLAG_STALE = 1 << 3
AGE_NONE = np.iinfo(np.int64).min


TICK_CTRL = 1 << 0
TICK_TELEM = 1 << 1


def parse_csv(path):
    meta = {}
    wake = []
    exec_us = []
    rx_us = []
    ctrl_us = []
    age_imu_us = []
    age_pos_us = []
    age_gps_us = []
    flags = []
    tick_class = []
    header = None
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt") as f:
        for line in f:
            if line.startswith("#"):
                body = line[1:].strip()
                if "=" in body:
                    key, _, val = body.partition("=")
                    meta[key.strip()] = val.strip()
                continue
            parts = [p.strip() for p in line.split(",")]
            if header is None:
                header = parts
                continue
            if len(parts) < 3:
                continue
            rec = {header[i]: parts[i] for i in range(min(len(header), len(parts)))}
            wake.append(float(rec.get("wake_err_ns", parts[2])) / 1000.0)
            if "exec_ns" in rec:
                exec_us.append(float(rec["exec_ns"]) / 1000.0)
            elif len(parts) >= 4:
                exec_us.append(float(parts[3]) / 1000.0)
            if "rx_ns" in rec:
                rx_us.append(float(rec["rx_ns"]) / 1000.0)
            tc = int(rec["tick_class"]) if "tick_class" in rec else 0
            tick_class.append(tc)
            # ctrl_ns is 0 on non-control ticks; drop those so plumbing cost is not diluted.
            if "ctrl_ns" in rec:
                cns = int(float(rec["ctrl_ns"]))
                if "tick_class" not in rec:
                    if cns != 0:
                        ctrl_us.append(cns / 1000.0)
                elif (tc & TICK_CTRL) != 0:
                    ctrl_us.append(cns / 1000.0)
            imu_key = "age_imu_ns" if "age_imu_ns" in rec else "age_ns" if "age_ns" in rec else None
            if imu_key is not None:
                v = int(rec[imu_key])
                if v != AGE_NONE:
                    age_imu_us.append(v / 1000.0)
            if "age_pos_ns" in rec:
                v = int(rec["age_pos_ns"])
                if v != AGE_NONE:
                    age_pos_us.append(v / 1000.0)
            if "age_gps_ns" in rec:
                v = int(rec["age_gps_ns"])
                if v != AGE_NONE:
                    age_gps_us.append(v / 1000.0)
            if "flags" in rec:
                flags.append(int(rec["flags"]))
    return (
        meta,
        np.asarray(wake, dtype=np.float64),
        np.asarray(exec_us, dtype=np.float64),
        np.asarray(rx_us, dtype=np.float64),
        np.asarray(age_imu_us, dtype=np.float64),
        np.asarray(flags, dtype=np.int64),
        np.asarray(age_pos_us, dtype=np.float64),
        np.asarray(age_gps_us, dtype=np.float64),
        np.asarray(ctrl_us, dtype=np.float64),
        np.asarray(tick_class, dtype=np.int64),
    )


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
    p99 = pct(exec_us, 0.99)
    if p99 is None:
        return
    frac = (p99 * 1000.0) / comp_ns
    if frac > 0.80:
        print(
            f"{path}: exec p99 {p99:.0f} µs is {frac:.0%} of "
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


def fmt1(x):
    return "n/a" if x is None else f"{x:.1f}"


def dist_stats(a):
    n = a.size
    if n == 0:
        return {
            "n": 0,
            "mean": float("nan"),
            "p50": None,
            "p99": None,
            "p999": None,
            "p9999": None,
            "max": float("nan"),
        }
    return {
        "n": n,
        "mean": float(np.mean(a)),
        "p50": pct(a, 0.50),
        "p99": pct(a, 0.99),
        "p999": pct(a, 0.999),
        "p9999": pct(a, 0.9999) if n >= 100_000 else None,
        "max": float(np.max(a)),
    }


def warn_tail(path, label, stats):
    n = stats["n"]
    if n == 0:
        return
    if n < 100_000:
        print(
            f"{path}: {label} n={n}, p99.99 needs ~100000 samples "
            f"({100000 / 250:.0f}s at 250 Hz)",
            file=sys.stderr,
        )
    if stats["p999"] is None:
        print(
            f"{path}: {label} n={n}, p99.9 n/a (need ~10 samples above it)",
            file=sys.stderr,
        )


def meta_int(meta, key, default=0):
    raw = meta.get(key)
    if raw is None:
        return default
    try:
        return int(raw)
    except ValueError:
        return default


def wake_by_tick_class(wake_us, tick_class):
    """Split wake so control ticks are not diluted by the 4/5 plain ticks."""
    if tick_class.size == 0 or tick_class.size != wake_us.size:
        return None
    if not np.any(tick_class):
        return None
    plain = wake_us[tick_class == 0]
    ctrl = wake_us[(tick_class & TICK_CTRL) != 0]
    telem = wake_us[(tick_class & TICK_TELEM) != 0]
    return {
        "wake_plain": dist_stats(plain),
        "wake_ctrl": dist_stats(ctrl),
        "wake_telem": dist_stats(telem),
    }


def summarize(path):
    (
        meta,
        a,
        exec_us,
        rx_us,
        age_imu_us,
        flags,
        age_pos_us,
        age_gps_us,
        ctrl_us,
        tick_class,
    ) = parse_csv(path)
    n = a.size
    name = display_name(meta, path)
    warn_if_denied(path, meta, name)
    warn_if_computation(path, meta, exec_us)
    wake = dist_stats(a)
    exec_d = dist_stats(exec_us)
    rx_d = dist_stats(rx_us)
    age_imu_d = dist_stats(age_imu_us)
    age_pos_d = dist_stats(age_pos_us)
    age_gps_d = dist_stats(age_gps_us)
    ctrl_d = dist_stats(ctrl_us)
    by_class = wake_by_tick_class(a, tick_class)
    warn_tail(path, "wake_err_ns", wake)
    if exec_d["n"] != wake["n"]:
        warn_tail(path, "exec_ns", exec_d)
    if rx_d["n"] > 0 and rx_d["n"] != wake["n"]:
        warn_tail(path, "rx_ns", rx_d)
    if age_imu_d["n"] > 0 and age_imu_d["n"] != wake["n"]:
        warn_tail(path, "age_imu_ns", age_imu_d)
    if age_pos_d["n"] > 0 and age_pos_d["n"] != wake["n"]:
        warn_tail(path, "age_pos_ns", age_pos_d)
    if age_gps_d["n"] > 0 and age_gps_d["n"] != wake["n"]:
        warn_tail(path, "age_gps_ns", age_gps_d)
    if ctrl_d["n"] > 0:
        warn_tail(path, "ctrl_ns", ctrl_d)
    stale_n = int(np.count_nonzero(flags & FLAG_STALE)) if flags.size else 0
    out = {
        "path": path,
        "name": name,
        "n": n,
        "mean": wake["mean"],
        "p50": wake["p50"],
        "p99": wake["p99"],
        "p999": wake["p999"],
        "p9999": wake["p9999"],
        "max": wake["max"],
        "exec_p50": exec_d["p50"] if exec_d["n"] else float("nan"),
        "exec_p99": exec_d["p99"],
        "exec_max": exec_d["max"],
        "wake": wake,
        "exec": exec_d,
        "rx": rx_d,
        "age": age_imu_d,
        "age_imu": age_imu_d,
        "age_pos": age_pos_d,
        "age_gps": age_gps_d,
        "ctrl": ctrl_d,
        "stale_n": stale_n,
        "stale_imu": meta_int(meta, "stale_imu", stale_n),
        "stale_pos": meta_int(meta, "stale_pos"),
        "stale_gps": meta_int(meta, "stale_gps"),
        "wake_us": a,
        "meta": meta,
    }
    if by_class is not None:
        out.update(by_class)
    return out


def md_dist_table(rows, key, prec):
    f = fmt if prec == 0 else fmt1
    lines = [
        "| configuration | n | mean | p50 | p99 | p99.9 | p99.99 | max |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for r in rows:
        d = r[key]
        if d["n"] == 0:
            lines.append(f"| {r['name']} | 0 | n/a | n/a | n/a | n/a | n/a | n/a |")
            continue
        lines.append(
            f"| {r['name']} | {d['n']} | {d['mean']:.{prec}f} | {f(d['p50'])} | "
            f"{f(d['p99'])} | {f(d['p999'])} | {f(d['p9999'])} | {d['max']:.{prec}f} |"
        )
    return lines


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csvs", nargs="+")
    p.add_argument("--out", required=True)
    args = p.parse_args()

    rows = [summarize(path) for path in args.csvs]

    lines = md_dist_table(rows, "wake", 0)
    lines += ["", "exec_ns, microseconds. Same files.", ""]
    lines += md_dist_table(rows, "exec", 1)
    lines += ["", "rx_ns, microseconds. Drain+parse+slot in-run. Same files.", ""]
    lines += md_dist_table(rows, "rx", 1)
    lines += ["", "age_imu_ns, microseconds. kAgeNone rows dropped. Same files.", ""]
    lines += md_dist_table(rows, "age_imu", 1)
    lines += ["", "age_pos_ns, microseconds. kAgeNone rows dropped. Same files.", ""]
    lines += md_dist_table(rows, "age_pos", 1)
    lines += ["", "age_gps_ns, microseconds. kAgeNone rows dropped. Same files.", ""]
    lines += md_dist_table(rows, "age_gps", 1)
    lines += ["", "ctrl_ns, microseconds. Non-control ticks (ctrl_ns==0) dropped. Same files.", ""]
    lines += md_dist_table(rows, "ctrl", 1)
    classed = [r for r in rows if "wake_ctrl" in r]
    if classed:
        lines += [
            "",
            "wake_err_ns by tick_class, microseconds. plain / ctrl / telem — "
            "asks whether the heavier control tick pushes the following deadline.",
            "",
        ]
        for key, label in (
            ("wake_plain", "plain (tick_class==0)"),
            ("wake_ctrl", "ctrl (TICK_CTRL)"),
            ("wake_telem", "telem (TICK_TELEM)"),
        ):
            lines.append(f"{label}:")
            lines += md_dist_table(
                [{"name": r["name"], key: r[key]} for r in classed], key, 0
            )
            lines.append("")
    text = "\n".join(lines) + "\n"
    sys.stdout.write(text)
    with open(args.out, "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()
