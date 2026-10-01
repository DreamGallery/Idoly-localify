#!/usr/bin/env python3
"""Read-only check that the current test-game process loaded the reviewed MasterDB patch."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import sys

from deploy_master_blobs import REMOTE_FILE, device_hash
from deploy_text import adb, assert_expected_avd
from master_blob_patches import ORIG, ROOT, ZH, verify_patches


LOADED = re.compile(r"Loaded (\d+) MasterDB Blob patches; suppressed (\d+) global Text sources")
MATCHED = re.compile(r"Replaced MasterDB Blob; matches=(\d+)")
LOG_TIME = re.compile(r"^\s*(\d+\.\d+)\s+\d+\s+\d+\s+[A-Z]\s+")


def process_start_bounds(stat: str, uptime: str, clock_ticks: str,
                         epoch_before: str, epoch_after: str) -> tuple[float, float]:
    # /proc/<pid>/stat field 22 is starttime; fields after the final ") " begin at 3.
    fields = stat.strip().rsplit(") ", 1)
    if len(fields) != 2:
        raise ValueError("Cannot parse current game process start time")
    rest = fields[1].split()
    if len(rest) < 20:
        raise ValueError("Incomplete current game process stat")
    start_since_boot = int(rest[19]) / int(clock_ticks.strip())
    elapsed = float(uptime.split()[0])
    lower = int(epoch_before.strip()) - elapsed + start_since_boot
    upper = int(epoch_after.strip()) - elapsed + start_since_boot
    if lower > upper or upper - lower > 30:
        raise ValueError("Device clock changed during runtime check")
    return lower, upper


def inspect_log(log: str, process_start_lower: float, expected_patches: int,
                expected_suppressed: int, *, require_matches: bool) -> dict:
    messages = []
    for line in log.splitlines():
        time_match = LOG_TIME.match(line)
        if time_match and float(time_match.group(1)) >= process_start_lower - 2:
            messages.append(line[time_match.end():])
    if any("MasterDB Blob patches unavailable:" in line or
           "Cannot parse dictionary:" in line or
           "FATAL EXCEPTION" in line for line in messages):
        raise ValueError("Current game process logged a localization load or runtime error")
    loaded = [LOADED.search(line) for line in messages]
    loaded = [match for match in loaded if match]
    if len(loaded) != 1:
        raise ValueError("Expected one MasterDB Blob load log from the current game process")
    count, suppressed = map(int, loaded[0].groups())
    if (count, suppressed) != (expected_patches, expected_suppressed):
        raise ValueError(f"Current process loaded {count} patches/{suppressed} suppressed "
                         f"sources; expected {expected_patches}/{expected_suppressed}")
    if not any("Qua.Master.SQLite.ResultSet.GetBlob hook installed" in line
               for line in messages):
        raise ValueError("Current process has no MasterDB GetBlob hook installation log")
    matches = [int(found.group(1)) for line in messages
               if (found := MATCHED.search(line))]
    if require_matches and not matches:
        raise ValueError("No MasterDB record replacement logged; enter the game first")
    return {"patches_loaded": count, "suppressed_sources": suppressed,
            "matches_logged_at_least": max(matches, default=0),
            "getblob_hook": True}


def check(adb_bin: str, serial: str, expected_avd: str, master_tag: Path,
          orig_dir: Path, zh_dir: Path, output: Path,
          *, require_matches: bool) -> dict:
    assert_expected_avd(adb_bin, serial, expected_avd)
    tables = {path.stem for path in orig_dir.glob("*.json")}
    manifest = verify_patches(master_tag, orig_dir, zh_dir, tables, output)
    if device_hash(adb_bin, serial) != manifest["binary_sha256"]:
        raise ValueError("Device MasterDB Blob differs from the verified local patch")
    pid = adb(adb_bin, serial, "shell", "pidof", "game.qualiarts.idolypride").strip()
    if not re.fullmatch(r"\d+", pid):
        raise ValueError("Expected exactly one running game process")
    before = adb(adb_bin, serial, "shell", "date", "+%s")
    stat = adb(adb_bin, serial, "shell", "cat", f"/proc/{pid}/stat")
    uptime = adb(adb_bin, serial, "shell", "cat", "/proc/uptime")
    ticks = adb(adb_bin, serial, "shell", "getconf", "CLK_TCK")
    after = adb(adb_bin, serial, "shell", "date", "+%s")
    lower, upper = process_start_bounds(stat, uptime, ticks, before, after)
    modified = int(adb(adb_bin, serial, "shell", "stat", "-c", "%Y", REMOTE_FILE).strip())
    if modified >= lower - 1:
        raise ValueError("MasterDB Blob may have changed after this process started; restart game")
    log = adb(adb_bin, serial, "logcat", "-d", "-v", "epoch", f"--pid={pid}",
              "-s", "IdolyLocalify:I", "AndroidRuntime:E")
    result = inspect_log(log, lower, manifest["patches"],
                         len(manifest["suppressed_global_text_sources"]),
                         require_matches=require_matches)
    if adb(adb_bin, serial, "shell", "pidof", "game.qualiarts.idolypride").strip() != pid:
        raise ValueError("Game process changed during runtime check; retry")
    result["pid"] = int(pid)
    result["device_hash_matches"] = True
    result["patch_older_than_process"] = True
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--expected-avd", required=True)
    parser.add_argument("--adb", default=os.getenv("ADB", "adb"))
    parser.add_argument("--master-tag", type=Path, required=True)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    parser.add_argument("--output", type=Path, default=ROOT / ".analysis/master-blobs.bin")
    parser.add_argument("--require-matches", action="store_true",
                        help="Require a recorded GetBlob replacement after entering the game")
    args = parser.parse_args()
    try:
        result = check(args.adb, args.serial, args.expected_avd, args.master_tag,
                       args.orig_dir, args.zh_dir, args.output,
                       require_matches=args.require_matches)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"Runtime check failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
