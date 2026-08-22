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
    age_imu_ns=None,
    age_pos_ns=None,
    age_gps_ns=None,
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
        typed = age_imu_ns is not None or age_pos_ns is not None or age_gps_ns is not None
        if typed:
            f.write(
                "seq,deadline_ns,wake_err_ns,exec_ns,rx_ns,flags,rx_count,seq_gaps,skew_ns,"
                "age_imu_ns,age_pos_ns,age_gps_ns\n"
            )
        elif rx_ns is not None:
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
            if typed:
                rx = 0 if rx_ns is None else int(rx_ns[i])
                imu = AGE_NONE if age_imu_ns is None else int(age_imu_ns[i])
                pos = AGE_NONE if age_pos_ns is None else int(age_pos_ns[i])
                gps = AGE_NONE if age_gps_ns is None else int(age_gps_ns[i])
                f.write(
                    f"{i},0,{ns},{exec_ns[i]},{rx},{fl},0,0,0,{imu},{pos},{gps}\n"
                )
            elif rx_ns is not None:
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
            _, _, _, _, age, flags, *_ = parse_csv(path)
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
        self.assertIn("age_imu_ns, microseconds", text)
        self.assertIn("age_pos_ns, microseconds", text)
        self.assertIn("age_gps_ns, microseconds", text)
        self.assertIn("kAgeNone rows dropped", text)
        self.assertIn("rx_ns, microseconds", text)
        self.assertIn("ctrl_ns, microseconds", text)
        self.assertEqual(text.count("| configuration | n | mean |"), 7)

    def test_staleness_floor_is_8ms_for_imu(self):
        # ceil(7.5 ms / 4 ms) * 4 ms. same integer ceil as src/msg_slots.h.
        limit_ns = 3 * 2_500_000
        period_ns = 4_000_000
        floor = ((limit_ns + period_ns - 1) // period_ns) * period_ns
        self.assertEqual(limit_ns, 7_500_000)
        self.assertEqual(floor, 8_000_000)
        self.assertLess(period_ns, limit_ns)
        self.assertGreater(2 * period_ns, limit_ns)

    def test_per_type_staleness_floors_at_250hz(self):
        period_ns = 4_000_000
        lim = 3
        imu_limit = lim * 2_500_000
        pos_limit = lim * 20_000_000
        gps_limit = lim * 200_000_000
        imu_floor = ((imu_limit + period_ns - 1) // period_ns) * period_ns
        pos_floor = ((pos_limit + period_ns - 1) // period_ns) * period_ns
        gps_floor = ((gps_limit + period_ns - 1) // period_ns) * period_ns
        self.assertEqual(imu_floor, 8_000_000)
        self.assertEqual(pos_floor, 60_000_000)
        self.assertEqual(gps_floor, 600_000_000)

    def test_staleness_floor_ns_in_metadata(self):
        wake_us = np.linspace(1.0, 100.0, 100)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "floor.csv")
            write_csv(
                path,
                wake_us,
                extra_meta=[
                    ("staleness_limit_periods", "3"),
                    ("staleness_floor_imu_ns", "8000000"),
                    ("staleness_floor_pos_ns", "60000000"),
                    ("staleness_floor_gps_ns", "600000000"),
                    ("stale_imu", "0"),
                    ("stale_pos", "2"),
                    ("stale_gps", "4"),
                ],
            )
            row = summarize(path)
        self.assertEqual(row["meta"]["staleness_limit_periods"], "3")
        self.assertEqual(row["meta"]["staleness_floor_imu_ns"], "8000000")
        self.assertEqual(row["meta"]["staleness_floor_pos_ns"], "60000000")
        self.assertEqual(row["meta"]["staleness_floor_gps_ns"], "600000000")
        self.assertEqual(row["stale_imu"], 0)
        self.assertEqual(row["stale_pos"], 2)
        self.assertEqual(row["stale_gps"], 4)

    def test_per_type_age_tables(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        age_imu = np.full(1000, 1000)
        age_pos = np.full(1000, 20000)
        age_gps = [400000] * 900 + [AGE_NONE] * 100
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "typed.csv")
            write_csv(
                path,
                wake_us,
                age_imu_ns=age_imu,
                age_pos_ns=age_pos,
                age_gps_ns=age_gps,
            )
            row = summarize(path)
        self.assertEqual(row["age_imu"]["n"], 1000)
        self.assertEqual(row["age_pos"]["n"], 1000)
        self.assertEqual(row["age_gps"]["n"], 900)
        self.assertAlmostEqual(row["age_imu"]["p50"], 1.0, places=1)
        self.assertAlmostEqual(row["age_pos"]["p50"], 20.0, places=1)
        self.assertIsNotNone(row["age_imu"]["p50"])
        self.assertIsNotNone(row["age_imu"]["p99"])
        self.assertIsNone(row["age_imu"]["p999"])
        self.assertIsNone(row["age_imu"]["p9999"])

    def test_rx_ns_table(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        rx_ns = np.full(1000, 12000)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "rx.csv")
            write_csv(path, wake_us, rx_ns=rx_ns)
            row = summarize(path)
        self.assertEqual(row["rx"]["n"], 1000)
        self.assertAlmostEqual(row["rx"]["p50"], 12.0, places=1)

    def test_ctrl_ns_excludes_zeros(self):
        wake_us = np.linspace(1.0, 100.0, 1000)
        ctrl_ns = np.zeros(1000, dtype=np.int64)
        tick = np.zeros(1000, dtype=np.int64)
        # every 5th tick is control, with 2500 ns plumbing
        ctrl_ns[::5] = 2500
        tick[::5] = 1
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "ctrl.csv")
            write_csv(
                path,
                wake_us,
                rx_ns=np.zeros(1000, dtype=np.int64),
                extra_meta=None,
            )
            # rewrite with ctrl_ns + tick_class columns
            with open(path) as f:
                lines = f.readlines()
            meta = [l for l in lines if l.startswith("#")]
            with open(path, "w") as f:
                f.writelines(meta)
                f.write(
                    "seq,deadline_ns,wake_err_ns,exec_ns,rx_ns,ctrl_ns,flags,"
                    "rx_count,seq_gaps,skew_ns,age_imu_ns,age_pos_ns,age_gps_ns,tick_class\n"
                )
                for i, us in enumerate(wake_us):
                    ns = int(round(us * 1000.0))
                    f.write(
                        f"{i},0,{ns},0,0,{int(ctrl_ns[i])},0,0,0,0,"
                        f"{AGE_NONE},{AGE_NONE},{AGE_NONE},{int(tick[i])}\n"
                    )
            row = summarize(path)
        self.assertEqual(row["ctrl"]["n"], 200)
        self.assertAlmostEqual(row["ctrl"]["p50"], 2.5, places=1)

    def test_wake_split_by_tick_class(self):
        n = 1000
        wake_us = np.zeros(n, dtype=np.float64)
        tick = np.zeros(n, dtype=np.int64)
        # plain=1 µs, ctrl=10 µs, telem (also ctrl)=20 µs — splits must not dilute.
        wake_us[:] = 1.0
        tick[::5] = 1
        wake_us[::5] = 10.0
        tick[::25] = 3  # CTRL|TELEM
        wake_us[::25] = 20.0
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "class.csv")
            with open(path, "w") as f:
                f.write("# label=class-split\n# scheduler_requested=1\n# scheduler_applied=1\n")
                f.write(
                    "seq,deadline_ns,wake_err_ns,exec_ns,rx_ns,ctrl_ns,flags,"
                    "rx_count,seq_gaps,skew_ns,age_imu_ns,age_pos_ns,age_gps_ns,tick_class\n"
                )
                for i in range(n):
                    ns = int(round(wake_us[i] * 1000.0))
                    f.write(
                        f"{i},0,{ns},0,0,0,0,0,0,0,"
                        f"{AGE_NONE},{AGE_NONE},{AGE_NONE},{int(tick[i])}\n"
                    )
            row = summarize(path)
        self.assertEqual(row["wake_plain"]["n"], 800)
        self.assertAlmostEqual(row["wake_plain"]["mean"], 1.0, places=6)
        self.assertEqual(row["wake_ctrl"]["n"], 200)
        self.assertEqual(row["wake_telem"]["n"], 40)
        self.assertAlmostEqual(row["wake_telem"]["mean"], 20.0, places=6)


if __name__ == "__main__":
    unittest.main()
