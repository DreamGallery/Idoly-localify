#!/usr/bin/env python3
"""Capture only new IDOLY PRIDE runtime dump records from one test AVD."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from deploy_text import assert_expected_avd
from localize import ROOT, captured_entries, write_json


REMOTE = "/sdcard/Android/data/game.qualiarts.idolypride/files/idoly-localify/dump.jsonl"
DEFAULT_SESSION = ROOT / ".analysis/runtime-capture/session.json"


def pull_dump(adb_bin: str, serial: str, output: Path) -> None:
    result = subprocess.run([adb_bin, "-s", serial, "pull", REMOTE, str(output)],
                            capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(f"ADB could not read the game dump on {serial}: "
                           f"{result.stderr.strip() or result.stdout.strip()}")


def start(adb_bin: str, serial: str, avd: str, session_path: Path) -> dict:
    assert_expected_avd(adb_bin, serial, avd)
    if session_path.exists():
        raise ValueError(f"Capture session already exists: {session_path}; finish or abort it")
    session_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="runtime-capture-", dir=session_path.parent) as temp:
        baseline = Path(temp) / "baseline.jsonl"
        pull_dump(adb_bin, serial, baseline)
        data = baseline.read_bytes()
    if data and not data.endswith(b"\n"):
        raise ValueError("Device dump ends with an incomplete record; retry after the game writes")
    session = {"version": 1, "serial": serial, "avd": avd, "remote": REMOTE,
               "baseline_bytes": len(data), "baseline_sha256": hashlib.sha256(data).hexdigest(),
               "started_at": datetime.now(timezone.utc).isoformat()}
    write_json(session_path, session)
    return session


def finish(adb_bin: str, serial: str, avd: str, session_path: Path,
           output_path: Path) -> dict:
    assert_expected_avd(adb_bin, serial, avd)
    session = json.loads(session_path.read_text(encoding="utf-8"))
    if (not isinstance(session, dict) or session.get("version") != 1 or
            session.get("serial") != serial or session.get("avd") != avd or
            session.get("remote") != REMOTE or
            not isinstance(session.get("baseline_bytes"), int) or
            session["baseline_bytes"] < 0 or
            not isinstance(session.get("baseline_sha256"), str)):
        raise ValueError(f"Invalid or mismatched capture session: {session_path}")
    if output_path.exists():
        raise ValueError(f"Capture output already exists: {output_path}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="runtime-capture-", dir=output_path.parent) as temp:
        current = Path(temp) / "current.jsonl"
        pull_dump(adb_bin, serial, current)
        data = current.read_bytes()
    baseline_size = session["baseline_bytes"]
    if (len(data) < baseline_size or
            hashlib.sha256(data[:baseline_size]).hexdigest() != session["baseline_sha256"]):
        raise ValueError("Game dump was rotated or edited; start a new capture session")
    delta = data[baseline_size:]
    if delta and not delta.endswith(b"\n"):
        raise ValueError("Newest game dump record is incomplete; retry finish shortly")
    with tempfile.NamedTemporaryFile("wb", dir=output_path.parent,
                                     prefix=".runtime-delta-", delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(delta)
    try:
        records = 0
        recovered = 0
        for _, _, _, _, nul_recovered in captured_entries(temporary):
            records += 1
            recovered += nul_recovered
        os.replace(temporary, output_path)
    finally:
        temporary.unlink(missing_ok=True)
    session_path.unlink()
    return {"records": records, "nul_recovered": recovered,
            "bytes": len(delta), "output": str(output_path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("start", "finish", "abort"))
    parser.add_argument("--serial", required=True)
    parser.add_argument("--expected-avd", required=True)
    parser.add_argument("--adb", default=os.getenv("ADB", "adb"))
    parser.add_argument("--session", type=Path, default=DEFAULT_SESSION)
    parser.add_argument("--output", type=Path,
                        help="finish only; defaults to a timestamped .analysis file")
    args = parser.parse_args()
    if args.output and args.command != "finish":
        parser.error("--output is only valid with finish")
    try:
        if args.command == "start":
            result = start(args.adb, args.serial, args.expected_avd, args.session)
        elif args.command == "finish":
            output = args.output or ROOT / ".analysis/runtime-capture" / (
                "delta-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + ".jsonl")
            if not output.resolve().is_relative_to((ROOT / ".analysis").resolve()):
                raise ValueError("Runtime dump output must stay in the Git-ignored .analysis directory")
            result = finish(args.adb, args.serial, args.expected_avd,
                            args.session, output)
        else:
            assert_expected_avd(args.adb, args.serial, args.expected_avd)
            args.session.unlink()
            result = {"aborted": str(args.session)}
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"Runtime capture failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
