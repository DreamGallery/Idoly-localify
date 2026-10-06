#!/usr/bin/env python3
"""Plan, sync, and verify compiled text on one explicitly selected test device."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import uuid

from pipeline import Pipeline, ROOT, digest
from font_deployment import assert_compatible

REMOTE_BASE = "/sdcard/Android/data/game.qualiarts.idolypride/files/idoly-localify"
REMOTE_RESOURCE = f"{REMOTE_BASE}/resource"
HASH_LINE = re.compile(r"^([0-9a-f]{64})  (.+)$")
RESOURCE_NAME = re.compile(r"^adv_[A-Za-z0-9_.-]+\.txt$")
HASH_BATCH_SIZE = 64
ERROR_TAIL_LIMIT = 1200
LIST_END = "__IDOLY_FILE_LIST_COMPLETE__"


def dictionary_removals(previous: dict, candidate: dict) -> list[dict]:
    """Find translated source keys that a local build would silently discard."""
    removed = []
    for section in ("text", "i18n", "i18n_by_source"):
        old, new = previous.get(section, {}), candidate.get(section, {})
        if not isinstance(old, dict) or not isinstance(new, dict):
            raise ValueError(f"Invalid dictionary section: {section}")
        for key, value in old.items():
            if section == "i18n_by_source":
                if not isinstance(value, dict) or not isinstance(new.get(key, {}), dict):
                    raise ValueError("Invalid source-guarded i18n dictionary")
                for source, translation in value.items():
                    replacement = new.get(key, {}).get(source)
                    if (isinstance(translation, str) and translation and translation != source
                            and (not replacement or replacement == source)):
                        removed.append({"section": section, "key": key, "source": source})
            elif isinstance(value, str) and value and value != key:
                replacement = new.get(key)
                if not replacement or replacement == key:
                    removed.append({"section": section, "key": key})
    return sorted(removed, key=lambda item: (item["section"], item["key"], item.get("source", "")))


def check_dictionary_upgrade(adb_bin: str, serial: str, candidate: Path,
                             expected_remote_hash: str, report: Path) -> list[dict]:
    raw = adb(adb_bin, serial, "exec-out", "cat", f"{REMOTE_BASE}/dict.json")
    if hashlib.sha256(raw.encode("utf-8")).hexdigest() != expected_remote_hash:
        raise ValueError("Device dictionary changed during comparison; retry")
    candidate_bytes = candidate.read_bytes()
    removals = dictionary_removals(json.loads(raw), json.loads(candidate_bytes))
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps({"device_sha256": expected_remote_hash,
        "candidate_sha256": hashlib.sha256(candidate_bytes).hexdigest(),
        "removed_translations": removals}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return removals


def adb(adb_bin: str, serial: str, *args: str) -> str:
    result = subprocess.run([adb_bin, "-s", serial, *args],
                            text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(f"adb {args[0]} failed ({result.returncode}): "
                           f"{error_tail(result.stderr.strip() or result.stdout.strip())}")
    return result.stdout


def error_tail(output: str) -> str:
    if len(output) > ERROR_TAIL_LIMIT:
        return "[output truncated; tail follows] " + output[-ERROR_TAIL_LIMIT:]
    return output or "no diagnostic output"


def directory_files(adb_bin: str, serial: str, directory: str,
                    resources_only: bool) -> list[str]:
    """Require a complete, successful listing, including for an absent directory."""
    quoted = shlex.quote(directory)
    selection = ' -name "adv_*.txt"' if resources_only else ''
    output = adb(adb_bin, serial, "shell",
                 f'if test -d {quoted}; then '
                 f'find {quoted} -maxdepth 1 -type f{selection} -print0; '
                 f'elif test -e {quoted} || test -L {quoted}; then exit 1; fi '
                 f"&& printf '%s\\0' {shlex.quote(LIST_END)}")
    parts = output.split("\0")
    if len(parts) < 2 or parts[-2:] != [LIST_END, ""]:
        raise ValueError("Incomplete device directory listing; deployment refused")
    paths = parts[:-2]
    for path in paths:
        name = path.removeprefix(directory + "/")
        if (path != directory + "/" + name or "/" in name or
                not (RESOURCE_NAME.fullmatch(name) or
                     (not resources_only and name in ("dict.json", "web-notice.json")))):
            raise ValueError("Device directory has an unexpected filename; inspect before applying")
    if len(paths) != len(set(paths)):
        raise ValueError("Duplicate path in device directory listing")
    return sorted(paths)


def directory_hashes(adb_bin: str, serial: str, directory: str,
                     resources_only: bool = False) -> dict[str, str]:
    paths = directory_files(adb_bin, serial, directory, resources_only)
    values = {}
    for start in range(0, len(paths), HASH_BATCH_SIZE):
        batch = paths[start:start + HASH_BATCH_SIZE]
        output = adb(adb_bin, serial, "shell",
                     "sha256sum " + " ".join(shlex.quote(path) for path in batch))
        # Check exact full paths too: an omitted or truncated result cannot be
        # mistaken for a missing remote file by the deployment planner.
        actual_paths = []
        for line in output.splitlines():
            match = HASH_LINE.fullmatch(line)
            if not match:
                raise ValueError("Incomplete or invalid device hash batch; deployment refused")
            actual_paths.append(match.group(2))
        if len(actual_paths) != len(batch) or set(actual_paths) != set(batch):
            raise ValueError("Device hash batch does not match requested files; deployment refused")
        values.update(parse_hashes(output))
    if directory_files(adb_bin, serial, directory, resources_only) != paths:
        raise ValueError("Device directory changed during hash scan; retry before applying")
    return values


def assert_expected_avd(adb_bin: str, serial: str, expected: str) -> None:
    """Stop before any writes when the ADB serial is not the selected test AVD."""
    if not expected or expected != expected.strip():
        raise ValueError("Expected AVD name must be nonempty and have no outer whitespace")
    response = adb(adb_bin, serial, "emu", "avd", "name")
    lines = [line.strip() for line in response.splitlines() if line.strip()]
    if len(lines) != 2 or lines[1] != "OK":
        raise ValueError(f"Cannot identify AVD for {serial}: {response!r}")
    if lines[0] != expected:
        raise ValueError(f"Device {serial} is AVD {lines[0]!r}, expected {expected!r}; "
                         "deployment refused")


def parse_hashes(output: str) -> dict[str, str]:
    values = {}
    for line in output.splitlines():
        line = line.strip()
        if not line:
            continue
        match = HASH_LINE.fullmatch(line)
        if not match:
            raise ValueError(f"Unexpected device hash output: {line[:120]}")
        name = Path(match.group(2)).name
        if name in values:
            raise ValueError(f"Duplicate device filename: {name}")
        values[name] = match.group(1)
    return values


def device_hashes(adb_bin: str, serial: str) -> tuple[str | None, dict[str, str]]:
    dictionary = adb(adb_bin, serial, "shell",
                     f'if test -f "{REMOTE_BASE}/dict.json"; then '
                     f'sha256sum "{REMOTE_BASE}/dict.json"; fi')
    dictionary_hashes = parse_hashes(dictionary)
    resources = directory_hashes(adb_bin, serial, REMOTE_RESOURCE, resources_only=True)
    return dictionary_hashes.get("dict.json"), resources


def device_web_notice_hash(adb_bin: str, serial: str) -> str | None:
    output = adb(adb_bin, serial, "shell",
                 f'if test -f "{REMOTE_BASE}/web-notice.json"; then '
                 f'sha256sum "{REMOTE_BASE}/web-notice.json"; fi')
    return parse_hashes(output).get("web-notice.json")


def expected_hashes(pipeline: Pipeline) -> tuple[str, dict[str, str]]:
    manifest = json.loads(pipeline.manifest.read_text(encoding="utf-8"))
    resources = {f"{name}.txt": data["txt"] for name, data in manifest["files"].items()}
    return digest(pipeline.dictionary), resources


def plan(expected_dict: str, expected_resources: dict[str, str],
         remote_dict: str | None, remote_resources: dict[str, str],
         expected_web: str | None = None, remote_web: str | None = None) -> dict:
    changed = sorted(name for name, value in expected_resources.items()
                     if remote_resources.get(name) != value)
    extras = sorted(set(remote_resources) - set(expected_resources))
    return {"dictionary_changed": remote_dict != expected_dict,
            "resources_changed": changed, "device_only": extras,
            "web_notice_changed": expected_web is not None and remote_web != expected_web}


def sync(adb_bin: str, serial: str, pipeline: Pipeline,
         differences: dict, expected_dict: str,
         expected_resources: dict[str, str], expected_web: str | None = None) -> str | None:
    changed = differences["resources_changed"]
    extras = differences["device_only"]
    if (not changed and not differences["dictionary_changed"] and
            not differences.get("web_notice_changed", False) and not extras):
        return None
    if any(not RESOURCE_NAME.fullmatch(name) or Path(name).name != name
           for name in extras):
        raise ValueError("Device-only resource has an unexpected filename; inspect before applying")
    stage = f"{REMOTE_BASE}/.text-stage-{uuid.uuid4().hex[:12]}"
    archive = (f"{REMOTE_BASE}/archive/resource/{uuid.uuid4().hex[:12]}"
               if extras else None)
    adb(adb_bin, serial, "shell", "mkdir", "-p", stage)
    try:
        if differences["dictionary_changed"]:
            adb(adb_bin, serial, "push", str(pipeline.dictionary), f"{stage}/dict.json")
        if differences.get("web_notice_changed", False):
            adb(adb_bin, serial, "push", str(pipeline.web_notice_dictionary),
                f"{stage}/web-notice.json")
        if changed:
            for start in range(0, len(changed), 80):
                batch = changed[start:start + 80]
                adb(adb_bin, serial, "push", *(
                    str(pipeline.txt_dir / name) for name in batch), f"{stage}/")
        actual = directory_hashes(adb_bin, serial, stage)
        expected = {name: expected_resources[name] for name in changed}
        if differences["dictionary_changed"]:
            expected["dict.json"] = expected_dict
        if differences.get("web_notice_changed", False):
            expected["web-notice.json"] = expected_web
        if actual != expected:
            raise ValueError("Device staging hashes differ from compiled local files")
        if archive:
            adb(adb_bin, serial, "shell", "mkdir", "-p", archive)
            for name in extras:
                source = shlex.quote(f"{REMOTE_RESOURCE}/{name}")
                target = shlex.quote(f"{archive}/{name}")
                adb(adb_bin, serial, "shell", f"mv {source} {target}")
        adb(adb_bin, serial, "shell", "mkdir", "-p", REMOTE_RESOURCE)
        if changed:
            adb(adb_bin, serial, "shell",
                f'for file in "{stage}"/adv_*.txt; do '
                f'mv "$file" "{REMOTE_RESOURCE}/" || exit 1; done')
        if differences["dictionary_changed"]:
            adb(adb_bin, serial, "shell", "mv", f"{stage}/dict.json",
                f"{REMOTE_BASE}/dict.json")
        if differences.get("web_notice_changed", False):
            adb(adb_bin, serial, "shell", "mv", f"{stage}/web-notice.json",
                f"{REMOTE_BASE}/web-notice.json")
    finally:
        adb(adb_bin, serial, "shell", "rm", "-rf", stage)
    return archive


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("plan", "apply", "verify"))
    parser.add_argument("--serial", required=True,
                        help="Exact ADB serial of the test device; no implicit default")
    parser.add_argument("--expected-avd",
                        help="Required for apply: exact test AVD name, checked before writes")
    parser.add_argument("--adb", default=os.getenv("ADB", "adb"))
    parser.add_argument("--restart", action="store_true",
                        help="Apply only: restart the game after successful hash verification")
    parser.add_argument("--dictionary-report", type=Path,
                        default=ROOT / ".analysis/text-deploy-dictionary-diff.json")
    parser.add_argument("--allow-dictionary-removals", action="store_true",
                        help="Apply only: permit reviewed intentional translation removals")
    args = parser.parse_args()
    if args.restart and args.command != "apply":
        parser.error("--restart is only valid with apply")
    if args.command == "apply" and not args.expected_avd:
        parser.error("apply requires --expected-avd to identify the test emulator")
    try:
        if args.command == "apply":
            assert_expected_avd(args.adb, args.serial, args.expected_avd)
        pipeline = Pipeline(ROOT)
        pipeline.run(verify=True)
        assert_compatible(adb, args.adb, args.serial, ROOT, pipeline.dictionary)
        expected_dict, expected_resources = expected_hashes(pipeline)
        remote_dict, remote_resources = device_hashes(args.adb, args.serial)
        expected_web = digest(pipeline.web_notice_dictionary)
        remote_web = device_web_notice_hash(args.adb, args.serial)
        differences = plan(expected_dict, expected_resources,
                           remote_dict, remote_resources, expected_web, remote_web)
        removals = []
        if differences["dictionary_changed"] and remote_dict is not None:
            removals = check_dictionary_upgrade(args.adb, args.serial, pipeline.dictionary,
                                                remote_dict, args.dictionary_report)
        if args.command == "apply" and removals and not args.allow_dictionary_removals:
            raise ValueError(f"Local dictionary would remove {len(removals)} device translations; "
                             f"sync newer sources first. See {args.dictionary_report}")
        archived_to = None
        archived_count = 0
        if args.command == "apply":
            archived_count = len(differences["device_only"])
            archived_to = sync(args.adb, args.serial, pipeline, differences,
                               expected_dict, expected_resources, expected_web)
            remote_dict, remote_resources = device_hashes(args.adb, args.serial)
            remote_web = device_web_notice_hash(args.adb, args.serial)
            differences = plan(expected_dict, expected_resources,
                               remote_dict, remote_resources, expected_web, remote_web)
        summary = {"dictionary_changed": differences["dictionary_changed"],
                   "dictionary_removed_translations": len(removals),
                   "web_notice_changed": differences["web_notice_changed"],
                   "resources_changed": len(differences["resources_changed"]),
                   "device_only": len(differences["device_only"]),
                   "archived_device_only": archived_count if archived_to else 0,
                   "archive_path": archived_to,
                   "first_changed": differences["resources_changed"][:8],
                   "first_device_only": differences["device_only"][:8]}
        print(json.dumps(summary, ensure_ascii=False, sort_keys=True))
        if args.command in ("verify", "apply") and (
                differences["dictionary_changed"] or differences["resources_changed"]
                or differences["device_only"] or differences["web_notice_changed"]):
            return 1
        if args.restart and args.command == "apply":
            adb(args.adb, args.serial, "shell", "am", "force-stop",
                "game.qualiarts.idolypride")
            adb(args.adb, args.serial, "shell", "monkey", "-p",
                "game.qualiarts.idolypride", "1")
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"Deployment failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
