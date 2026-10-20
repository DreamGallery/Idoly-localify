#!/usr/bin/env python3
"""Maintain IDOLY PRIDE MasterDB source and translation JSON dictionaries."""

import argparse
from collections import defaultdict
from datetime import datetime, timezone
import filecmp
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import uuid

from localize import validate, write_json
from master_identity import index_records, rules_metadata, validate_keyed_table
from master_field_paths import FieldPaths, field_rules_metadata, validate_field_table


ROOT = Path(__file__).resolve().parent.parent
ORIG = ROOT / "translations/master/orig"
ZH = ROOT / "translations/master/zh-Hans"
TEXT_FIELDS = re.compile(
    r"^(?:name|title|subTitle|description|shortDescription|unlockDescription|"
    r"text|message|word|theme|choiceText|longDescription|precaution|howToGet|"
    r"appealText|managerText|displayConditionDescription|managerCallText|"
    r"characterArrivalText|obtainMessage|eventName|placeName|guageName|"
    r"branchFirstText|branchCautionText|dailyRewardName|shortProfile|"
    r"evolveMessage|age|birthday|zodiacSign|hometown|favorite|unfavorite|"
    r"profile|catchphrase|idiom|excursionFirstText|"
    r"functionMaintenanceTitle|functionMaintenanceMessage|"
    r"tutorialAdvTitle|tutorialAdvSubTitle|dreamAreaName|singer|"
    r"lyricist|composer|arranger)$", re.I)
JAPANESE = re.compile(r"[\u3040-\u30ff\u3400-\u9fff]")
TEXT_LIST_FIELDS = {"texts"}
MUSIC_CREDIT_FIELDS = {"lyricist", "composer", "arranger"}
# These fields are identifiers, character names handled by the shared glossary,
# staff credits, or non-text profile measurements. A new Japanese string field
# must be reviewed before it can be silently excluded from the export.
NON_TRANSLATABLE_FIELDS = {
    "Character.firstName",
    "Character.groupName",
    "Character.cv",
    "Character.loveInfo.cv",
    "Character.loveInfos[].cv",
    "Character.threeSize",
    "Character.weight",
}


def text_fields(value, path=""):
    if isinstance(value, dict):
        for key, child in value.items():
            child_path = f"{path}.{key}" if path else key
            if isinstance(child, str) and TEXT_FIELDS.fullmatch(key) and JAPANESE.search(child):
                yield child_path, child
            else:
                yield from text_fields(child, child_path)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            child_path = f"{path}[{index}]"
            if (isinstance(child, str) and path.rsplit(".", 1)[-1] in TEXT_LIST_FIELDS
                    and JAPANESE.search(child)):
                yield child_path, child
            else:
                yield from text_fields(child, child_path)


def japanese_string_fields(value, path=""):
    """Find all Japanese-looking strings, including as-yet unknown field names."""
    if isinstance(value, dict):
        for key, child in value.items():
            child_path = f"{path}.{key}" if path else key
            yield from japanese_string_fields(child, child_path)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from japanese_string_fields(child, f"{path}[{index}]")
    elif isinstance(value, str) and JAPANESE.search(value):
        yield path, value


def unclassified_fields(record: dict, table: str,
                        selected: list[tuple[str, str]]) -> list[str]:
    """Return Japanese fields the extractor would otherwise silently miss."""
    known = set(selected)
    result = set()
    for field_path, value in japanese_string_fields(record):
        if (field_path, value) in known:
            continue
        normalized = re.sub(r"\[\d+\]", "[]", field_path)
        if normalized == "id" or f"{table}.{normalized}" in NON_TRANSLATABLE_FIELDS:
            continue
        result.add(normalized)
    return sorted(result)


def read_table(path):
    if not path.is_file():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"{path}: expected a JSON dictionary")
    for record_id, fields in data.items():
        if not isinstance(record_id, str) or not isinstance(fields, dict) or any(
                not isinstance(field, str) or not isinstance(value, str)
                for field, value in fields.items()):
            raise ValueError(f"{path}: expected record ID -> field path -> string")
    return data


def require_ready(master_dir: Path) -> None:
    for name in (".master-export-in-progress.json", ".master-key-migration-in-progress.json"):
        marker = master_dir / name
        if marker.is_file():
            raise ValueError(f"MasterDB operation was interrupted; resume it before reading: {marker}")
    validate_record_schema(master_dir)


