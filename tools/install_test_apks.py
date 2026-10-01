#!/usr/bin/env python3
"""Verify and install locally built APKs on an explicitly named test AVD."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess

from build_test_apks import preflight
from deploy_text import assert_expected_avd


def sdk_tool(sdk: Path, name: str) -> str:
    candidates = sorted((sdk / "build-tools").glob(f"*/{name}"))
    if not candidates:
        raise ValueError(f"Android SDK build-tools is missing {name}")
    return str(candidates[-1])


def install(base: Path, split: Path, serial: str, expected_avd: str,
            adb_bin: str, sdk: Path, *, check_only: bool = False,
            allow_untested_version: bool = False) -> str:
    if not base.is_file() or not split.is_file():
        raise ValueError("Both signed base and arm64 split APKs are required")
    env = os.environ.copy()
    version = preflight(base, split, None, sdk_tool(sdk, "aapt"),
                        sdk_tool(sdk, "apksigner"), env,
                        allow_untested_version=allow_untested_version)
    assert_expected_avd(adb_bin, serial, expected_avd)
    if not check_only:
        subprocess.run([adb_bin, "-s", serial, "install-multiple", "-r",
                        str(base), str(split)], check=True)
    return version


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--split", type=Path, required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--expected-avd", required=True)
    parser.add_argument("--adb", default=os.getenv("ADB", "adb"))
    parser.add_argument("--check-only", action="store_true",
                        help="Verify APKs and AVD identity without installing")
    parser.add_argument("--allow-untested-version", action="store_true")
    args = parser.parse_args()
    sdk_env = os.getenv("ANDROID_HOME") or os.getenv("ANDROID_SDK_ROOT")
    if not sdk_env or not Path(sdk_env).is_dir():
        parser.error("Set ANDROID_HOME or ANDROID_SDK_ROOT to the Android SDK")
    try:
        version = install(args.base, args.split, args.serial, args.expected_avd,
                          args.adb, Path(sdk_env), check_only=args.check_only,
                          allow_untested_version=args.allow_untested_version)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    action = "Verified" if args.check_only else "Installed"
    print(f"{action} IDOLY PRIDE {version} on {args.expected_avd} ({args.serial})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
