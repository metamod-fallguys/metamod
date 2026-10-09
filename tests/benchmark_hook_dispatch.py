"""Compare two Release dispatchers with the same test harness (Linux i386)."""

import argparse
import os
from pathlib import Path
import re
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--repeats", type=int, default=5)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    if hasattr(os, "sched_getaffinity"):
        os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
    directory = args.build_dir.resolve() / "tests"
    samples = {}
    for _ in range(args.repeats):
        for label, executable in (
            ("baseline", "mmfg_test_api_hook_baseline"),
            ("cached", "mmfg_test_api_hook"),
        ):
            output = subprocess.check_output(
                [str(directory / executable), "--benchmark"],
                cwd=directory, text=True,
            )
            for slots, providers, ns in re.findall(
                r"slots=(\d+) providers=(\d+) iterations=\d+ ns/dispatch=([\d.]+)",
                output,
            ):
                samples.setdefault((int(slots), int(providers), label), []).append(float(ns))
    print("slots providers version median_ns min_ns max_ns")
    for (slots, providers, label), values in sorted(samples.items()):
        print(slots, providers, label, f"{statistics.median(values):.2f}",
              f"{min(values):.2f}", f"{max(values):.2f}")


if __name__ == "__main__":
    main()
