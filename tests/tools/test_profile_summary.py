"""不混合硬件/主机，不丢失败或空任务。 / Separate hardware/host and retain failures and empty work."""
import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location("profile_summary", pathlib.Path(__file__).resolve().parents[2] / "tools/summarize_profile.py")
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


class ProfileSummary(unittest.TestCase):
    def test_backend_status_and_percentiles(self):
        data = "\n".join([
            "profile phase=open elapsed_us=10 status=0 peak=100 attempts=1 backend=host",
            "profile phase=open elapsed_us=30 status=5 peak=150 attempts=2 backend=host",
            "\x1b[32mI (1) pico: profile phase=open elapsed_us=90 status=3 peak=200 attempts=0 backend=device\x1b[0m",
        ])
        rows = {r["backend"]: r for r in summary.summarize(data)}
        self.assertEqual(rows["host"]["samples"], 2)
        self.assertEqual(rows["host"]["non_ok"], 1)
        self.assertEqual(rows["host"]["p95_us"], 30)
        self.assertEqual(rows["device"]["statuses"], {"3": 1})
        self.assertEqual(rows["host"]["max_pool_phase_bytes"], 150)