def validate_record_schema(master_dir: Path) -> None:
    for filename, expected in (("record-keys.json", rules_metadata()),
                               ("field-keys.json", field_rules_metadata())):
        path = master_dir / filename
        if path.is_file() and json.loads(path.read_text(encoding="utf-8")) != expected:
            raise ValueError(f"{path}: unsupported MasterDB identity rules; update the tools first")


def validate_field(source: str, target: str, label: str,
                   *, allow_line_reflow: bool = False) -> None:
    validate(source, target, label)
    if not allow_line_reflow and (source.count("\n") != target.count("\n") or
                                  source.count("\r") != target.count("\r")):
        raise ValueError(f"{label}: line ending count changed")
    if (source.count("<") != target.count("<") or
            source.count(">") != target.count(">")):
        raise ValueError(f"{label}: angle bracket count changed")


def _atomic_copy(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=target.parent, prefix=".master-export-",
                                     delete=False) as stream:
        temporary = Path(stream.name)
    try:
        shutil.copyfile(source, temporary)
        os.replace(temporary, target)
    finally:
        temporary.unlink(missing_ok=True)


def _different(current: Path, staged: Path) -> bool:
    return not current.is_file() or not filecmp.cmp(current, staged, shallow=False)


def _file_digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def _optional_digest(path: Path) -> str | None:
    return _file_digest(path) if path.is_file() else None


def _table_snapshot(orig_dir: Path, zh_dir: Path, names: set[str]) -> dict:
    return {name: {"orig": _optional_digest(orig_dir / name),
                   "zh-Hans": _optional_digest(zh_dir / name)}
            for name in names}


def _input_revision(input_files: list[Path], seed: Path | None,
                    glossary: Path | None) -> str:
    value = hashlib.sha256()
    for label, path in ([(path.name, path) for path in input_files] +
                        [("seed", seed), ("names", glossary)]):
        value.update(label.encode("utf-8"))
        value.update(b"\0")
        value.update(_file_digest(path).encode("ascii") if path else b"none")
        value.update(b"\0")
    return value.hexdigest()


