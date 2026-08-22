#!/usr/bin/env python3
"""Smoke tests for estimator_error.py metadata and percentile gates."""

import os
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))

import estimator_error as ee


def write_pair(tmpdir, n=200, with_ekf=True):
    loop = os.path.join(tmpdir, "loop.csv")
    truth = os.path.join(tmpdir, "truth.csv")
    t0 = 1_000_000_000
    dt_loop = 4_000_000  # 250 Hz
    dt_truth = 1_000_000  # 1 kHz
    with open(loop, "w") as f:
        f.write("# platform=test\n# label=synth\n# ekf=1\n")
        f.write(
            "seq,deadline_ns,wake_err_ns,exec_ns,rx_ns,ekf_ns,ctrl_ns,flags,"
            "rx_count,seq_gaps,skew_ns,age_imu_ns,age_pos_ns,age_gps_ns,tick_class,"
            "ekf_pn,ekf_pe,ekf_pd,ekf_vn,ekf_ve,ekf_vd,ekf_trace_p,ekf_nis,ekf_rejects\n"
        )
        for i in range(n):
            t = t0 + i * dt_loop
            ctrl = 1 if (i % 5 == 4) else 0
            pn = 0.1 * (i // 5) if ctrl else 0.0
            f.write(
                f"{i},{t},0,0,0,0,0,0,0,0,0,0,0,0,{ctrl},"
                f"{pn},0,0,0,0,0,3.0,0,0\n"
            )
    with open(truth, "w") as f:
        f.write("# setpoint_rx=1\n")
        f.write("t_ns,px,py,pz,vx,vy,vz,ax,ay,az,ax_cmd,ay_cmd,az_cmd\n")
        n_truth = n * 4
        for i in range(n_truth):
            t = t0 + i * dt_truth
            # Match loop ctrl samples: every 20 ms → pn = 0.1 * k
            k = (t - t0) // (dt_loop * 5)
            px = 0.1 * k
            f.write(f"{t},{px},0,0,0,0,0,0,0,0,0,0,0\n")
    return loop, truth


class TestEstimatorError(unittest.TestCase):
    def test_small_n_reports_stats(self):
        with tempfile.TemporaryDirectory() as td:
            loop, truth = write_pair(td, n=200)
            out = os.path.join(td, "out.md")
            with patch.object(sys, "argv", ["estimator_error.py", loop, truth, "--out", out]):
                rc = ee.main()
            self.assertEqual(rc, 0)
            text = open(out).read()
            self.assertIn("pos_err_m", text)
            self.assertIn("trace_P", text)
            self.assertIn("# samples=", text)

    def test_p95_gate_n_a_when_too_few(self):
        # p95 needs ~10 above it → n >= ~200. With n=20 ctrl samples, p95 is n/a.
        err = np.linspace(0.0, 1.0, 20)
        self.assertIsNone(ee.pct(err, 0.95))
        self.assertIsNotNone(ee.pct(np.linspace(0.0, 1.0, 200), 0.95))


if __name__ == "__main__":
    unittest.main()
