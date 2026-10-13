import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "baseline", Path(__file__).resolve().parents[1] / "tools/compare_gameplay_baseline.py")
baseline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baseline)


class GameplayBaselineTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.before = Path(self.tmp.name) / "before"
        self.after = Path(self.tmp.name) / "after"
        self.frame = {name: "0" for name in baseline.FRAME_FIELDS}
        self.frame["presentation_delta_ms"] = "33.7"
        self.driver = {name: "0" for name in baseline.DRIVER_FIELDS}
        self.driver["pos_x"] = "123456"
        for folder in (self.before, self.after):
            folder.mkdir()
            self.write(folder / "frame_times.csv", [self.frame])
            self.write(folder / "drivers.csv", [self.driver])

    def write(self, path, rows):
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)

    def test_same_simulation_different_presentation_times(self):
        self.frame["presentation_delta_ms"] = "40.0"
        self.write(self.after / "frame_times.csv", [self.frame])
        result = baseline.compare(self.before, self.after)
        self.assertTrue(result["simulation_matches"])
        self.assertEqual(result["driver_observations"], 1)

    def test_position_difference(self):
        self.driver["pos_x"] = "123457"
        self.write(self.after / "drivers.csv", [self.driver])
        with self.assertRaisesRegex(ValueError, "pos_x"):
            baseline.compare(self.before, self.after)

    def test_timer_difference(self):
        self.frame["timer"] = "1"
        self.write(self.after / "frame_times.csv", [self.frame])
        with self.assertRaisesRegex(ValueError, "timer"):
            baseline.compare(self.before, self.after)

    def test_missing_driver_row(self):
        self.write(self.before / "drivers.csv", [self.driver, self.driver])
        with self.assertRaisesRegex(ValueError, "row count"):
            baseline.compare(self.before, self.after)

    def test_truncated_capture(self):
        with (self.after / "drivers.csv").open("a") as stream:
            stream.write("0,0\n")
        with self.assertRaisesRegex(ValueError, "incomplete row"):
            baseline.compare(self.before, self.after)

    def test_empty_driver_capture_is_not_a_pass(self):
        for folder in (self.before, self.after):
            (folder / "drivers.csv").write_text(",".join(baseline.DRIVER_FIELDS) + "\n")
        with self.assertRaisesRegex(ValueError, "no driver observations"):
            baseline.compare(self.before, self.after)


if __name__ == "__main__":
    unittest.main()