def export(input_dir: Path, orig_dir: Path, zh_dir: Path,
           seed_dictionary: Path | None = None,
           name_glossary: Path | None = None, *,
           prune_missing: bool = False,
           allow_large_prune: bool = False,
           archive_dir: Path | None = None) -> tuple[int, int]:
    seed_data = json.loads(seed_dictionary.read_text(encoding="utf-8")) \
        if seed_dictionary else {"text": {}}
    if not isinstance(seed_data, dict):
        raise ValueError("Seed dictionary must be a JSON object")
    seeds = seed_data.get("text", {})
    names = json.loads(name_glossary.read_text(encoding="utf-8")) \
        if name_glossary else {}
    if (not isinstance(seeds, dict) or not isinstance(names, dict) or
            any(not isinstance(key, str) or not isinstance(value, str)
                for mapping in (seeds, names) for key, value in mapping.items())):
        raise ValueError("Seed dictionary and name glossary must map strings to strings")
    if (input_dir / ".master-fetch-in-progress.json").is_file():
        raise ValueError(f"{input_dir}: MasterDB fetch is incomplete; retry it before export")
    if (orig_dir.parent / ".master-key-migration-in-progress.json").exists():
        raise ValueError("MasterDB key migration is incomplete; resume it before export")
    validate_record_schema(orig_dir.parent)
    fetch_state = input_dir / "master-fetch-state.json"
    state = None
    if fetch_state.is_file():
        state = json.loads(fetch_state.read_text(encoding="utf-8"))
        if (not isinstance(state, dict) or not isinstance(state.get("tables"), dict)):
            raise ValueError(f"{fetch_state}: invalid fetch state")
        if (prune_missing and (not state.get("version") or
                state.get("complete_version") != state["version"])):
            raise ValueError(f"{fetch_state}: full MasterDB fetch is required before pruning")
    input_files = sorted(path for path in input_dir.glob("*.json")
                         if path.name != fetch_state.name)
    if not input_files:
        raise ValueError(f"No MasterDB JSON tables in {input_dir}")
    if state is not None:
        for path in input_files:
            record = state["tables"].get(path.stem)
            if not isinstance(record, dict) or record.get("sha256") != _file_digest(path):
                raise ValueError(f"{path}: differs from the verified MasterDB fetch state")
        if prune_missing and {path.stem for path in input_files} != set(state["tables"]):
            raise ValueError(f"{fetch_state}: raw tables do not match the complete fetch")
    revision = _input_revision(input_files, seed_dictionary, name_glossary)
    fetch_state_digest = _optional_digest(fetch_state)
    first_names = set()
    character_path = input_dir / "Character.json"
    if character_path.is_file():
        character_records = json.loads(character_path.read_text(encoding="utf-8"))
        if not isinstance(character_records, list):
            raise ValueError(f"{character_path}: expected a list of records")
        for record in character_records:
            if not isinstance(record, dict):
                raise ValueError(f"{character_path}: expected object records")
            if isinstance(record.get("firstName"), str):
                first_names.add(record["firstName"])
    existing = (set(path.name for path in orig_dir.glob("*.json")) |
                set(path.name for path in zh_dir.glob("*.json")))
    live_before = _table_snapshot(orig_dir, zh_dir, existing)
    if prune_missing and len(input_files) < len(existing) and not allow_large_prune:
        raise ValueError("MasterDB dump has fewer raw tables than existing text tables; "
                         "it may be partial. Omit --prune-missing or inspect and use "
                         "--allow-large-prune")
    stage_parent = orig_dir.parent
    stage_parent.mkdir(parents=True, exist_ok=True)
    marker = stage_parent / ".master-export-in-progress.json"
    recovery = json.loads(marker.read_text(encoding="utf-8")) if marker.is_file() else None
    if recovery is not None:
        if (not isinstance(recovery, dict) or recovery.get("input_revision") != revision or
                recovery.get("prune_missing") != prune_missing):
            raise ValueError(f"{marker}: interrupted export used different input or mode; "
                             "restore the previous input before retrying")
        if (not isinstance(recovery.get("changed"), list) or
                not isinstance(recovery.get("retired"), list) or
                not isinstance(recovery.get("outputs"), dict) or
                any(not isinstance(name, str) or Path(name).name != name
                    for name in recovery["changed"] + recovery["retired"])):
            raise ValueError(f"{marker}: invalid interrupted export marker")
        archive_path = Path(recovery["archive"]) if recovery.get("archive") else None
        if archive_path is not None and not archive_path.is_dir():
            raise ValueError(f"{marker}: recovery archive is missing: {archive_path}")
        recovery_affected = set(recovery["changed"]) | set(recovery["retired"])
    else:
        archive_path = None
        recovery_affected = set()
    tables = fields = 0
    with tempfile.TemporaryDirectory(prefix="master-export-", dir=stage_parent) as temporary:
        stage = Path(temporary)
        generated = set()
        observed = set()
        for path in input_files:
            records = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(records, list):
                raise ValueError(f"{path}: expected a list of records")
            observed.add(path.name)
            if recovery is not None and path.name in recovery_affected:
                old_orig = read_table((archive_path / "orig" / path.name) if archive_path
                                      else stage / "missing" / path.name)
                old_zh = read_table((archive_path / "zh-Hans" / path.name) if archive_path
                                    else stage / "missing" / path.name)
            else:
                old_orig = read_table(orig_dir / path.name)
                old_zh = read_table(zh_dir / path.name)
            validate_keyed_table(path.stem, old_orig)
            validate_keyed_table(path.stem, old_zh)
            validate_field_table(path.stem, old_orig)
            validate_field_table(path.stem, old_zh)
            source, translated = {}, {}
            has_text = False
            for index, record in enumerate(records):
                if not isinstance(record, dict):
                    raise ValueError(f"{path}: record {index} is not an object")
                selected = list(text_fields(record))
                has_text = has_text or bool(selected)
                unclassified = unclassified_fields(record, path.stem, selected)
                if unclassified:
                    raise ValueError(f"{path}: record {index} has unclassified Japanese "
                                     f"string fields {unclassified[:8]}; review and update "
                                     "the MasterDB text extractor")
            # Resolve every row, including non-text rows, so a duplicate key or
            # a missing rule cannot silently overwrite a reviewed translation.
            identified = index_records(path.stem, records) if old_orig or has_text else {}
            for record_id, record in identified.items():
                selected = list(text_fields(record))
                field_paths = FieldPaths(path.stem, record)
                for positional_path, original in selected:
                    field_path = field_paths.canonicalize(positional_path)
                    if original in names:
                        continue
                    if (field_path == "name" or field_path.endswith(".name")) \
                            and original in first_names:
                        continue
                    if (path.stem.lower() in {"character", "idol", "npc"}
                            and (field_path == "name" or field_path.endswith(".name"))):
                        continue
                    if path.stem.lower() == "charactergroup" and field_path == "name":
                        continue
                    slot = source.setdefault(record_id, {})
                    if field_path in slot:
                        raise ValueError(f"{path}: duplicate {record_id}/{field_path}")
                    slot[field_path] = original
                    previous = old_zh.get(record_id, {}).get(field_path, "")
                    if old_orig.get(record_id, {}).get(field_path) != original:
                        previous = ""
                    # Music production credits are proper names. Keep their
                    # original spelling in the review JSON and in the game.
                    target = (original if field_path in MUSIC_CREDIT_FIELDS
                              else previous or seeds.get(original, ""))
                    if target:
                        validate_field(original, target, f"{path.stem}:{record_id}:{field_path}",
                                       allow_line_reflow=path.stem == "Story" and field_path == "name")
                        translated.setdefault(record_id, {})[field_path] = target
                    fields += 1
            if source:
                write_json(stage / "orig" / path.name, source)
                write_json(stage / "zh-Hans" / path.name, translated)
                generated.add(path.name)
                tables += 1
        if prune_missing and not generated:
            raise ValueError("Full MasterDB export produced no text tables; refusing to prune")
        if (prune_missing and existing and len(generated) * 2 < len(existing)
                and not allow_large_prune):
            raise ValueError("Full MasterDB export would remove over half the text tables; "
                             "inspect the dump before using --allow-large-prune")
        retired = (existing if prune_missing else existing & observed) - generated
        changed = {name for name in generated if (
            _different(orig_dir / name, stage / "orig" / name) or
            _different(zh_dir / name, stage / "zh-Hans" / name))}
        affected = sorted(changed | retired)
        current_input_files = sorted(path for path in input_dir.glob("*.json")
                                     if path.name != fetch_state.name)
        if (current_input_files != input_files or
                _input_revision(input_files, seed_dictionary, name_glossary) != revision or
                _optional_digest(fetch_state) != fetch_state_digest or
                (input_dir / ".master-fetch-in-progress.json").exists()):
            raise ValueError("MasterDB raw tables or export inputs changed during export; retry")
        if recovery is None:
            current_existing = (set(path.name for path in orig_dir.glob("*.json")) |
                                set(path.name for path in zh_dir.glob("*.json")))
            if (current_existing != existing or
                    _table_snapshot(orig_dir, zh_dir, existing) != live_before):
                raise ValueError("MasterDB source or translation JSON changed during export; retry")
        else:
            unaffected = existing - recovery_affected
            if _table_snapshot(orig_dir, zh_dir, unaffected) != {
                    name: live_before[name] for name in unaffected}:
                raise ValueError("MasterDB source or translation JSON changed during recovery; retry")
            for name in recovery_affected:
                target = recovery["outputs"].get(name)
                if not isinstance(target, dict):
                    raise ValueError(f"{marker}: missing output hashes for {name}")
                for directory, label in ((orig_dir, "orig"), (zh_dir, "zh-Hans")):
                    desired = target.get(label)
                    prior = _optional_digest((archive_path / label / name) if archive_path
                                             else stage / "missing" / name)
                    current = _optional_digest(directory / name)
                    if current not in (prior, desired):
                        raise ValueError(f"{directory / name}: changed outside interrupted export")
        if recovery is not None and not set(affected).issubset(recovery_affected):
            raise ValueError(f"{marker}: recovery changed unexpected tables; inspect archive")
        to_archive = [name for name in affected if (orig_dir / name).is_file() or
                      (zh_dir / name).is_file()]
        if to_archive and recovery is None:
            archive_base = archive_dir or (orig_dir.parent / ".master-archive")
            archive_base.mkdir(parents=True, exist_ok=True)
            archive_name = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + \
                uuid.uuid4().hex[:8]
            archive_stage = archive_base / f".pending-{archive_name}"
            archive_stage.mkdir()
            try:
                for name in to_archive:
                    for current, label in ((orig_dir / name, "orig"),
                                           (zh_dir / name, "zh-Hans")):
                        if current.is_file():
                            target = archive_stage / label / name
                            target.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copy2(current, target)
                write_json(archive_stage / "manifest.json", {
                    "changed": sorted(changed), "retired": sorted(retired)})
                archive_path = (archive_base / archive_name).resolve()
                os.replace(archive_stage, archive_path)
            finally:
                if archive_stage.exists():
                    shutil.rmtree(archive_stage)
        if affected:
            if recovery is None:
                outputs = {name: {"orig": _file_digest(stage / "orig" / name)
                                  if name in changed else None,
                                  "zh-Hans": _file_digest(stage / "zh-Hans" / name)
                                  if name in changed else None}
                           for name in affected}
                write_json(marker, {"changed": sorted(changed),
                                    "retired": sorted(retired),
                                    "outputs": outputs,
                                    "archive": str(archive_path) if archive_path else None,
                                    "input_revision": revision,
                                    "prune_missing": prune_missing})
            for name in sorted(changed):
                _atomic_copy(stage / "orig" / name, orig_dir / name)
                _atomic_copy(stage / "zh-Hans" / name, zh_dir / name)
            for name in sorted(retired):
                (orig_dir / name).unlink(missing_ok=True)
                (zh_dir / name).unlink(missing_ok=True)
        write_json(orig_dir.parent / "record-keys.json", rules_metadata())
        write_json(orig_dir.parent / "field-keys.json", field_rules_metadata())
        marker.unlink(missing_ok=True)
    return tables, fields


