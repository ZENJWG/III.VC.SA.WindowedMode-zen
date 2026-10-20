import csv
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from frame_time_report import load_csv, measure


class FrameTimeReportTests(unittest.TestCase):
    def test_stable_60fps(self):
        values = [1000 / 60] * 100
        report = measure(values)
        self.assertAlmostEqual(report["average_fps"], 60.0)
        self.assertAlmostEqual(report["one_percent_low_fps"], 60.0)
        self.assertEqual(report["severe_spikes"], 0)

    def test_csv_rounding_is_not_a_spike(self):
        report = measure([16.667] * 100, target_fps=60)
        self.assertEqual(report["over_budget"], 0)

    def test_single_long_frame_degrades_one_percent_low(self):
        values = [10.0] * 99 + [100.0]
        report = measure(values, target_fps=60)
        self.assertAlmostEqual(report["one_percent_low_fps"], 10.0)
        self.assertEqual(report["severe_spikes"], 1)
        self.assertEqual(report["over_budget"], 1)

    def test_csv_column_autodetection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.csv"
            with path.open("w", newline="", encoding="utf-8") as f:
                writer = csv.DictWriter(f, fieldnames=["msBetweenPresents", "other"])
                writer.writeheader()
                writer.writerow({"msBetweenPresents": 16.6, "other": "x"})
                writer.writerow({"msBetweenPresents": "nan", "other": "y"})
                writer.writerow({"msBetweenPresents": 0, "other": "z"})
            values, column = load_csv(path)
            self.assertEqual(column, "msBetweenPresents")
            self.assertEqual(values, [16.6])

    def test_invalid_capture(self):
        with self.assertRaises(ValueError):
            measure([0.0, -1, math.nan])
        with self.assertRaises(ValueError):
            measure([16.0], target_fps=0)


if __name__ == "__main__":
    unittest.main()
