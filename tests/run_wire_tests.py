#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
# Public-domain example code (CC0) - see ../LICENSE.
"""Run every B-GW Wire Test in both modes, one after another, and report one result.

    python3 tests/run_wire_tests.py [--exe build/BACnetExampleBGW] [--port 47890]

Each script runs once with --mode eo and once with --mode vn. Exit code 0 = every check
in every run passed. [KNOWN GAP] lines are reported but do not fail the run (TODO.md).
"""
import subprocess
import sys
from pathlib import Path

import gw_setup as setup

TESTS = Path(__file__).resolve().parent
SCRIPTS = ["discovery_test.py", "mapping_test.py", "failure_mode_test.py", "router_test.py"]
MODES = ["eo", "vn"]


def main():
    arguments = setup.parser(__doc__)
    arguments.set_defaults(mode=None)
    args = arguments.parse_args()
    outcomes = []
    for mode in MODES:
        for script in SCRIPTS:
            label = f"{script} --mode {mode}"
            print(f"\n===== {label} =====", flush=True)
            run = subprocess.run([sys.executable, "-B", str(TESTS / script), "--exe", args.exe,
                                  "--port", str(args.port), "--mode", mode],
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            print(run.stdout, end="", flush=True)
            totals = [line for line in run.stdout.splitlines() if "checks passed" in line]
            outcomes.append((label, run.returncode, totals[-1] if totals else "no summary"))

    print("\n===== summary =====")
    for label, returncode, totals in outcomes:
        print(f"{'PASS' if returncode == 0 else 'FAIL'}  {label:34} {totals}")
    failed = [label for label, returncode, _ in outcomes if returncode != 0]
    print("ALL WIRE TESTS PASSED" if not failed else f"FAILED: {', '.join(failed)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