def iter_fields(orig_dir: Path, zh_dir: Path):
    require_ready(orig_dir.parent)
    source_tables = {path.name for path in orig_dir.glob("*.json")}
    orphan_tables = sorted(path.name for path in zh_dir.glob("*.json")
                           if path.name not in source_tables)
    if orphan_tables:
        raise ValueError(f"MasterDB translations without source tables: {orphan_tables[:5]}")
    for path in sorted(orig_dir.glob("*.json")):
        target_path = zh_dir / path.name
        if not target_path.is_file():
            raise ValueError(f"Missing MasterDB translation table: {target_path}")
        orig = read_table(path)
        translated = read_table(target_path)
        validate_keyed_table(path.stem, orig)
        validate_keyed_table(path.stem, translated)
        validate_field_table(path.stem, orig)
        validate_field_table(path.stem, translated)
        for record_id, fields in translated.items():
            for field_path in fields:
                if field_path not in orig.get(record_id, {}):
                    raise ValueError(f"{path.stem}:{record_id}:{field_path}: "
                                     "translation has no source field")
        for record_id, fields in orig.items():
            for field_path, source in fields.items():
                target = translated.get(record_id, {}).get(field_path, "")
                if target:
                    validate_field(source, target, f"{path.stem}:{record_id}:{field_path}",
                                   allow_line_reflow=path.stem == "Story" and field_path == "name")
                yield path.stem, record_id, field_path, source, target


