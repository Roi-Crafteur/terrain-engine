#!/usr/bin/env python3
"""Summarizes the CTest JUnit report of a CI job, to compare the platforms.

Usage: scripts/ci_test_report.py <ctest-junit.xml> <config-name> <output-dir>

Prints the golden checksums of cave_golden_test and writes them to golden-<config>.txt, writes
the duration and status of every test to durations-<config>.txt, warns (GitHub annotation) for
each test longer than 60 s, and adds all of it to the job summary when $GITHUB_STEP_SUMMARY is
set. It only reads the test outputs: it never fails because of their content.
"""

import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

GOLDEN_TEST = "cave_golden_test"
SLOW_SECONDS = 60.0
SLOWEST_SHOWN = 5


def read_tests(junit_path):
    """Returns (name, seconds, status, output) for each test of the report."""
    tests = []
    for case in ET.parse(junit_path).getroot().iter("testcase"):
        output = case.findtext("system-out") or ""
        seconds = float(case.get("time") or 0.0)
        tests.append((case.get("name", "?"), seconds, case.get("status", "?"), output))
    return tests


def golden_text(tests):
    for name, _, status, output in tests:
        if name == GOLDEN_TEST:
            return output.strip() or f"{GOLDEN_TEST}: no output (status {status})"
    return f"{GOLDEN_TEST}: not found in the report"


def durations_text(tests):
    ordered = sorted(tests, key=lambda test: test[1], reverse=True)
    rows = (f"{seconds:8.2f} s  {status:<7} {name}" for name, seconds, status, _ in ordered)
    return "\n".join(rows)


def summary_markdown(config, tests, golden, slow):
    total = sum(test[1] for test in tests)
    failed = [name for name, _, status, _ in tests if status != "run"]
    slowest = sorted(tests, key=lambda test: test[1], reverse=True)[:SLOWEST_SHOWN]
    lines = [
        f"### Tests on {config}",
        "",
        f"{len(tests)} tests, {len(failed)} not passed, {total:.1f} s in total.",
        "",
        f"Golden checksums ({GOLDEN_TEST}):",
        "",
        "```",
        golden,
        "```",
        "",
        "- Slowest tests: " + ", ".join(f"{name} {time:.1f} s" for name, time, _, _ in slowest),
    ]
    if slow:
        lines.append(f"- Tests over {SLOW_SECONDS:.0f} s: " + ", ".join(name for name, _ in slow))
    if failed:
        lines.append("- Not passed: " + ", ".join(failed))
    return "\n".join(lines) + "\n"


def main(argv):
    if len(argv) != 4:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    junit_path, config, output_dir = Path(argv[1]), argv[2], Path(argv[3])
    if not junit_path.is_file():
        print(f"::warning::{junit_path} not found: no test report for {config}")
        return 0
    tests = read_tests(junit_path)
    golden = golden_text(tests)
    slow = [(name, seconds) for name, seconds, _, _ in tests if seconds > SLOW_SECONDS]
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / f"golden-{config}.txt").write_text(golden + "\n", encoding="utf-8")
    (output_dir / f"durations-{config}.txt").write_text(durations_text(tests) + "\n",
                                                        encoding="utf-8")
    print(f"{config}: {golden}")
    for name, seconds in slow:
        print(f"::warning::{name} took {seconds:.1f} s on {config} (over {SLOW_SECONDS:.0f} s)")
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as summary:
            summary.write(summary_markdown(config, tests, golden, slow))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
