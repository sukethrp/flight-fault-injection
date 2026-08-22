#!/usr/bin/env python3
"""Fault table: TTD / TTR from injector events + loop CSV.

TTD = detector-fire mono − inject_mono_ns.
TTR = recovery mono − detector-fire.
Recovery is defined once below: FSM back to NOMINAL, or stable in DEGRADED,
with estimator error under threshold for the hold window. Never report a mean;
p50 / p95 / max only. Floor column is the detector's theoretical floor from
the loop CSV # metadata, not a measured TTD.
"""

from __future__ import annotations

import argparse
import gzip
import sys

import numpy as np

# FailsafeState ordinals — keep in lockstep with src/failsafe.h.
FSM_NOMINAL = 0
FSM_DEGRADED = 1
FSM_SAFE = 2
FSM_LOCKED = 3

# DET_* bits — keep in lockstep with src/detectors.h.
DET_STALE_IMU = 1 << 0
DET_STALE_POS = 1 << 1
DET_STALE_GPS = 1 << 2
DET_SEQ_GAP = 1 << 3
DET_CLOCK_SKEW = 1 << 4
DET_DEADLINE_MISS = 1 << 5
DET_STUCK_SENSOR = 1 << 6
DET_EST_DIVERGE = 1 << 7

DET_NAMES = {
    DET_STALE_IMU: "stale_imu",
    DET_STALE_POS: "stale_pos",
    DET_STALE_GPS: "stale_gps",
    DET_SEQ_GAP: "seq_gap",
    DET_CLOCK_SKEW: "clock_skew",
    DET_DEADLINE_MISS: "deadline_miss",
    DET_STUCK_SENSOR: "stuck_sensor",
    DET_EST_DIVERGE: "est_diverge",
}

# fault_id / type substring → primary detector bit used for TTD.
FAULT_TO_DET = {
    "gps_dropout": DET_STALE_GPS,
    "packet_drop": DET_SEQ_GAP,
    "packet_delay": DET_STALE_IMU,
    "reorder": DET_SEQ_GAP,
    "imu_stuck": DET_STUCK_SENSOR,
    "clock_skew": DET_CLOCK_SKEW,
    "imu_bitflip": DET_EST_DIVERGE,
    "imu_bias": DET_EST_DIVERGE,
    "gps_jump": DET_EST_DIVERGE,
    "deadline": DET_DEADLINE_MISS,
    "stale_imu": DET_STALE_IMU,
    "stale_pos": DET_STALE_POS,
    "stale_gps": DET_STALE_GPS,
}

FLOOR_META = {
    DET_STALE_IMU: "staleness_floor_imu_ns",
    DET_STALE_POS: "staleness_floor_pos_ns",
    DET_STALE_GPS: "staleness_floor_gps_ns",
    DET_SEQ_GAP: "floor_seq_gap_ns",
    DET_CLOCK_SKEW: "floor_clock_skew_ns",
    DET_DEADLINE_MISS: "floor_deadline_miss_ns",
    DET_STUCK_SENSOR: "floor_stuck_sensor_ns",
    DET_EST_DIVERGE: "floor_est_diverge_ns",
}


def open_text(path: str):
    return gzip.open(path, "rt") if path.endswith(".gz") else open(path, "rt")


def parse_meta_csv(path: str):
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


def mono_of(row: dict) -> int:
    # woke ≈ deadline + wake_err; both share the injector epoch.
    return int(row["deadline_ns"]) + int(row["wake_err_ns"])


def recovery_reached(
    fsm_state: np.ndarray,
    est_err_m: np.ndarray,
    hold_ticks: int,
    err_limit_m: float,
) -> np.ndarray:
    """True on the first index where recovery holds, else -1 sentinel via search.

    Recovery (single definition): FSM is NOMINAL, or FSM is DEGRADED and has
    been DEGRADED for the whole hold window, AND est_err_m <= err_limit_m for
    the same hold window. SAFE/LOCKED never count as recovered.
    """
    n = len(fsm_state)
    ok = np.zeros(n, dtype=bool)
    if n == 0 or hold_ticks < 1:
        return ok
    under = est_err_m <= err_limit_m
    for i in range(hold_ticks - 1, n):
        window = slice(i - hold_ticks + 1, i + 1)
        fsm_w = fsm_state[window]
        err_w = under[window]
        if not np.all(err_w):
            continue
        if np.all(fsm_w == FSM_NOMINAL):
            ok[i] = True
        elif np.all(fsm_w == FSM_DEGRADED):
            ok[i] = True
    return ok


def percentile_us(samples_ns, q):
    if len(samples_ns) == 0:
        return None
    return float(np.percentile(samples_ns, q) / 1000.0)