def plan(orig_dir: Path, zh_dir: Path):
    total = done = 0
    pending = set()
    for _, _, _, source, target in iter_fields(orig_dir, zh_dir):
        total += 1
        if target:
            done += 1
        else:
            pending.add(source)
    return total, done, len(pending)


def build(orig_dir: Path, zh_dir: Path, output: Path):
    translations = defaultdict(set)
    flat_short_profiles = defaultdict(set)
    story_headers = defaultdict(set)
    for table, record_id, field_path, source, target in iter_fields(orig_dir, zh_dir):
        if target:
            translations[source].add(target)
            if table == "Character" and field_path == "shortProfile" and "\n" in source:
                # The dialogue log removes authored line breaks from a
                # character's short profile before displaying it.
                flat_short_profiles[source.replace("\n", "")].add(
                    target.replace("\n", ""))
            if table == "Story" and field_path == "name":
                # The player composes its overlay header *after* the normal
                # title lookup, e.g. "第28话 この世界に生まれた瞬間の声".
                episode = re.search(r"-(\d+)(?:-limited)?$", record_id)
                if episode:
                    number = int(episode.group(1))
                    original = source.replace("\r", "").replace("\n", "")
                    translated = target.replace("\r", "").replace("\n", "")
                    story_headers[f"第{number}话 {original}"].add(
                        f"第{number}话 {translated}")
    unique = {source: next(iter(values)) for source, values in translations.items()
              if len(values) == 1 and source not in values}
    conflicts = {source: sorted(values) for source, values in translations.items()
                 if len(values) > 1}
    alias_candidates = defaultdict(set)
    for group in (flat_short_profiles, story_headers):
        for source, values in group.items():
            alias_candidates[source].update(values)
    aliases = {source: next(iter(values)) for source, values in alias_candidates.items()
               if len(values) == 1 and source not in unique and source not in conflicts
               and (source not in translations or values == translations[source])}
    write_json(output, {"text": unique, "aliases": aliases,
                        "conflicts": conflicts})
    return len(unique), len(conflicts)


