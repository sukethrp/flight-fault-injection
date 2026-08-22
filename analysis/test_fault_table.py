#!/usr/bin/env python3
"""Synthetic TTD/TTR and false-positive fixtures for analysis scripts."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAULT_TABLE = os.path.join(ROOT, "analysis", "fault_table.py")
FP_RATE = os.path.join(ROOT, "analysis", "false_positive_rate.py")

DET_STALE_GPS = 1 << 2
FSM_NOMINAL = 0
FSM_DEGRADED = 1
FSM_SAFE = 2

PERIOD = 4_000_000
FLOOR_GPS = 600_000_000


def write_events(path, rows):
    with open(path, "w") as f:
        f.write("# seed=1\n")
        f.write("inject_mono_ns,fault_id,param_json,target_msgid,action,seq\n")
        for r in rows:
            f.write(
                f"{r['inject_mono_ns']},{r['fault_id']},{r.get('param_json', '{}')},"
                f"{r.get('target_msgid', 24)},{r.get('action', 'Drop')},{r['seq']}\n"
            )


def write_loop(path, n, inject_ns, fire_delay_ticks, recover_after_ticks):
    """n samples starting at t0=1e12. GPS stale bit rises fire_delay_ticks after inject."""
    t0 = 1_000_000_000_000
    with open(path, "w") as f:
        f.write("# hz=250\n")
        f.write(f"# period_ns={PERIOD}\n")
        f.write(f"# staleness_floor_gps_ns={FLOOR_GPS}\n")
        f.write(f"# floor_seq_gap_ns={PERIOD}\n")
        f.write(f"# floor_clock_skew_ns={64 * PERIOD}\n")
        f.write(f"# floor_deadline_miss_ns={3 * PERIOD}\n")
        f.write(f"# floor_stuck_sensor_ns={25 * PERIOD}\n")
        f.write(f"# floor_est_diverge_ns={PERIOD}\n")
        f.write(
            "seq,deadline_ns,wake_err_ns,exec_ns,rx_ns,ekf_ns,ctrl_ns,flags,"
            "rx_count,seq_gaps,skew_ns,age_imu_ns,age_pos_ns,age_gps_ns,tick_class,"
            "ekf_pn,ekf_pe,ekf_pd,ekf_vn,ekf_ve,ekf_vd,ekf_trace_p,ekf_nis,ekf_rejects,"
            "det_mask,fsm_state,est_err_um\n"
        )
        fire_i = None
        for i in range(n):
            deadline = t0 + i * PERIOD
            wake_err = 0
            mono = deadline + wake_err
            det = 0
            fsm = FSM_NOMINAL
            err_um = 0
            if mono >= inject_ns:
                ticks_since = (mono - inject_ns) // PERIOD
                if ticks_since >= fire_delay_ticks:
                    det = DET_STALE_GPS
                    if fire_i is None:
                        fire_i = i
                    # stay SAFE until recover_after_ticks past fire, then NOMINAL
                    if fire_i is not None and (i - fire_i) < recover_after_ticks:
                        fsm = FSM_SAFE
                        err_um = 2_000_000  # 2 m
                    else:
                        fsm = FSM_NOMINAL
                        err_um = 100_000  # 0.1 m
            f.write(
                f"{i},{deadline},{wake_err},500000,0,0,0,0,0,0,-2147483648,"
                f"0,0,0,0,0,0,0,0,0,0,1,0,0,{det},{fsm},{err_um}\n"
            )


class FaultTableTests(unittest.TestCase):
    def test_ttd_ttr_and_floor(self):
        with tempfile.TemporaryDirectory() as td:
            events = os.path.join(td, "events.csv")
            loop = os.path.join(td, "loop.csv")
            out = os.path.join(td, "table.md")
            inject = 1_000_000_000_000 + 10 * PERIOD
            # GPS floor is 600 ms = 150 periods; fire earlier is physically
            # impossible against staleness_limit_periods=3 at 5 Hz.
            fire_delay = FLOOR_GPS // PERIOD
            recover_after = 30  # > hold_ticks default 25
            write_events(
                events,
                [{"inject_mono_ns": inject, "fault_id": "gps_dropout", "seq": 0}],
            )
            # inject @ tick 10, fire @ 160, recover hold past fire → need ~220 rows
            write_loop(loop, 250, inject, fire_delay, recover_after)
            r = subprocess.run(
                [
                    sys.executable,
                    FAULT_TABLE,
                    "--events",
                    events,
                    "--loop",
                    loop,
                    "--out",
                    out,
                    "--hold-ticks",
                    "25",
                    "--err-limit-m",
                    "0.5",
                ],
                capture_output=True,
                text=True,
                check=True,
            )
            with open(out) as fh:
                body = fh.read()
            self.assertIn("gps_dropout", body)
            self.assertIn("stale_gps", body)
            # TTD = floor = 600000 µs (fixture timestamps were the bug, not the join)
            self.assertIn("600000.0", body)
            self.assertNotRegex(body, r"(?i)\|\s*mean\s*\|")
            self.assertIn("| n |", body.splitlines()[0])
            self.assertTrue(r.stdout)

    def test_ttd_below_floor_raises(self):
        with tempfile.TemporaryDirectory() as td:
            events = os.path.join(td, "events.csv")
            loop = os.path.join(td, "loop.csv")
            out = os.path.join(td, "table.md")
            inject = 1_000_000_000_000 + 10 * PERIOD
            write_events(
                events,
                [{"inject_mono_ns": inject, "fault_id": "gps_dropout", "seq": 0}],
            )
            write_loop(loop, 80, inject, fire_delay_ticks=3, recover_after_ticks=30)
            r = subprocess.run(
                [
                    sys.executable,
                    FAULT_TABLE,
                    "--events",
                    events,
                    "--loop",
                    loop,
                    "--out",
                    out,
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("TTD below floor", r.stderr)


class FalsePositiveTests(unittest.TestCase):
    def test_clean_zero(self):
        with tempfile.TemporaryDirectory() as td:
            loop = os.path.join(td, "clean.csv")
            out = os.path.join(td, "fp.md")
            with open(loop, "w") as f:
                f.write("# label=passthrough\n")
                f.write("# scheduler_applied=1\n")
                f.write(
                    "seq,deadline_ns,wake_err_ns,det_mask,fsm_state,est_err_um\n"
                )
                for i in range(1000):
                    f.write(f"{i},{i * PERIOD},0,0,0,0\n")
            r = subprocess.run(
                [sys.executable, FP_RATE, loop, "--out", out],
                capture_output=True,
                text=True,
                check=True,
            )
            with open(out) as fh:
                body = fh.read()
            self.assertIn("n=1000", body)
            self.assertIn("false_positive_rate=0.000000e+00", body)
            self.assertIn("false_positive_rate_95cl_upper=3.000000e-03", body)
            self.assertEqual(r.returncode, 0)

    def test_nonzero_rate(self):
        with tempfile.TemporaryDirectory() as td:
            loop = os.path.join(td, "dirty.csv")
            out = os.path.join(td, "fp.md")
            with open(loop, "w") as f:
                f.write("# label=dirty\n")
                f.write("seq,deadline_ns,wake_err_ns,det_mask,fsm_state,est_err_um\n")
                for i in range(100):
                    # one rising edge of seq_gap at i=50
                    mask = DET_STALE_GPS if i >= 50 else 0
                    f.write(f"{i},{i * PERIOD},0,{mask},0,0\n")
            subprocess.run(
                [sys.executable, FP_RATE, loop, "--out", out],
                capture_output=True,
                text=True,
                check=True,
            )
            with open(out) as fh:
                body = fh.read()
            self.assertIn("false_positive_rate=1.000000e-02", body)


if __name__ == "__main__":
    unittest.main()
