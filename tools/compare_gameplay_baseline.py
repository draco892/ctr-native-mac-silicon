#!/usr/bin/env python3
"""Compare simulation observations from two runs of the SAME input replay."""

import argparse
import csv
import json
from pathlib import Path
import statistics


FRAME_FIELDS = (
    "frameCounter", "timer", "level_id", "game_mode1", "loading_stage",
    "bool_demo_mode", "num_plyr", "elapsed_time_ms", "vsync_till_flip",
    "vSync_between_drawSync", "frameTimer_VsyncCallback",
)
DRIVER_FIELDS = (
    "frame_index", "frame_counter", "timer", "level_id", "game_mode", "loading_stage",
    "elapsed_time", "bucket", "driver_id", "pos_x", "pos_y", "pos_z", "velocity_x",
    "velocity_y", "velocity_z", "speed", "reserves", "fire_speed", "num_turbos",
    "jump_height", "jump_cooldown", "lap_index", "lap_time", "actions", "collision_flags",
)


def read_csv(path, required):
    with Path(path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        names = reader.fieldnames or []
        if len(names) != len(set(names)) or not set(required).issubset(names):
            raise ValueError(f"{path}: missing or duplicate CSV columns")
        rows = list(reader)
    for index, row in enumerate(rows):
        if None in row or any(value is None for value in row.values()):
            raise ValueError(f"{path}: incomplete row {index}")
        for field in required:
            int(row[field])  # Reject partial/non-numeric observations.
    return rows


def compare_rows(label, before, after, fields):
    if len(before) != len(after):
        raise ValueError(f"{label}: row count differs: {len(before)} != {len(after)}")
    for index, (left, right) in enumerate(zip(before, after)):
        for field in fields:
            if int(left[field]) != int(right[field]):
                raise ValueError(f"{label}: row {index}, {field}: {left[field]} != {right[field]}")


def timing(rows):
    values = [float(row["presentation_delta_ms"]) for row in rows
              if float(row["presentation_delta_ms"]) > 0]
    if not values:
        return None
    median = statistics.median(values)
    return {"intervals": len(values), "median_ms": median, "median_fps": 1000 / median,
            "min_ms": min(values), "max_ms": max(values)}


def compare(before, after):
    before, after = Path(before), Path(after)
    left = read_csv(before / "frame_times.csv", FRAME_FIELDS)
    right = read_csv(after / "frame_times.csv", FRAME_FIELDS)
    if not left:
        raise ValueError("empty frame trace")
    compare_rows("frames", left, right, FRAME_FIELDS)
    drivers = read_csv(before / "drivers.csv", DRIVER_FIELDS)
    candidates = read_csv(after / "drivers.csv", DRIVER_FIELDS)
    if not drivers:
        raise ValueError("no driver observations; capture a race, not just menus")
    compare_rows("drivers", drivers, candidates, DRIVER_FIELDS)
    return {"simulation_matches": True, "frames": len(left), "driver_observations": len(drivers),
            "before_timing": timing(left), "after_timing": timing(right)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(compare(args.before, args.after), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Baseline comparison failed: {error}\n")


if __name__ == "__main__":
    main()
