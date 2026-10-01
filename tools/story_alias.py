#!/usr/bin/env python3
"""Temporarily play a translated story TXT through an unlocked entry on a test AVD.

Only the external idoly-localify/resource override is changed. The original
override is backed up locally and restored with the `restore` command.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from workspace_paths import story_output_dir

from localize import ROOT, write_json


REMOTE = "/sdcard/Android/data/game.qualiarts.idolypride/files/idoly-localify/resource"
NAME = re.compile(r"adv_[A-Za-z0-9_]+\.txt\Z")
DEFAULT_SOURCE = story_output_dir(ROOT)
DEFAULT_STATE = ROOT / ".analysis/story-alias"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_name(name: str) -> str:
    if not NAME.fullmatch(name):
        raise ValueError(f"Expected an adv_*.txt filename, got {name!r}")
    return name


def adb(args, *parts: str) -> subprocess.CompletedProcess:
    return subprocess.run([args.adb, "-s", args.serial, *parts],
                          check=True, text=True, capture_output=True)


def remote_digest(args, remote: str) -> str:
    value = adb(args, "shell", "sha256sum", remote).stdout.split()
    if not value or not re.fullmatch(r"[0-9a-f]{64}", value[0]):
        raise ValueError(f"Could not verify remote TXT: {remote}")
    return value[0]


def start(args) -> None:
    entry = check_name(args.entry)
    script = check_name(args.script)
    if entry == script:
        raise ValueError("Entry and replacement script must differ")
    state_file = args.state_dir / "state.json"
    if state_file.exists():
        raise ValueError(f"An alias is already active; restore it first: {state_file}")
    source = args.source_dir / script
    if not source.is_file():
        raise FileNotFoundError(source)
    remote = f"{REMOTE}/{entry}"
    args.state_dir.mkdir(parents=True, exist_ok=True)
    backup = args.state_dir / "entry-original.txt"
    if backup.exists():
        raise ValueError(f"Previous backup still exists: {backup}")
    adb(args, "pull", remote, str(backup))
    original_hash = digest(backup)
    if remote_digest(args, remote) != original_hash:
        backup.unlink()
        raise ValueError("Remote entry changed during backup")
    state = {"entry": entry, "script": script, "original_sha256": original_hash,
             "replacement_sha256": digest(source)}
    write_json(state_file, state)
    try:
        adb(args, "push", str(source), remote)
        if remote_digest(args, remote) != state["replacement_sha256"]:
            raise ValueError("Replacement TXT did not arrive intact")
    except Exception:
        adb(args, "push", str(backup), remote)
        state_file.unlink()
        backup.unlink()
        raise
    print(f"Alias active: {entry} -> {script}. Restart and open the entry in game.")


def restore(args) -> None:
    state_file = args.state_dir / "state.json"
    backup = args.state_dir / "entry-original.txt"
    if not state_file.is_file() or not backup.is_file():
        raise FileNotFoundError("No saved story alias and backup to restore")
    state = json.loads(state_file.read_text(encoding="utf-8"))
    entry = check_name(state["entry"])
    remote = f"{REMOTE}/{entry}"
    if digest(backup) != state["original_sha256"]:
        raise ValueError("Local backup hash differs from saved state")
    current = remote_digest(args, remote)
    if current != state["replacement_sha256"]:
        raise ValueError("Remote entry changed since aliasing; refusing to overwrite it")
    adb(args, "push", str(backup), remote)
    if remote_digest(args, remote) != state["original_sha256"]:
        raise ValueError("Restore verification failed; backup kept")
    state_file.unlink()
    backup.unlink()
    print(f"Restored original {entry} override")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default=os.getenv("ADB", "adb"))
    parser.add_argument("--serial", default=os.getenv("ANDROID_SERIAL", "emulator-5560"))
    parser.add_argument("--source-dir", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--state-dir", type=Path, default=DEFAULT_STATE)
    commands = parser.add_subparsers(dest="command", required=True)
    start_command = commands.add_parser("start")
    start_command.add_argument("--entry", required=True, help="Unlocked adv_*.txt entry")
    start_command.add_argument("--script", required=True, help="Translated adv_*.txt to play")
    commands.add_parser("restore")
    args = parser.parse_args()
    try:
        if args.command == "start":
            start(args)
        else:
            restore(args)
    except (FileNotFoundError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
