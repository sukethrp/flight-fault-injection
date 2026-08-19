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

from percentiles import AGE_NONE, FLAG_STALE, parse_csv, pct, summarize


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
    age_ns=None,
    flags=None,
    rx_ns=None,
    extra_meta=None,
):
    with open(path, "w") as f:
        f.write("# platform=test\n")
        f.write(f"# label={label}\n")
        f.write(f"# scheduler_requested={scheduler_requested}\n")
        f.write(f"# scheduler_applied={scheduler_applied}\n")
        f.write("# memory_locked=0\n")
        if computation_ns is not None:
            f.write(f"# computation_ns={computation_ns}\n")
        if extra_meta:
            for k, v in extra_meta:
                f.write(f"# {k}={v}\n")
        if rx_ns is not None:
            f.write(
                "seq,deadline_ns,wake_err_ns,exec_ns,rx_ns,flags,rx_count,seq_gaps,skew_ns,age_ns\n"
            )
        elif age_ns is None:
            f.write("seq,deadline_ns,wake_err_ns,exec_ns,flags\n")
        else:
            f.write("seq,deadline_ns,wake_err_ns,exec_ns,flags,rx_count,seq_gaps,skew_ns,age_ns\n")
        exec_ns = (
            [int(round(x * 1000.0)) for x in exec_us]
            if exec_us is not None
            else [0] * len(wake_us)
        )
        for i, us in enumerate(wake_us):
            ns = int(round(us * 1000.0))
            fl = 0 if flags is None else int(flags[i])
            if rx_ns is not None:
                age = 0 if age_ns is None else int(age_ns[i])
                f.write(
                    f"{i},0,{ns},{exec_ns[i]},{int(rx_ns[i])},{fl},0,0,0,{age}\n"
                )
            elif age_ns is None:
                f.write(f"{i},0,{ns},{exec_ns[i]},{fl}\n")
            else:
                f.write(f"{i},0,{ns},{exec_ns[i]},{fl},0,0,0,{int(age_ns[i])}\n")


class PercentilesTest(unittest.TestCase):
    def test_p9999_recovered_at_100k(self):
        rng = np.random.default_rng(SEED)
        wake_us = rng.lognormal(MU, SIGMA, N)
        known = float(np.quantile(wake_us, 0.9999))
        self.assertIsNotNone(pct(wake_us, 0.9999))
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "synthetic.csv")
            write_csv(path, wake_us)
            _, loaded, *_ = parse_csv(path)
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
        self.assertIn("exec p99", err.getvalue())
        self.assertIn("87%", err.getvalue())

    def test_exec_p50_under_p99_over_warns(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        exec_us = np.concatenate([np.full(980, 500.0), np.full(20, 700.0)])
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "tail.csv")
            write_csv(path, wake_us, exec_us=exec_us, computation_ns=750000)
            err = io.StringIO()
            with patch.object(sys, "stderr", err):
                summarize(path)
        self.assertIn("demote", err.getvalue())
        self.assertIn("exec p99", err.getvalue())

    def test_stale_flag_bit(self):
        self.assertEqual(FLAG_STALE, 1 << 3)
        self.assertEqual(FLAG_STALE, 8)

    def test_stale_flag_counted_from_csv(self):
        n = 100
        wake_us = np.linspace(1.0, 100.0, n)
        flags = [0] * 80 + [FLAG_STALE] * 20
        age_ns = [0] * 80 + [8_000_000] * 20
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "stale.csv")
            write_csv(path, wake_us, age_ns=age_ns, flags=flags)
            row = summarize(path)
        self.assertEqual(row["stale_n"], 20)
        self.assertEqual(row["age"]["n"], 100)

    def test_age_sentinel_excluded(self):
        wake_us = np.linspace(1.0, 100.0, 100)
        age_ns = [1000] * 90 + [AGE_NONE] * 10
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "age.csv")
            write_csv(path, wake_us, age_ns=age_ns)
            _, _, _, _, age, flags = parse_csv(path)
        self.assertEqual(age.size, 90)
        self.assertTrue(np.allclose(age, 1.0))
        self.assertEqual(flags.size, 100)

    def test_age_table_same_gates(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        age_ns = np.arange(1000, 2000, dtype=np.int64)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "age_gate.csv")
            write_csv(path, wake_us, age_ns=age_ns)
            row = summarize(path)
        self.assertEqual(row["age"]["n"], 1000)
        self.assertIsNotNone(row["age"]["p50"])
        self.assertIsNotNone(row["age"]["p99"])
        self.assertIsNone(row["age"]["p999"])
        self.assertIsNone(row["age"]["p9999"])

    def test_age_and_exec_tables_emitted(self):
        from percentiles import main

        wake_us = np.linspace(1.0, 100.0, 1000)
        exec_us = np.full(1000, 515.4)
        age_ns = np.full(1000, 0)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "tables.csv")
            out = os.path.join(td, "out.md")
            write_csv(path, wake_us, exec_us=exec_us, age_ns=age_ns)
            stdout = io.StringIO()
            with patch.object(sys, "stdout", stdout), patch.object(
                sys, "argv", ["percentiles.py", path, "--out", out]
            ):
                main()
        text = stdout.getvalue()
        self.assertIn("exec_ns, microseconds", text)
        self.assertIn("age_ns, microseconds", text)
        self.assertIn("kAgeNone rows dropped", text)
        self.assertIn("rx_ns, microseconds", text)
        self.assertEqual(text.count("| configuration | n | mean |"), 4)

    def test_staleness_floor_is_8ms_for_imu(self):
        # ceil(7.5 ms / 4 ms) * 4 ms. same integer ceil as src/msg_slots.h.
        limit_ns = 3 * 2_500_000
        period_ns = 4_000_000
        floor = ((limit_ns + period_ns - 1) // period_ns) * period_ns
        self.assertEqual(limit_ns, 7_500_000)
        self.assertEqual(floor, 8_000_000)
        self.assertLess(period_ns, limit_ns)
        self.assertGreater(2 * period_ns, limit_ns)

    def test_staleness_floor_ns_in_metadata(self):
        wake_us = np.linspace(1.0, 100.0, 100)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "floor.csv")
            write_csv(
                path,
                wake_us,
                extra_meta=[
                    ("staleness_limit_periods", "3"),
                    ("staleness_floor_ns", "8000000"),
                ],
            )
            row = summarize(path)
        self.assertEqual(row["meta"]["staleness_limit_periods"], "3")
        self.assertEqual(row["meta"]["staleness_floor_ns"], "8000000")

    def test_rx_ns_table(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        rx_ns = np.full(1000, 12000)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "rx.csv")
            write_csv(path, wake_us, rx_ns=rx_ns)
            row = summarize(path)
        self.assertEqual(row["rx"]["n"], 1000)
        self.assertAlmostEqual(row["rx"]["p50"], 12.0, places=1)


if __name__ == "__main__":
    unittest.main()
