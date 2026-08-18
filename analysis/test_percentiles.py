#!/usr/bin/env python3
"""Seeded lognormal CSV with a known p99.99; script must recover it within 1%."""

import io
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))

from percentiles import parse_csv, pct, summarize


N = 100_000
SEED = 20260816
SIGMA = 0.5
MU = np.log(400.0)


def write_csv(
    path,
    wake_us,
    scheduler_applied="1",
    scheduler_requested="1",
    label="synthetic",
    exec_us=None,
    computation_ns=None,
):
    with open(path, "w") as f:
        f.write("# platform=test\n")
        f.write(f"# label={label}\n")
        f.write(f"# scheduler_requested={scheduler_requested}\n")
        f.write(f"# scheduler_applied={scheduler_applied}\n")
        f.write("# memory_locked=0\n")
        if computation_ns is not None:
            f.write(f"# computation_ns={computation_ns}\n")
        f.write("seq,deadline_ns,wake_err_ns,exec_ns,flags\n")
        exec_ns = (
            [int(round(x * 1000.0)) for x in exec_us]
            if exec_us is not None
            else [0] * len(wake_us)
        )
        for i, us in enumerate(wake_us):
            ns = int(round(us * 1000.0))
            f.write(f"{i},0,{ns},{exec_ns[i]},0\n")


class PercentilesTest(unittest.TestCase):
    def test_p9999_recovered_at_100k(self):
        rng = np.random.default_rng(SEED)
        wake_us = rng.lognormal(MU, SIGMA, N)
        known = float(np.quantile(wake_us, 0.9999))
        self.assertIsNotNone(pct(wake_us, 0.9999))
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "synthetic.csv")
            write_csv(path, wake_us)
            _, loaded, _ = parse_csv(path)
            got = pct(loaded, 0.9999)
        self.assertIsNotNone(got)
        self.assertLess(abs(got - known) / known, 0.01)

    def test_gate_at_documented_threshold(self):
        a = np.arange(100_000, dtype=np.float64)
        self.assertEqual(round(100_000 * (1.0 - 0.9999)), 10)
        self.assertIsNotNone(pct(a, 0.9999))

    def test_timeshare_does_not_warn(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "ts.csv")
            write_csv(path, wake_us, scheduler_requested="0", scheduler_applied="0", label="ts")
            err = io.StringIO()
            with patch.object(sys, "stderr", err):
                row = summarize(path)
        self.assertTrue(row["name"].startswith("best effort"))
        self.assertNotIn("requested but not applied", err.getvalue())

    def test_missing_requested_key_does_not_warn(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "legacy.csv")
            with open(path, "w") as f:
                f.write("# platform=test\n# label=legacy\n# scheduler_applied=0\n")
                f.write("seq,deadline_ns,wake_err_ns,exec_ns,flags\n")
                for i, us in enumerate(wake_us):
                    f.write(f"{i},0,{int(round(us * 1000.0))},0,0\n")
            err = io.StringIO()
            with patch.object(sys, "stderr", err):
                summarize(path)
        self.assertNotIn("requested but not applied", err.getvalue())

    def test_denied_rt_warns(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "denied.csv")
            write_csv(path, wake_us, scheduler_requested="1", scheduler_applied="0", label="rt")
            err = io.StringIO()
            with patch.object(sys, "stderr", err):
                row = summarize(path)
        self.assertTrue(row["name"].startswith("best effort"))
        self.assertIn("requested but not applied", err.getvalue())

    def test_missing_computation_ns_does_not_warn(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        exec_us = np.full(1000, 511.0)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "legacy.csv")
            write_csv(path, wake_us, exec_us=exec_us)
            err = io.StringIO()
            with patch.object(sys, "stderr", err):
                summarize(path)
        self.assertNotIn("computation_ns", err.getvalue())

    def test_exec_below_80_percent_does_not_warn(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        exec_us = np.full(1000, 511.0)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "ok.csv")
            write_csv(path, wake_us, exec_us=exec_us, computation_ns=750000)
            err = io.StringIO()
            with patch.object(sys, "stderr", err):
                summarize(path)
        self.assertNotIn("demote", err.getvalue())

    def test_exec_above_80_percent_warns(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        exec_us = np.full(1000, 650.0)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "hot.csv")
            write_csv(path, wake_us, exec_us=exec_us, computation_ns=750000)
            err = io.StringIO()
            with patch.object(sys, "stderr", err):
                summarize(path)
        self.assertIn("demote", err.getvalue())
        self.assertIn("87%", err.getvalue())


if __name__ == "__main__":
    unittest.main()