def resolve_detector(fault_id: str) -> int | None:
    fid = fault_id.lower()
    if fid in FAULT_TO_DET:
        return FAULT_TO_DET[fid]
    for key, bit in FAULT_TO_DET.items():
        if key in fid:
            return bit
    return None


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--events", required=True, help="injector event CSV")
    p.add_argument("--loop", required=True, help="loop_bench CSV")
    p.add_argument("--out", required=True)
    p.add_argument("--hold-ticks", type=int, default=25, help="recovery hold window")
    p.add_argument("--err-limit-m", type=float, default=0.5)
    args = p.parse_args()

    _ev_meta, events = parse_meta_csv(args.events)
    loop_meta, loop_rows = parse_meta_csv(args.loop)

    if not loop_rows:
        print("empty loop CSV", file=sys.stderr)
        return 1

    monos = np.asarray([mono_of(r) for r in loop_rows], dtype=np.int64)
    det_mask = np.asarray([int(r.get("det_mask", 0)) for r in loop_rows], dtype=np.uint32)
    fsm = np.asarray([int(r.get("fsm_state", 0)) for r in loop_rows], dtype=np.int32)
    est = []
    for r in loop_rows:
        v = int(float(r.get("est_err_um", "-2147483648")))
        est.append(np.nan if v == -2147483648 else v * 1e-6)
    est_err = np.asarray(est, dtype=np.float64)
    # missing residual: treat as 0 so FSM-only recovery still works on clean stubs
    est_err = np.where(np.isnan(est_err), 0.0, est_err)

    recovered = recovery_reached(fsm, est_err, args.hold_ticks, args.err_limit_m)

    # per fault_id accumulate TTD/TTR
    buckets: dict[str, dict] = {}

    for e in events:
        inject = int(e["inject_mono_ns"])
        fid = e["fault_id"]
        bit = resolve_detector(fid)
        if bit is None:
            print(f"warn: no detector mapping for fault_id={fid}", file=sys.stderr)
            continue
        # first tick at/after inject where bit is set
        after = np.flatnonzero((monos >= inject) & ((det_mask & bit) != 0))
        if after.size == 0:
            continue
        fire_i = int(after[0])
        fire_ns = int(monos[fire_i])
        ttd = fire_ns - inject
        # recovery strictly after fire
        rec = np.flatnonzero(recovered)
        rec = rec[rec > fire_i]
        ttr = None
        if rec.size:
            ttr = int(monos[int(rec[0])]) - fire_ns

        b = buckets.setdefault(
            fid,
            {"bit": bit, "ttd": [], "ttr": [], "end": []},
        )
        b["ttd"].append(ttd)
        if ttr is not None:
            b["ttr"].append(ttr)
        b["end"].append(int(fsm[fire_i]))

    lines = [
        "| Fault | Detector | TTD p50 (µs) | TTD p95 (µs) | TTD max (µs) | Floor (µs) | TTR p50 (µs) | TTR p95 (µs) | TTR max (µs) | n |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]

    for fid in sorted(buckets):
        b = buckets[fid]
        bit = b["bit"]
        name = DET_NAMES.get(bit, hex(bit))
        floor_key = FLOOR_META.get(bit)
        floor_ns = int(loop_meta[floor_key]) if floor_key and floor_key in loop_meta else None
        ttd = np.asarray(b["ttd"], dtype=np.float64)
        ttr = np.asarray(b["ttr"], dtype=np.float64) if b["ttr"] else np.asarray([])

        def fmt(v):
            return "n/a" if v is None else f"{v:.1f}"

        floor_us = None if floor_ns is None else floor_ns / 1000.0
        # Measured TTD below the detector's theoretical floor is physically
        # impossible; that is a join/fixture bug, not a fast detector.
        if floor_ns is not None and ttd.size:
            under = ttd[ttd < floor_ns]
            if under.size:
                raise SystemExit(
                    f"TTD below floor for fault_id={fid} detector={name}: "
                    f"min_ttd_ns={int(under.min())} floor_ns={floor_ns} "
                    f"(n_under={under.size}/{ttd.size})"
                )
        lines.append(
            "| {fid} | {det} | {p50} | {p95} | {mx} | {fl} | {tp50} | {tp95} | {tmx} | {n} |".format(
                fid=fid,
                det=name,
                p50=fmt(percentile_us(ttd, 50)),
                p95=fmt(percentile_us(ttd, 95)),
                mx=fmt(percentile_us(ttd, 100)),
                fl=fmt(floor_us),
                tp50=fmt(percentile_us(ttr, 50)),
                tp95=fmt(percentile_us(ttr, 95)),
                tmx=fmt(percentile_us(ttr, 100)),
                n=len(ttd),
            )
        )

    if len(buckets) == 0:
        lines.append("| _(no matched injections)_ |  |  |  |  |  |  |  |  | 0 |")

    text = "\n".join(lines) + "\n"
    with open(args.out, "w") as f:
        f.write(text)
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