def keyed_index(orig_dir: Path, zh_dir: Path) -> dict:
    """Compile validated, source-guarded translations by MasterDB record and field.

    Unchanged reviewed values are retained: a global source-string replacement
    cannot distinguish a translated singer from a Japanese music credit.
    Empty values have no decision and are omitted from the index.
    """
    names = {path.name for path in orig_dir.glob("*.json")}
    snapshots = _table_snapshot(orig_dir, zh_dir, names)
    tables = {}
    source_targets = defaultdict(set)
    source_counts = defaultdict(int)
    total = retained = indexed = 0
    for table, record_id, field_path, source, target in iter_fields(orig_dir, zh_dir):
        total += 1
        if not target:
            continue
        indexed += 1
        retained += target == source
        tables.setdefault(table, {}).setdefault(record_id, {})[field_path] = [source, target]
        source_targets[source].add(target)
        source_counts[source] += 1
    if (names != {path.name for path in orig_dir.glob("*.json")} or
            snapshots != _table_snapshot(orig_dir, zh_dir, names)):
        raise ValueError("MasterDB JSON changed while compiling the keyed index; retry")
    contextual = {source for source, targets in source_targets.items() if len(targets) > 1}
    return {
        "format": 1,
        "record_keys": rules_metadata(),
        "field_keys": field_rules_metadata(),
        "source_files": snapshots,
        "total_fields": total,
        "indexed_fields": indexed,
        "retained_fields": retained,
        "contextual_sources": len(contextual),
        "contextual_fields": sum(source_counts[source] for source in contextual),
        "tables": tables,
    }


def build_keyed(orig_dir: Path, zh_dir: Path, output: Path, *, verify: bool = False) -> dict:
    result = keyed_index(orig_dir, zh_dir)
    if verify:
        if not output.is_file() or json.loads(output.read_text(encoding="utf-8")) != result:
            raise ValueError(f"{output}: keyed MasterDB index is missing or stale")
    else:
        write_json(output, result)
    return {key: result[key] for key in ("total_fields", "indexed_fields",
                                        "retained_fields", "contextual_sources",
                                        "contextual_fields")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("export", "plan", "build", "build-keyed"):
        item = sub.add_parser(command)
        item.add_argument("--orig-dir", type=Path, default=ORIG)
        item.add_argument("--zh-dir", type=Path, default=ZH)
        if command == "export":
            item.add_argument("--input-dir", type=Path, required=True)
            item.add_argument("--seed-dictionary", type=Path)
            item.add_argument("--name-glossary", type=Path,
                              default=ROOT / "translations/glossary/names.json")
            item.add_argument("--prune-missing", action="store_true",
                              help="Archive and remove old tables absent from a complete MasterDB dump")
            item.add_argument("--allow-large-prune", action="store_true",
                              help="Allow unusually large table removal after inspecting the raw dump")
            item.add_argument("--archive-dir", type=Path,
                              help="Location for backups of replaced or retired JSON tables")
        if command == "build":
            item.add_argument("--output", type=Path,
                              default=ROOT / ".analysis/compiled/master-text.json")
        if command == "build-keyed":
            item.add_argument("--output", type=Path,
                              default=ROOT / ".analysis/master-keyed.json")
            item.add_argument("--verify", action="store_true",
                              help="Check the existing index without writing it")
        if command == "plan":
            item.add_argument("--require-complete", action="store_true",
                              help="Fail when any exported MasterDB field has no reviewed value")
    args = parser.parse_args()
    if args.command == "export":
        if args.allow_large_prune and not args.prune_missing:
            parser.error("--allow-large-prune requires --prune-missing")
        tables, fields = export(args.input_dir, args.orig_dir, args.zh_dir,
                                args.seed_dictionary, args.name_glossary,
                                prune_missing=args.prune_missing,
                                allow_large_prune=args.allow_large_prune,
                                archive_dir=args.archive_dir)
        print(f"Exported {fields} fields from {tables} MasterDB tables to JSON")
    elif args.command == "plan":
        total, done, pending = plan(args.orig_dir, args.zh_dir)
        print(f"{done}/{total} MasterDB fields have values; "
              f"{pending} unique sources pending")
        if args.require_complete and (not total or done != total):
            raise SystemExit("MasterDB field decisions are incomplete")
    elif args.command == "build-keyed":
        counts = build_keyed(args.orig_dir, args.zh_dir, args.output, verify=args.verify)
        print(f"Keyed MasterDB index {'verified' if args.verify else 'compiled'}: {counts}")
    else:
        unique, conflicts = build(args.orig_dir, args.zh_dir, args.output)
        print(f"Compiled {unique} unambiguous strings; {conflicts} conflicts")


if __name__ == "__main__":
    main()
