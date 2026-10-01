#!/usr/bin/env python3
"""Verify and atomically deploy a local MasterDB Blob patch to a named test AVD."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import uuid

from deploy_text import REMOTE_BASE, adb, assert_expected_avd, parse_hashes
from master_blob_patches import ORIG, ROOT, ZH, verify_patches


REMOTE_FILE = f"{REMOTE_BASE}/master-blobs.bin"


def device_hash(adb_bin: str, serial: str) -> str | None:
    result = adb(adb_bin, serial, "shell",
                 f'if test -f "{REMOTE_FILE}"; then sha256sum "{REMOTE_FILE}"; fi')
    return parse_hashes(result).get("master-blobs.bin")


def deploy(adb_bin: str, serial: str, expected_avd: str, output: Path,
           expected: str, recheck) -> bool:
    """Stage and verify before replacing; refuse every write on a wrong AVD."""
    assert_expected_avd(adb_bin, serial, expected_avd)
    if device_hash(adb_bin, serial) == expected:
        return False
    stage = f"{REMOTE_BASE}/.master-blobs-{uuid.uuid4().hex[:12]}.bin"
    adb(adb_bin, serial, "shell", "mkdir", "-p", REMOTE_BASE)
    try:
        adb(adb_bin, serial, "push", str(output), stage)
        staged = parse_hashes(adb(adb_bin, serial, "shell", "sha256sum", stage))
        if staged.get(Path(stage).name) != expected:
            raise ValueError("Staged MasterDB Blob hash differs; existing device file untouched")
        recheck()
        adb(adb_bin, serial, "shell", "mv", stage, REMOTE_FILE)
        if device_hash(adb_bin, serial) != expected:
            raise ValueError("Device MasterDB Blob hash differs after replacement")
    finally:
        adb(adb_bin, serial, "shell", "rm", "-f", stage)
    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("plan", "apply", "verify"))
    parser.add_argument("--serial", required=True)
    parser.add_argument("--expected-avd", help="Required for apply")
    parser.add_argument("--adb", default=os.getenv("ADB", "adb"))
    parser.add_argument("--master-tag", type=Path, required=True)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--table", action="append")
    selection.add_argument("--all-tables", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / ".analysis/master-blobs.bin")
    parser.add_argument("--restart", action="store_true")
    args = parser.parse_args(argv)
    if args.command == "apply" and not args.expected_avd:
        parser.error("apply requires --expected-avd")
    if args.restart and args.command != "apply":
        parser.error("--restart is only valid with apply")
    tables = ({path.stem for path in args.orig_dir.glob("*.json")}
              if args.all_tables else set(args.table))
    try:
        def local_manifest():
            return verify_patches(args.master_tag, args.orig_dir, args.zh_dir,
                                  tables, args.output)

        expected = local_manifest()["binary_sha256"]
        if args.command == "apply":
            changed = deploy(args.adb, args.serial, args.expected_avd,
                             args.output, expected, local_manifest)
        else:
            changed = device_hash(args.adb, args.serial) != expected
        actual = device_hash(args.adb, args.serial)
        summary = {"changed": changed, "matches": actual == expected,
                   "tables": len(tables), "patches": local_manifest()["patches"]}
        print(json.dumps(summary, ensure_ascii=False, sort_keys=True))
        if args.command == "verify" and actual != expected:
            return 1
        if args.command == "apply" and actual != expected:
            return 1
        if args.restart and args.command == "apply":
            adb(args.adb, args.serial, "shell", "am", "force-stop",
                "game.qualiarts.idolypride")
            adb(args.adb, args.serial, "shell", "monkey", "-p",
                "game.qualiarts.idolypride", "1")
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"MasterDB Blob deployment failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
