#!/usr/bin/env python3
"""Package verified runtime text for a static HTTPS hot-update host.

Dictionary JSON is always included; notice JSON and story TXT are optional.
Content-addressed objects are written before manifest.json, so a previously
published manifest remains valid when preparing a new release.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

from numeric_text_templates import compile_templates
from home_activity_names import compile_names
from urllib.parse import urlsplit

from pipeline import Pipeline
from font_policy import required_patch_mode


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / ".analysis/text-update-release"
ASSETS = {
    "dict.json": ("app/src/main/assets/idoly-localify/dict.json", 64 * 1024 * 1024),
    "web-notice.json": ("app/src/main/assets/idoly-localify/web-notice.json", 32 * 1024 * 1024),
}
MAX_MANIFEST = 4 * 1024 * 1024
MAX_STORY = 8 * 1024 * 1024
MAX_FILES = 8192
MAX_TOTAL = 1024 * 1024 * 1024
STORY_NAME = re.compile(r"adv_[A-Za-z0-9_-]+\.txt\Z")
NOTICE_PATH = re.compile(r"/notice/[0-9a-f]{64}/index\.html\Z")
NOTICE_MAX_PAGES = 8192
NOTICE_MAX_ENTRIES_PER_PAGE = 2048
NOTICE_MAX_PAGE_BYTES = 1024 * 1024
NOTICE_MAX_TEXT_BYTES = 64 * 1024


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha256(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def validate_base_url(value: str) -> str:
    url = urlsplit(value)
    if (url.scheme != "https" or not url.hostname or url.username is not None or
            url.password is not None or url.query or url.fragment or
            any(ord(ch) < 33 for ch in value) or "\\" in value):
        raise ValueError("--base-url must be a direct HTTPS directory URL without credentials, query or fragment")
    if url.path.endswith("/manifest.json"):
        raise ValueError("--base-url must name the directory containing manifest.json")
    return value.rstrip("/")


def validate_version(value: str) -> str:
    if not value or len(value) > 128 or value != value.strip() or any(ord(ch) < 32 for ch in value):
        raise ValueError("--version must be a nonempty, single-line string of at most 128 characters")
    return value


def validate_json(name: str, data: bytes) -> None:
    try:
        value = json.loads(data.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"{name}: expected UTF-8 JSON text") from error
    if not isinstance(value, dict):
        raise ValueError(f"{name}: expected a JSON object")
    if name == "dict.json":
        allowed = {"dump", "font", "font_mode", "i18n", "i18n_by_source", "text", "translated_values"}
        if (set(value) - {"legal_by_type", "numeric_text_templates", "home_activity_names"} != allowed or
                not all(isinstance(value[key], dict) for key in ("i18n", "i18n_by_source", "text")) or
                not isinstance(value["translated_values"], list) or
                not isinstance(value["dump"], bool) or not isinstance(value["font"], bool) or
                not isinstance(value["font_mode"], str)):
            raise ValueError("dict.json: unexpected runtime dictionary schema")
        if value["dump"]:
            raise ValueError("dict.json: automatic text capture must be disabled for publication")
        required_patch_mode(value["font_mode"])
        numeric = value.get("numeric_text_templates", {})
        if not isinstance(numeric, dict):
            raise ValueError("dict.json: invalid numeric text template schema")
        try:
            compile_templates({source: source for source in numeric}, numeric)
        except ValueError as error:
            raise ValueError("dict.json: invalid numeric text template schema") from error
        names = value.get("home_activity_names", {})
        if not isinstance(names, dict):
            raise ValueError("dict.json: invalid home activity name schema")
        try:
            compile_names({name: name for name in names}, names)
        except ValueError as error:
            raise ValueError("dict.json: invalid home activity name schema") from error
        legal = value.get("legal_by_type", {})
        if not isinstance(legal, dict):
            raise ValueError("dict.json: invalid legal dictionary schema")
        seen = {}
        for key, rule in legal.items():
            if (key not in {str(i) for i in range(1, 8)} or not isinstance(rule, dict)
                    or set(rule) != {"source", "translation"}
                    or any(not isinstance(text, str) or not text or len(text.encode('utf-8')) > 1048576
                           for text in rule.values())):
                raise ValueError("dict.json: invalid legal rule schema")
            if rule["source"] in seen and seen[rule["source"]] != rule["translation"]:
                raise ValueError("dict.json: conflicting legal translations")
            seen[rule["source"]] = rule["translation"]
    elif name == "web-notice.json":
        if (set(value) != {"host", "pages"} or
                value["host"] != "stat.game-idolypride.jp" or
                not isinstance(value["pages"], dict) or
                len(value["pages"]) > NOTICE_MAX_PAGES):
            raise ValueError("web-notice.json: unexpected notice dictionary schema")
        for path, entries in value["pages"].items():
            if (not NOTICE_PATH.fullmatch(path) or not isinstance(entries, dict) or
                    not 0 < len(entries) <= NOTICE_MAX_ENTRIES_PER_PAGE):
                raise ValueError("web-notice.json: invalid page path or entry count")
            if len(json.dumps(entries, ensure_ascii=False, separators=(",", ":")).encode("utf-8")) > NOTICE_MAX_PAGE_BYTES:
                raise ValueError("web-notice.json: page exceeds 1 MiB")
            for original, translated in entries.items():
                if (not isinstance(original, str) or not original or
                        len(original.encode("utf-8")) > NOTICE_MAX_TEXT_BYTES or
                        not isinstance(translated, str) or not translated or
                        len(translated.encode("utf-8")) > NOTICE_MAX_TEXT_BYTES):
                    raise ValueError("web-notice.json: invalid page translation text")


def validated_output(root: Path, output: Path) -> Path:
    lexical_root = root.absolute()
    output = output.absolute()
    if output == lexical_root or lexical_root in output.parents:
        output = root.resolve() / output.relative_to(lexical_root)
    root = root.resolve()
    allowed = root / ".analysis"
    if ".." in output.parts or output == allowed or allowed not in output.parents:
        raise ValueError("output directory must be a child of the project's .analysis directory")
    current = allowed
    if current.is_symlink():
        raise ValueError(f"symlinked output path: {current}")
    for part in output.relative_to(allowed).parts:
        current = current / part
        if current.is_symlink():
            raise ValueError(f"symlinked output path: {current}")
    return output


def atomic_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        raise ValueError(f"refusing symlink: {path}")
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".text-update-", delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def atomic_copy(path: Path, source: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        raise ValueError(f"refusing symlink: {path}")
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".text-update-", delete=False) as stream:
        temporary = Path(stream.name)
        with source.open("rb") as original:
            shutil.copyfileobj(original, stream, length=1024 * 1024)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def snapshot(path: Path, limit: int, label: str, require_utf8: bool = False) -> tuple[Path, str, int]:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"missing or symlinked release input: {path}")
    size = path.stat().st_size
    if size > limit:
        raise ValueError(f"{label} exceeds the app download limit")
    if require_utf8:
        try:
            path.read_text(encoding="utf-8")
        except UnicodeError as error:
            raise ValueError(f"{label}: expected UTF-8 text") from error
    return path, file_sha256(path), size


def build(root: Path, output: Path, base_url: str, version: str | None = None,
          include_notice: bool = True, include_stories: bool = False,
          github_repository: str | None = None, include_master_blobs: bool = False,
          master_tag: Path | None = None) -> dict:
    """Package a byte-for-byte snapshot after the complete pipeline verifies."""
    base_url = validate_base_url(base_url)
    output = validated_output(root, output)
    pipeline = Pipeline(root)
    sources = {}
    for name, (relative, limit) in ASSETS.items():
        if name == "web-notice.json" and not include_notice:
            continue
        path = root / relative
        if path.is_symlink():
            raise ValueError(f"refusing symlinked input: {path}")
        if name == "web-notice.json" and not path.exists():
            continue
        source = snapshot(path, limit, name)
        validate_json(name, path.read_bytes())
        sources[name] = source
    if include_stories:
        story_dir = pipeline.txt_dir
        if story_dir.is_symlink() or not story_dir.is_dir():
            raise ValueError(f"missing or symlinked compiled story directory: {story_dir}")
        names = pipeline.names()
        for stem in names:
            filename = f"{stem}.txt"
            if not STORY_NAME.fullmatch(filename):
                raise ValueError(f"unsafe story resource name: {filename}")
            key = f"resource/{filename}"
            sources[key] = snapshot(story_dir / filename, MAX_STORY, key, require_utf8=True)
        if not names:
            raise ValueError("no verified story resources found")
    if include_master_blobs:
        if master_tag is None:
            raise ValueError("--include-master-blobs requires --master-tag")
        from master_blob_patches import verify_patches
        blob = root / ".analysis/master-blobs.bin"
        orig = root / "translations/master/orig"
        verify_patches(master_tag, orig, root / "translations/master/zh-Hans",
                       {path.stem for path in orig.glob("*.json")}, blob)
        sources["master-blobs.bin"] = snapshot(blob, 256 * 1024 * 1024, "master-blobs.bin")
    if len(sources) > MAX_FILES:
        raise ValueError("release exceeds the app file-count limit")
    if sum(size for _, _, size in sources.values()) > MAX_TOTAL:
        raise ValueError("release exceeds the app total download limit")
    pipeline.run(verify=True)
    if include_stories and pipeline.names() != names:
        raise ValueError("story resource list changed during verification; retry")
    for name, (path, digest, size) in sources.items():
        if path.stat().st_size != size or file_sha256(path) != digest:
            raise ValueError(f"{name} changed during verification; retry")
    if version is None:
        joined = "\n".join(f"{name}:{sources[name][1]}" for name in sorted(sources))
        version = sha256(joined.encode("ascii"))[:16]
    validate_version(version)
    files = {}
    for name, (_, digest, _) in sources.items():
        object_path = f"objects/{digest}/{name}"
        url = f"{base_url}/{object_path}"
        validate_base_url(url)
        files[name] = {"url": url, "sha256": digest}
    manifest = {"version": version, "files": files}
    if github_repository is not None:
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", github_repository):
            raise ValueError("Invalid GitHub release repository")
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", version):
            raise ValueError("GitHub release version must be URL-safe")
        files = {name: {"sha256": digest} for name, (_, digest, _) in sources.items()}
        manifest = {"schema_version": 2, "version": version, "files": files}
    manifest_bytes = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if len(manifest_bytes) > MAX_MANIFEST:
        raise ValueError("manifest exceeds the app download limit")
    output.mkdir(parents=True, exist_ok=True)
    objects_dir = output / "objects"
    if objects_dir.is_symlink():
        raise ValueError(f"refusing symlink: {objects_dir}")
    for name, (source, digest, _) in sources.items():
        object_dir = objects_dir / digest
        if object_dir.is_symlink():
            raise ValueError(f"refusing symlink: {object_dir}")
        destination = object_dir / name
        if destination.parent != object_dir and destination.parent.is_symlink():
            raise ValueError(f"refusing symlink: {destination.parent}")
        if destination.exists():
            if destination.is_symlink() or file_sha256(destination) != digest:
                raise ValueError(f"existing release object differs: {destination}")
        else:
            atomic_copy(destination, source)
        if file_sha256(destination) != digest:
            raise ValueError(f"release object failed checksum: {destination}")
    if include_stories and pipeline.names() != names:
        raise ValueError("story resource list changed during packaging; retry")
    for name, (path, digest, size) in sources.items():
        if path.stat().st_size != size or file_sha256(path) != digest:
            raise ValueError(f"{name} changed during packaging; retry")
    if github_repository is not None:
        archive_path = output / "text-update.zip"
        with tempfile.NamedTemporaryFile(dir=output, delete=False) as stream:
            archive_temp = Path(stream.name)
        try:
            with ZipFile(archive_temp, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
                for name in sorted(sources):
                    source, digest, _ = sources[name]
                    info = ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                    info.compress_type = ZIP_DEFLATED
                    info.external_attr = 0o100644 << 16
                    with archive.open(info, "w") as output_stream, source.open("rb") as input_stream:
                        shutil.copyfileobj(input_stream, output_stream, 1024 * 1024)
            if archive_temp.stat().st_size > MAX_TOTAL:
                raise ValueError("Archive exceeds the app download limit")
            # Reject source changes during compression before exposing any manifest.
            with ZipFile(archive_temp) as archive:
                for name, (_, digest, _) in sources.items():
                    checksum = hashlib.sha256()
                    with archive.open(name) as member:
                        for block in iter(lambda: member.read(1024 * 1024), b""):
                            checksum.update(block)
                    if checksum.hexdigest() != digest:
                        raise ValueError(f"{name} changed while archiving")
            os.replace(archive_temp, archive_path)
        finally:
            archive_temp.unlink(missing_ok=True)
        manifest["archive"] = {
            "url": f"https://github.com/{github_repository}/releases/download/text-{version}/text-update.zip",
            "sha256": file_sha256(archive_path)}
        manifest_bytes = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
        if len(manifest_bytes) > MAX_MANIFEST:
            raise ValueError("manifest exceeds the app download limit")
    # Every object is now complete. Publish the small pointer file last.
    atomic_bytes(output / "manifest.json", manifest_bytes)
    if (output / "manifest.json").read_bytes() != manifest_bytes:
        raise ValueError("manifest readback failed")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url",
                        help="Direct HTTPS URL of the hosted output directory")
    parser.add_argument("--github-repository", help="Publish schema 2 ZIP assets for owner/repository")
    parser.add_argument("--include-master-blobs", action="store_true")
    parser.add_argument("--master-tag", type=Path)
    parser.add_argument("--version", help="Opaque display version; defaults to a content hash")
    parser.add_argument("--omit-notice", action="store_true",
                        help="Publish only dict.json even when the notice asset exists")
    parser.add_argument("--include-stories", action="store_true",
                        help="Also publish every verified adventure TXT under resource/")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT,
                        help="Local release directory under .analysis")
    args = parser.parse_args()
    if not args.base_url and not args.github_repository:
        parser.error("--base-url or --github-repository is required")
    args.base_url = args.base_url or "https://github.com/" + args.github_repository
    try:
        result = build(ROOT, args.output_dir, args.base_url, args.version,
                       include_notice=not args.omit_notice,
                       include_stories=args.include_stories, github_repository=args.github_repository,
                       include_master_blobs=args.include_master_blobs, master_tag=args.master_tag)
    except (OSError, ValueError, KeyError) as error:
        print(f"Text update release failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"manifest": str(args.output_dir / "manifest.json"),
                      "version": result["version"],
                      "file_count": len(result["files"]),
                      "story_count": sum(name.startswith("resource/")
                                         for name in result["files"])},
                     ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
