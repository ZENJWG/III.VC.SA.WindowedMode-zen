#!/usr/bin/env python3
"""Offline frame-pacing report for a CSV captured by PresentMon or similar.

No instrumentation is injected into the game. Examples:
  python tools/frame_time_report.py presentmon.csv
  python tools/frame_time_report.py capture.csv --column frame_ms --target-fps 60
"""
import argparse
import csv
import math
from pathlib import Path
from statistics import fmean, median

CANDIDATES = ("msBetweenPresents", "msBetweenDisplayChange", "FrameTimeMs",
              "frame_ms", "frametime_ms", "Frame Time (ms)")


def measure(frame_ms, target_fps=60.0):
    samples = [float(v) for v in frame_ms if math.isfinite(float(v)) and float(v) > 0]
    if not samples:
        raise ValueError("No positive, finite frame-time measurements")
    if target_fps <= 0 or not math.isfinite(target_fps):
        raise ValueError("Target FPS must be positive and finite")
    slowest = sorted(samples, reverse=True)
    tail_count = max(1, math.ceil(len(samples) * 0.01))
    p99_ms = slowest[tail_count - 1]
    budget = 1000.0 / target_fps
    # Millisecond CSVs often round 16.666... to 16.667; ignore tiny errors.
    rounding_allowance_ms = 0.05
    return {
        "frames": len(samples),
        "average_fps": 1000.0 / fmean(samples),
        "one_percent_low_fps": 1000.0 / fmean(slowest[:tail_count]),
        "median_ms": median(samples),
        "p99_ms": p99_ms,
        "over_budget": sum(ms > budget + rounding_allowance_ms for ms in samples),
        "severe_spikes": sum(ms > 2.0 * budget for ms in samples),
        "budget_ms": budget,
    }


def load_csv(path, column=None):
    with path.open(newline="", encoding="utf-8-sig") as file:
        reader = csv.DictReader(file)
        headers = reader.fieldnames or ()
        if column is None:
            column = next((key for key in CANDIDATES if key in headers), None)
        if column not in headers:
            raise ValueError(f"Frame-time column not found. Available: {', '.join(headers)}")
        values = []
        for record in reader:
            raw = record.get(column, "")
            try:
                number = float(raw)
            except (TypeError, ValueError):
                continue
            if math.isfinite(number) and number > 0:
                values.append(number)
        return values, column


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="Frame-time capture CSV")
    parser.add_argument("--column", help="Column with frame times in milliseconds")
    parser.add_argument("--target-fps", type=float, default=60.0,
                        help="Frame-budget baseline, not an FPS limiter (default: 60)")
    args = parser.parse_args()
    try:
        samples, column = load_csv(args.csv, args.column)
        result = measure(samples, args.target_fps)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    print(f"Source: {args.csv} [{column}]")
    print(f"Frames sampled: {result['frames']}")
    print(f"Average FPS: {result['average_fps']:.1f}")
    print(f"1% low FPS (slowest 1% mean): {result['one_percent_low_fps']:.1f}")
    print(f"Median frame time: {result['median_ms']:.2f} ms")
    print(f"99th-percentile frame time: {result['p99_ms']:.2f} ms")
    print(f"Frames over {result['budget_ms']:.2f} ms budget (+0.05 ms tolerance): {result['over_budget']}")
    print(f"Frames over 2x budget: {result['severe_spikes']}")


if __name__ == "__main__":
    main()
