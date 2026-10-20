#!/usr/bin/env python3
"""Migrate verified MasterDB record and nested field keys without changing values."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid

from master_identity import (
    BUSINESS_KEY_RULES, PREFIX, index_records, record_key, rules_metadata,
    validate_keyed_table,
)
from master_json import text_fields
from master_field_paths import FieldPaths, NESTED_TABLES, field_rules_metadata


MARKER = "master/.master-key-migration-in-progress.json"
EXPORT_MARKER = "master/.master-export-in-progress.json"
LOCAL_ROOT = ".agent-local/master-key-migrations"
FETCH_MARKER = ".master-fetch-in-progress.json"
FETCH_STATE = "master-fetch-state.json"
METADATA = "master/record-keys.json"
FIELD_METADATA = "master/field-keys.json"
UPSTREAM = "upstream.json"
INDEX = re.compile(r"(?:0|[1-9][0-9]*)\Z")
SHA256 = re.compile(r"[a-f0-9]{64}\Z")
JOURNAL_PATH = re.compile(
    r"\.agent-local/master-key-migrations/"
    r"[0-9]{8}T[0-9]{6}Z-[a-f0-9]{32}/journal\.json\Z"
)
AFFECTED_TABLES = set(BUSINESS_KEY_RULES) | NESTED_TABLES
TARGETS = {UPSTREAM, METADATA, FIELD_METADATA} | {
    f"master/{language}/{table}.json"
    for language in ("orig", "zh-Hans") for table in AFFECTED_TABLES
}


class MigrationError(ValueError):
    """The identity migration cannot safely proceed."""


def _digest(data: bytes | None) -> str | None:
    return hashlib.sha256(data).hexdigest() if data is not None else None


def _json_bytes(value) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _json(data: bytes, label: str):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise MigrationError(f"{label}: duplicate JSON key {key!r}")
            result[key] = value
        return result
    try:
        return json.loads(data, object_pairs_hook=unique)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise MigrationError(f"{label}: invalid JSON: {exc}") from exc


def _safe_path(root: Path, relative: str) -> Path:
    parts = relative.split("/")
    if (Path(relative).is_absolute() or "\\" in relative or
            any(part in ("", ".", "..") for part in parts)):
        raise MigrationError(f"Unsafe migration path: {relative!r}")
    path = root
    for part in parts:
        path /= part
        if path.is_symlink():
            raise MigrationError(f"Symlinked migration path: {path}")
    return path


def _read(root: Path, relative: str, *, optional=False) -> bytes | None:
    path = _safe_path(root, relative)
    if not path.exists() and optional:
        return None
    if not path.is_file():
        raise MigrationError(f"Required regular file is missing: {path}")
    return path.read_bytes()


def _absent(root: Path, relative: str) -> None:
    if _safe_path(root, relative).exists():
        raise MigrationError(f"An operation is incomplete; resume it first: {root / relative}")


def _table(data: bytes, label: str) -> dict:
    value = _json(data, label)
    if not isinstance(value, dict) or any(
            not isinstance(fields, dict) or any(
                not isinstance(field, str) or not isinstance(text, str)
                for field, text in fields.items())
            for fields in value.values()):
        raise MigrationError(f"{label}: expected record key -> field path -> string")
    return value


def _capture(repository: Path) -> dict[str, bytes | None]:
    result = {}
    for language in ("orig", "zh-Hans"):
        folder = _safe_path(repository, f"master/{language}")
        if not folder.is_dir():
            raise MigrationError(f"Required directory is missing: {folder}")
        for path in sorted(folder.glob("*.json")):
            relative = f"master/{language}/{path.name}"
            result[relative] = _read(repository, relative)
    result[UPSTREAM] = _read(repository, UPSTREAM)
    result[METADATA] = _read(repository, METADATA, optional=True)
    result[FIELD_METADATA] = _read(repository, FIELD_METADATA, optional=True)
    return result


def _snapshot(inputs: dict[str, bytes | None]) -> str:
    # Exactly export_translation_sources.snapshot, using already captured bytes.
    lines = sorted((Path(path).name, _digest(data)) for path, data in inputs.items()
                   if path.startswith("master/orig/") and data is not None)
    return hashlib.sha256("".join(name + "\0" + digest + "\n"
                                  for name, digest in lines).encode("utf-8")).hexdigest()


@dataclass
class Plan:
    inputs: dict[str, bytes | None]
    outputs: dict[str, bytes]
    raw_sha256: dict[str, str]
    mappings: dict[str, dict[str, str]]
    field_mappings: dict[str, dict[str, dict[str, str]]]
    version: str
    snapshot_before: str
    snapshot_after: str
    records: int
    fields: int
    legacy_records: int
    positional_fields: int


def _plan(raw_dir: Path, repository: Path, inputs=None) -> Plan:
    _absent(raw_dir, FETCH_MARKER)
    _absent(repository, EXPORT_MARKER)
    if inputs is None:
        inputs = _capture(repository)
    upstream = _json(inputs[UPSTREAM], UPSTREAM)
    master = upstream.get("master") if isinstance(upstream, dict) else None
    if not isinstance(master, dict):
        raise MigrationError("upstream.json: missing master provenance")
    version = master.get("master_tag_version")
    state_bytes = _read(raw_dir, FETCH_STATE)
    state = _json(state_bytes, FETCH_STATE)
    if (not isinstance(version, str) or not version or not isinstance(state, dict) or
            state.get("version") != version or not isinstance(state.get("tables"), dict)):
        raise MigrationError("MasterTag version must match upstream.json and the verified fetch state")
    # A partial fetch can retain older tables while changing the top-level
    # version, so its table hashes alone do not establish row-order provenance.
    if state.get("complete_version") != version:
        raise MigrationError("A full verified MasterDB fetch is required: complete_version must match version")
    before = _snapshot(inputs)
    if master.get("orig_snapshot_sha256") != before:
        raise MigrationError("upstream.json: orig_snapshot_sha256 does not match the entire orig file set")
    if inputs[METADATA] is not None and _json(inputs[METADATA], METADATA) != rules_metadata():
        raise MigrationError(f"{METADATA}: existing identity rules differ from this migration")
    if (inputs[FIELD_METADATA] is not None and
            _json(inputs[FIELD_METADATA], FIELD_METADATA) != field_rules_metadata()):
        raise MigrationError(f"{FIELD_METADATA}: existing identity rules differ from this migration")
    affected = sorted(table for table in AFFECTED_TABLES
                      if any(f"master/{language}/{table}.json" in inputs
                             for language in ("orig", "zh-Hans")))
    if not affected:
        raise MigrationError("No MasterDB tables with supported identity rules found in the repository")
    outputs, mappings, field_mappings = {}, {}, {}
    raw_sha256 = {FETCH_STATE: _digest(state_bytes)}
    records_count = fields_count = legacy_records = positional_fields = 0
    for table in affected:
        original_path = f"master/orig/{table}.json"
        translated_path = f"master/zh-Hans/{table}.json"
        if original_path not in inputs:
            raise MigrationError(f"{table}: translation table has no original table")
        raw_name = table + ".json"
        raw_bytes = _read(raw_dir, raw_name)
        recorded = state["tables"].get(table)
        digest = _digest(raw_bytes)
        if not isinstance(recorded, dict) or recorded.get("sha256") != digest:
            raise MigrationError(f"{raw_name}: sha256 differs from the verified fetch state")
        raw_sha256[raw_name] = digest
        raw = _json(raw_bytes, raw_name)
        if not isinstance(raw, list) or any(not isinstance(row, dict) for row in raw):
            raise MigrationError(f"{raw_name}: expected a list of object records")
        indexed = index_records(table, raw)
        original = _table(inputs[original_path], original_path)
        translated = (_table(inputs[translated_path], translated_path)
                      if translated_path in inputs else {})
        if set(translated) - set(original):
            raise MigrationError(f"{table}: orphan translation record keys")
        is_legacy = (table in BUSINESS_KEY_RULES and bool(original) and
                     all(INDEX.fullmatch(key) for key in original))
        if not is_legacy:
            if table in BUSINESS_KEY_RULES and any(not key.startswith(PREFIX) for key in original):
                raise MigrationError(f"{table}: legacy row indices must be canonical nonnegative decimals")
            validate_keyed_table(table, original)
        mapping, table_field_mappings = {}, {}
        new_original, new_translated = {}, {}
        for old_key, fields in original.items():
            if is_legacy:
                # Bound the decimal length before int() to reject enormous indices.
                if len(old_key) > len(str(len(raw))) or int(old_key) >= len(raw):
                    raise MigrationError(f"{table}: legacy row index {old_key!r} is out of range")
                row = raw[int(old_key)]
                new_key = record_key(table, row)
            else:
                if old_key not in indexed:
                    raise MigrationError(f"{table}: business identity is absent from verified raw data: {old_key}")
                row, new_key = indexed[old_key], old_key
            selected = dict(text_fields(row))
            paths = FieldPaths(table, row)
            aliases, stable_sources = {}, {}
            for positional_path, source in selected.items():
                stable = paths.canonicalize(positional_path)
                if stable in stable_sources:
                    raise MigrationError(f"{table}[{old_key}]: duplicate nested field identity")
                aliases[positional_path] = stable
                aliases[stable] = stable
                stable_sources[stable] = source
            field_mapping = {}
            moved = {}
            for field, source in fields.items():
                stable = aliases.get(field)
                if stable is None or stable_sources[stable] != source:
                    raise MigrationError(f"{table}[{old_key}].{field}: original differs from verified raw text")
                if stable in moved:
                    raise MigrationError(f"{table}[{old_key}]: duplicate aliases for one nested field")
                moved[stable] = source
                if stable != field:
                    field_mapping[field] = stable
                    positional_fields += 1
            if set(translated.get(old_key, {})) - set(fields):
                raise MigrationError(f"{table}[{old_key}]: orphan translation fields")
            new_original[new_key] = moved
            if old_key in translated:
                new_translated[new_key] = {
                    field_mapping.get(field, field): value
                    for field, value in translated[old_key].items()}
            if field_mapping:
                table_field_mappings[old_key] = field_mapping
            mapping[old_key] = new_key
            records_count += 1
            fields_count += len(fields)
        mappings[table] = mapping
        if table_field_mappings:
            field_mappings[table] = table_field_mappings
        if is_legacy or table_field_mappings:
            legacy_records += len(original) if is_legacy else 0
            outputs[original_path] = _json_bytes(new_original)
            if translated_path in inputs:
                outputs[translated_path] = _json_bytes(new_translated)
    outputs[METADATA] = (inputs[METADATA] if inputs[METADATA] is not None
                         else _json_bytes(rules_metadata()))
    outputs[FIELD_METADATA] = (inputs[FIELD_METADATA] if inputs[FIELD_METADATA] is not None
                               else _json_bytes(field_rules_metadata()))
    after = _snapshot({**inputs, **outputs})
    if after != before:
        master["orig_snapshot_sha256"] = after
        outputs[UPSTREAM] = _json_bytes(upstream)
    outputs = {path: data for path, data in outputs.items() if data != inputs.get(path)}
    return Plan(inputs, outputs, raw_sha256, mappings, field_mappings, version, before, after,
                records_count, fields_count, legacy_records, positional_fields)


def _journal(plan: Plan, repository: Path) -> dict:
    return {
        "schema_version": 1,
        "repository": str(repository),
        "master_tag_version": plan.version,
        "input_sha256": {path: _digest(data) for path, data in plan.inputs.items()},
        "raw_sha256": plan.raw_sha256,
        "files": {path: {"before_sha256": _digest(plan.inputs.get(path)),
                         "after_sha256": _digest(data)}
                  for path, data in plan.outputs.items()},
        "mappings": plan.mappings,
        "field_mappings": plan.field_mappings,
        "orig_snapshot_sha256": {"before": plan.snapshot_before, "after": plan.snapshot_after},
    }


def _ignored(repository: Path, paths: list[Path]) -> None:
    try:
        git = subprocess.run(["git", "-C", str(repository), "rev-parse", "--show-toplevel"],
                             capture_output=True, text=True, check=False)
    except FileNotFoundError:
        git = None
    if git is not None and git.returncode == 0:
        checked = subprocess.run(
            ["git", "-C", str(repository), "check-ignore", "--stdin", "-z"],
            input="".join(str(path) + "\0" for path in paths),
            capture_output=True, text=True, check=False)
        if checked.returncode not in (0, 1) or set(filter(None, checked.stdout.split("\0"))) != {
                str(path) for path in paths}:
            raise MigrationError("Migration backups must be Git-ignored under repository/.agent-local/")
        return
    if git is not None and "not a git repository" not in git.stderr.lower():
        raise MigrationError(f"Cannot verify Git ignore rules: {git.stderr.strip()}")
    # Standalone fixtures/exports can declare the same exclusion locally.
    ignore = _read(repository, ".agent-local/.gitignore", optional=True)
    if ignore is None or [line.strip() for line in ignore.decode("utf-8").splitlines()
                          if line.strip() and not line.lstrip().startswith("#")] != ["*"]:
        raise MigrationError("A non-Git repository must contain .agent-local/.gitignore with only '*'")


def _sync_directory(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _store(root: Path, relative: str, data: bytes) -> Path:
    path = _safe_path(root, relative)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    _sync_directory(path.parent)
    return path


def _check_live(repository: Path, journal: dict, *, final=False) -> dict[str, bytes | None]:
    current = _capture(repository)
    if set(current) != set(journal["input_sha256"]):
        raise MigrationError("Repository file set changed outside the migration; refusing to overwrite edits")
    for relative, data in current.items():
        expected = {journal["input_sha256"][relative]}
        if relative in journal["files"]:
            after = journal["files"][relative]["after_sha256"]
            expected = {after} if final else expected | {after}
        if _digest(data) not in expected:
            raise MigrationError(f"{relative}: external edit is neither the migration before nor after state")
    return current


def _check_raw(raw_dir: Path, journal: dict) -> None:
    _absent(raw_dir, FETCH_MARKER)
    for name, digest in journal["raw_sha256"].items():
        if _digest(_read(raw_dir, name)) != digest:
            raise MigrationError(f"{name}: verified raw input changed during migration")


def _load_recovery(raw_dir: Path, repository: Path):
    marker_bytes = _read(repository, MARKER)
    marker = _json(marker_bytes, MARKER)
    if (not isinstance(marker, dict) or set(marker) != {"schema_version", "journal", "journal_sha256"} or
            marker["schema_version"] != 1 or not isinstance(marker["journal"], str) or
            not JOURNAL_PATH.fullmatch(marker["journal"])):
        raise MigrationError("Invalid migration marker; inspect the local journal before recovery")
    journal_path = _safe_path(repository, marker["journal"])
    data = _read(repository, marker["journal"])
    if _digest(data) != marker["journal_sha256"]:
        raise MigrationError("Migration journal hash differs from the marker")
    journal = _json(data, str(journal_path))
    if (not isinstance(journal, dict) or journal.get("schema_version") != 1 or
            journal.get("repository") != str(repository) or
            not isinstance(journal.get("files"), dict) or not journal["files"] or
            not set(journal["files"]).issubset(TARGETS) or
            not isinstance(journal.get("input_sha256"), dict)):
        raise MigrationError("Invalid migration journal or disallowed output path")
    for relative, entry in journal["files"].items():
        if (not isinstance(entry, dict) or
                set(entry) != {"before_sha256", "after_sha256"} or
                not isinstance(entry["after_sha256"], str) or
                not SHA256.fullmatch(entry["after_sha256"]) or
                (entry["before_sha256"] is not None and
                 (not isinstance(entry["before_sha256"], str) or
                  not SHA256.fullmatch(entry["before_sha256"])))):
            raise MigrationError(f"Invalid migration file hashes: {relative}")
    current = _check_live(repository, journal)
    before = dict(current)
    folder = journal_path.parent
    for relative, entry in journal["files"].items():
        baseline = (_read(folder, "before/" + relative)
                    if entry["before_sha256"] is not None else None)
        staged = _read(folder, "after/" + relative)
        if (_digest(baseline) != entry["before_sha256"] or
                _digest(staged) != entry["after_sha256"]):
            raise MigrationError(f"{relative}: backup or staged file hash changed")
        before[relative] = baseline
    plan = _plan(raw_dir, repository, before)
    if _journal(plan, repository) != journal:
        raise MigrationError("Migration journal does not reproduce the fully verified original plan")
    return plan, journal, folder, marker_bytes


def _install(repository: Path, folder: Path, relative: str, entry: dict) -> None:
    current = _read(repository, relative, optional=True)
    if _digest(current) == entry["after_sha256"]:
        return
    if _digest(current) != entry["before_sha256"]:
        raise MigrationError(f"{relative}: external edit before replacement")
    staged = _read(folder, "after/" + relative)
    if _digest(staged) != entry["after_sha256"]:
        raise MigrationError(f"{relative}: staged file hash changed")
    pending_relative = "pending/" + uuid.uuid4().hex
    pending = _safe_path(folder, pending_relative)
    _ignored(repository, [pending])
    _store(folder, pending_relative, staged)
    target = _safe_path(repository, relative)
    if _digest(_read(repository, relative, optional=True)) != entry["before_sha256"]:
        raise MigrationError(f"{relative}: external edit during replacement")
    os.replace(pending, target)
    _sync_directory(target.parent)


def _apply(raw_dir: Path, repository: Path, journal: dict, folder: Path, marker_bytes: bytes) -> None:
    _ignored(repository, [folder / "journal.json"])
    _check_live(repository, journal)
    _check_raw(raw_dir, journal)
    for relative, entry in sorted(journal["files"].items(), key=lambda item: (item[0] == UPSTREAM, item[0])):
        _absent(repository, EXPORT_MARKER)
        if _read(repository, MARKER) != marker_bytes:
            raise MigrationError("Migration marker changed during apply")
        _install(repository, folder, relative, entry)
    _check_live(repository, journal, final=True)
    _check_raw(raw_dir, journal)
    if _read(repository, MARKER) != marker_bytes:
        raise MigrationError("Migration marker changed before completion")
    _safe_path(repository, MARKER).unlink()
    _sync_directory(repository / "master")


def migrate(raw_dir: Path, repository: Path, *, apply: bool = False) -> dict:
    """Validate or apply a migration; retry the same call to resume its journal.

    Dry runs never write. Values and field subsets are copied verbatim, including
    empty translations and retained Japanese. Completed calls are idempotent.
    The caller must pause other writers for apply/recovery. Hash checks detect
    intervening edits, but are not a file lock honored by editors or other tools.
    """
    raw_dir, repository = Path(raw_dir).resolve(), Path(repository).resolve()
    marker_path = _safe_path(repository, MARKER)
    resumed = marker_path.exists()
    folder = None
    if resumed:
        plan, journal, folder, marker_bytes = _load_recovery(raw_dir, repository)
    else:
        plan = _plan(raw_dir, repository)
        journal = _journal(plan, repository)
    report = {
        "status": "dry-run" if plan.outputs else "unchanged",
        "changed": sorted(plan.outputs),
        "tables": len(plan.mappings),
        "records": plan.records,
        "fields": plan.fields,
        "legacy_records": plan.legacy_records,
        "positional_fields": plan.positional_fields,
        "master_tag_version": plan.version,
        "orig_snapshot_sha256": plan.snapshot_after,
        "resumed": resumed,
        "journal": str(folder / "journal.json") if folder else None,
    }
    if not apply or not plan.outputs:
        return report
    if not resumed:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        relative_folder = f"{LOCAL_ROOT}/{stamp}-{uuid.uuid4().hex}"
        folder = _safe_path(repository, relative_folder)
        local_files = [folder / "journal.json", folder / "marker.json"] + [
            folder / state / relative for relative in plan.outputs
            for state in ("before", "after")
            if state == "after" or plan.inputs.get(relative) is not None]
        _ignored(repository, local_files)
        for relative, data in plan.outputs.items():
            if plan.inputs.get(relative) is not None:
                _store(folder, "before/" + relative, plan.inputs[relative])
            _store(folder, "after/" + relative, data)
        journal_bytes = _json_bytes(journal)
        _store(folder, "journal.json", journal_bytes)
        marker_bytes = _json_bytes({
            "schema_version": 1, "journal": relative_folder + "/journal.json",
            "journal_sha256": _digest(journal_bytes)})
        prepared_marker = _store(folder, "marker.json", marker_bytes)
        current = _check_live(repository, journal)
        if any(_digest(data) != journal["input_sha256"][relative]
               for relative, data in current.items()):
            raise MigrationError("Repository changed while preparing migration backups")
        _check_raw(raw_dir, journal)
        _absent(repository, EXPORT_MARKER)
        # A hard link publishes a complete marker atomically and refuses to
        # overwrite a marker created concurrently by another migration.
        os.link(prepared_marker, marker_path)
        _sync_directory(marker_path.parent)
    _apply(raw_dir, repository, journal, folder, marker_bytes)
    report.update(status="applied", journal=str(folder / "journal.json"))
    return report


run = migrate


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-dir", type=Path, required=True)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--apply", action="store_true", help="apply the verified migration or resume its journal")
    args = parser.parse_args(argv)
    try:
        report = migrate(args.raw_dir, args.repository, apply=args.apply)
    except (ValueError, OSError) as exc:
        parser.exit(2, f"error: {exc}\n")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
