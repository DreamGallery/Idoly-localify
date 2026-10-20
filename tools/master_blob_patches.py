#!/usr/bin/env python3
"""Build local, source-guarded protobuf Blob patches from reviewed MasterDB JSON.

The binary contains original game records and must stay in Git-ignored local
output. The runtime only substitutes a Blob when every original byte matches.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile

from localize import write_json
from master_json import ORIG, ROOT, ZH, iter_fields, require_ready
from master_identity import record_key, rules_metadata
from master_field_paths import FieldPaths, field_rules_metadata


MAGIC = b"IDOLYMB2"
TABLE_NAME = re.compile(r"[A-Za-z_][A-Za-z_0-9]*\Z")


def file_digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def encrypted_pack(pack, version: str, cache_dir: Path, requests_module,
                   *, offline: bool = False) -> Path:
    """Reuse only a byte-identical locally verified download for this MasterTag pack."""
    identity = json.dumps({"version": str(version), "table": pack.type,
                           "url": pack.downloadUrl, "size": pack.fileSize,
                           "key": pack.cryptoKey}, sort_keys=True)
    fingerprint = hashlib.sha256(identity.encode("utf-8")).hexdigest()
    directory = cache_dir / pack.type
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / f"{fingerprint}.db"
    digest_path = directory / f"{fingerprint}.sha256"
    if target.is_file() and digest_path.is_file():
        try:
            saved = digest_path.read_text(encoding="ascii").strip()
            if (re.fullmatch(r"[0-9a-f]{64}", saved) and
                    target.stat().st_size == pack.fileSize and file_digest(target) == saved):
                return target
        except (OSError, UnicodeError):
            pass
    if offline:
        raise ValueError(f"{pack.type}: verified encrypted MasterDB cache is missing")
    response = requests_module.get(pack.downloadUrl, timeout=60)
    response.raise_for_status()
    if len(response.content) != pack.fileSize:
        raise ValueError(f"{pack.type}: download length differs from MasterTag")
    with tempfile.NamedTemporaryFile(dir=directory, prefix=".master-db-",
                                     delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(response.content)
            stream.flush()
            os.fsync(stream.fileno())
            digest = file_digest(temporary)
            os.replace(temporary, target)
            # A crash between the two replacements invalidates this cache entry.
            with tempfile.NamedTemporaryFile("w", encoding="ascii", dir=directory,
                                             prefix=".master-hash-", delete=False) as hashes:
                temp_hash = Path(hashes.name)
                hashes.write(digest + "\n")
                hashes.flush()
                os.fsync(hashes.fileno())
            try:
                os.replace(temp_hash, digest_path)
            finally:
                temp_hash.unlink(missing_ok=True)
        finally:
            temporary.unlink(missing_ok=True)
    return target


def input_snapshot(master_tag: Path, orig_dir: Path, zh_dir: Path,
                   tables: set[str]) -> dict:
    require_ready(orig_dir.parent)
    return {"master_tag": file_digest(master_tag), "record_keys": rules_metadata(),
            "field_keys": field_rules_metadata(),
            "tables": {table: {"orig": file_digest(orig_dir / f"{table}.json"),
                               "zh-Hans": file_digest(zh_dir / f"{table}.json")}
                       for table in sorted(tables)}}


def field_slot(message, path: str, table: str = ""):
    """Return (owner, field name or list index, current string)."""
    return FieldPaths(table, message).locate(path)[:3]


def patch_blob(blob: bytes, message_type, record_id: str,
               fields: dict[str, tuple[str, str]], label: str) -> bytes:
    message = message_type.FromString(blob)
    if message.SerializeToString() != blob:
        raise ValueError(f"{label}: protobuf round-trip changed original bytes")
    paths = FieldPaths(label, message)
    for field_path, (source, target) in fields.items():
        owner, key, current, _ = paths.locate(field_path)
        if current != source:
            raise ValueError(f"{label}:{record_id}:{field_path}: MasterDB source changed")
        if target and target != source:
            if isinstance(key, int):
                owner[key] = target
            else:
                setattr(owner, key, target)
    return message.SerializeToString()


def decisions(orig_dir: Path, zh_dir: Path, selected: set[str]) -> dict:
    result = defaultdict(dict)
    for table, record_id, field_path, source, target in iter_fields(orig_dir, zh_dir):
        if table in selected:
            result[table].setdefault(record_id, {})[field_path] = (source, target)
    return result


def retained_conflicts(reviewed: dict) -> list[str]:
    targets = defaultdict(set)
    for records in reviewed.values():
        for fields in records.values():
            for source, target in fields.values():
                if target:
                    targets[source].add(target)
    return sorted(source for source, values in targets.items()
                  if source in values and len(values) > 1)


def compile_patches(master_tag: Path, solis_dir: Path, orig_dir: Path,
                    zh_dir: Path, tables: set[str], output: Path,
                    cache_dir: Path | None = None, *, offline: bool = False) -> dict:
    if not tables or any(not TABLE_NAME.fullmatch(name) for name in tables):
        raise ValueError("Select one or more valid MasterDB table names")
    output = output.resolve()
    private_roots = (ROOT / ".agent-local", ROOT / ".analysis")
    if not any(directory in output.parents for directory in private_roots):
        raise ValueError("Blob patches contain game resources; output must stay in .agent-local or .analysis")
    output.parent.mkdir(parents=True, exist_ok=True)
    cache_dir = (cache_dir or ROOT / ".agent-local/master-encrypted-cache").resolve()
    if not any(directory in cache_dir.parents for directory in private_roots):
        raise ValueError("Encrypted MasterDB cache must stay in .agent-local or .analysis")
    sys.path.insert(0, str(solis_dir.resolve()))
    import pmaster_pb2
    import ptransaction_pb2
    import requests
    from sqlcipher3 import dbapi2 as sqlcipher

    tag = ptransaction_pb2.MasterTag.FromString(master_tag.read_bytes())
    packs = {pack.type: pack for pack in tag.masterTagPacks if pack.type in tables}
    if set(packs) != tables:
        raise ValueError(f"MasterTag lacks tables: {sorted(tables - set(packs))}")
    sources = input_snapshot(master_tag, orig_dir, zh_dir, tables)
    reviewed = decisions(orig_dir, zh_dir, tables)
    complete = tables == {path.stem for path in orig_dir.glob("*.json")}
    suppressed = retained_conflicts(reviewed) if complete else []
    digest_to_target = {}
    total_patches = 0
    counts = {}
    with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".master-blobs-",
                                     delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(MAGIC + b"\0\0\0\0" + struct.pack("<I", len(suppressed)))
        try:
            for table in sorted(tables):
                if not hasattr(pmaster_pb2, table):
                    raise ValueError(f"No protobuf class for {table}")
                pack = packs[table]
                encrypted = encrypted_pack(pack, tag.version, cache_dir, requests,
                                           offline=offline)
                connection = sqlcipher.connect(str(encrypted))
                try:
                    connection.execute('pragma key="x\'%s\'"' % pack.cryptoKey)
                    seen = set()
                    rows = patches = 0
                    for (blob,) in connection.execute(f"select data from {table}"):
                        message_type = getattr(pmaster_pb2, table)
                        message = message_type.FromString(blob)
                        record_id = record_key(table, message)
                        if record_id in seen:
                            raise ValueError(f"{table}: duplicate record ID {record_id}")
                        seen.add(record_id)
                        fields = reviewed.get(table, {}).get(record_id, {})
                        translated = patch_blob(blob, message_type, record_id,
                                                fields, table)
                        if translated != blob:
                            identity = hashlib.sha256(blob).digest()
                            target_hash = hashlib.sha256(translated).digest()
                            prior = digest_to_target.get(identity)
                            if prior is not None and prior != target_hash:
                                raise ValueError(f"{table}:{record_id}: identical Blob has "
                                                 "conflicting translations")
                            if prior is None:
                                stream.write(struct.pack("<II", len(blob), len(translated)))
                                stream.write(blob)
                                stream.write(translated)
                                digest_to_target[identity] = target_hash
                                total_patches += 1
                            patches += 1
                        rows += 1
                    missing = set(reviewed.get(table, {})) - seen
                    if missing:
                        raise ValueError(f"{table}: reviewed records absent from DB: "
                                         f"{sorted(missing)[:5]}")
                    counts[table] = {"rows": rows, "changed_rows": patches}
                    print(f"{table}: {patches}/{rows} changed rows", flush=True)
                finally:
                    connection.close()
            for source in suppressed:
                encoded = source.encode("utf-8")
                if not encoded or len(encoded) > 4096:
                    raise ValueError("Invalid contextual MasterDB source length")
                stream.write(struct.pack("<I", len(encoded)))
                stream.write(encoded)
            stream.seek(len(MAGIC))
            stream.write(struct.pack("<I", total_patches))
            stream.flush()
            os.fsync(stream.fileno())
            if complete and tables != {path.stem for path in orig_dir.glob("*.json")}:
                raise ValueError("MasterDB table set changed while building Blob patches; retry")
            if input_snapshot(master_tag, orig_dir, zh_dir, tables) != sources:
                raise ValueError("MasterDB inputs changed while building Blob patches; retry")
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    os.replace(temporary, output)
    result = {"format": 2, "master_tag_version": tag.version,
              "patches": total_patches, "tables": counts,
              "suppressed_global_text_sources": suppressed,
              "sources_sha256": sources,
              "binary_sha256": file_digest(output)}
    write_json(output.with_suffix(".json"), result)
    return result


def verify_patches(master_tag: Path, orig_dir: Path, zh_dir: Path,
                   tables: set[str], output: Path) -> dict:
    manifest = output.with_suffix(".json")
    if not output.is_file() or not manifest.is_file():
        raise ValueError("Master Blob patch or manifest is missing; build it first")
    data = json.loads(manifest.read_text(encoding="utf-8"))
    if (data.get("format") != 2 or
            data.get("sources_sha256") != input_snapshot(master_tag, orig_dir, zh_dir, tables) or
            data.get("binary_sha256") != file_digest(output)):
        raise ValueError("Master Blob patch or inputs changed; rebuild before deployment")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--master-tag", type=Path, required=True)
    parser.add_argument("--solis-dir", type=Path)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--table", action="append")
    selection.add_argument("--all-tables", action="store_true",
                           help="Use every reviewed table in --orig-dir")
    parser.add_argument("--output", type=Path,
                        default=ROOT / ".analysis/master-blobs.bin")
    parser.add_argument("--cache-dir", type=Path,
                        default=ROOT / ".analysis/master-encrypted-cache",
                        help="Git-ignored cache of original encrypted MasterDB tables")
    parser.add_argument("--offline", action="store_true",
                        help="Rebuild only from verified cached encrypted tables")
    parser.add_argument("--verify", action="store_true",
                        help="Check source and binary hashes without downloading")
    args = parser.parse_args()
    tables = ({path.stem for path in args.orig_dir.glob("*.json")}
              if args.all_tables else set(args.table))
    if args.verify:
        result = verify_patches(args.master_tag, args.orig_dir, args.zh_dir,
                                tables, args.output)
        print(f"Verified {result['patches']} MasterDB Blob patches across {len(tables)} tables")
    else:
        if args.solis_dir is None:
            parser.error("--solis-dir is required when building Blob patches")
        result = compile_patches(args.master_tag, args.solis_dir, args.orig_dir,
                                 args.zh_dir, tables, args.output, args.cache_dir,
                                 offline=args.offline)
        print(f"Compiled {result['patches']} MasterDB Blob patches across {len(tables)} tables")


if __name__ == "__main__":
    main()
