#!/usr/bin/env python3
"""Run the project tests and verify published text artifacts."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


from workspace_paths import toolkit_root, translator_root

ROOT = Path(__file__).resolve().parents[1]
SUITES = (
    ("localify", ROOT, "tools"),
    ("HoshimiToolkit", toolkit_root(ROOT), "tests"),
    ("Hoshimi_Teleprompter", translator_root(ROOT), "tests"),
)


def run(label: str, cwd: Path, *command: str) -> None:
    print(f"\n[{label}]", flush=True)
    result = subprocess.run([sys.executable, *command], cwd=cwd, check=False)
    if result.returncode:
        raise RuntimeError(f"{label} failed (exit {result.returncode})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", help="Also compare published text on this test device")
    parser.add_argument("--adb", help="ADB executable to use with --serial")
    parser.add_argument("--master-tag", type=Path,
                        help="Also verify the local all-table MasterDB Blob patch")
    parser.add_argument("--master-output", type=Path,
                        help="MasterDB Blob patch path; requires --master-tag")
    parser.add_argument("--live-runtime", action="store_true",
                        help="Also require the current game process to load and use the patch")
    parser.add_argument("--expected-avd", help="Exact test AVD name for --live-runtime")
    args = parser.parse_args()
    if sys.version_info < (3, 10):
        parser.error("Python 3.10 or newer is required; invoke this script with that interpreter")
    if args.adb and not args.serial:
        parser.error("--adb requires --serial")
    if args.master_output and not args.master_tag:
        parser.error("--master-output requires --master-tag")
    if args.live_runtime and not (args.serial and args.master_tag and args.expected_avd):
        parser.error("--live-runtime requires --serial, --master-tag and --expected-avd")
    if args.expected_avd and not args.live_runtime:
        parser.error("--expected-avd is only used with --live-runtime")
    try:
        for label, repository, tests in SUITES:
            if not (repository / tests).is_dir():
                raise RuntimeError(f"{label}: missing repository or test directory: {repository}")
            run(label, repository, "-m", "unittest", "discover", "-s", tests,
                "-p", "test_*.py")
        run("MasterDB field completeness", ROOT, "tools/master_json.py",
            "plan", "--require-complete")
        run("text pipeline", ROOT, "tools/pipeline.py", "verify")
        master_options = None
        if args.master_tag:
            master_options = ["--master-tag", str(args.master_tag), "--all-tables"]
            if args.master_output:
                master_options.extend(("--output", str(args.master_output)))
            run("MasterDB Blob", ROOT, "tools/master_blob_patches.py",
                *master_options, "--verify")
        run("repository contents", ROOT, "tools/check_repository_contents.py")
        if args.serial:
            command = ["tools/deploy_text.py", "verify", "--serial", args.serial]
            if args.adb:
                command.extend(("--adb", args.adb))
            run("test device", ROOT, *command)
            if master_options:
                command = ["tools/deploy_master_blobs.py", "verify", "--serial", args.serial,
                           *master_options]
                if args.adb:
                    command.extend(("--adb", args.adb))
                run("test device MasterDB Blob", ROOT, *command)
            if args.live_runtime:
                command = ["tools/check_runtime.py", "--serial", args.serial,
                           "--expected-avd", args.expected_avd,
                           "--master-tag", str(args.master_tag), "--require-matches"]
                if args.master_output:
                    command.extend(("--output", str(args.master_output)))
                if args.adb:
                    command.extend(("--adb", args.adb))
                run("current game process", ROOT, *command)
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1
    print("\nWorkspace checks passed", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
