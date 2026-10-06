"""Check text/font compatibility against the APK actually installed on a device.

Public APKs and the installed module font bundle are copied. Caches use SHA-256;
external dictionaries are not evidence of the installed font payload.
"""
from __future__ import annotations

import hashlib
from io import BytesIO
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
from zipfile import ZipFile

from font_policy import INDEPENDENT_FONT_MODES, SUPPORTED_REPLACEMENT_FAMILIES, validate_font_pair
from font_coverage import audit_codepoints, require_coverage, source_fields, translated_fields

PACKAGE = "game.qualiarts.idolypride"
CACHE_SCHEMA = 2
CACHE_DIRECTORY = "font-payload-cache-v2"
BUNDLE_PATH = f"/sdcard/Android/data/{PACKAGE}/files/idoly-localify/resource-han-rounded.bundle"


def file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def inspect_payload(base: Path) -> dict:
    import UnityPy
    from fontTools.ttLib import TTFont
    from patch_font import FONT_NAME, INDEPENDENT_FONT_NAME, split_names, verify_independent_dependencies

    with ZipFile(base) as apk:
        if "assets/lspatch/origin.apk" in apk.namelist():
            with ZipFile(BytesIO(apk.read("assets/lspatch/origin.apk"))) as original:
                serialized = b"".join(original.read(name) for name in split_names(original))
        else:
            serialized = b"".join(apk.read(name) for name in split_names(apk))
    environment = UnityPy.load(serialized)
    fonts = [obj.read() for obj in environment.objects if obj.type.name == "Font"]
    legacy = [font for font in fonts if font.m_Name == FONT_NAME]
    independent = [font for font in fonts if font.m_Name == INDEPENDENT_FONT_NAME]
    if len(legacy) != 1 or len(independent) > 1:
        raise ValueError("Installed APK has an unrecognized font layout")

    def is_replacement(font) -> bool:
        with TTFont(BytesIO(bytes(font.m_FontData))) as face:
            families = {face['name'].getDebugName(i) for i in (1, 16)}
        return bool(families.intersection(SUPPORTED_REPLACEMENT_FAMILIES))

    if independent:
        if not is_replacement(independent[0]) or is_replacement(legacy[0]):
            raise ValueError("Independent font APK does not preserve the original font")
        verify_independent_dependencies(environment, bytes(independent[0].m_FontData))
        patch_mode, selected = "add-font", independent[0]
    else:
        patch_mode = "replace-fot" if is_replacement(legacy[0]) else "original"
        selected = legacy[0]
    data = bytes(selected.m_FontData)
    with TTFont(BytesIO(data)) as font:
        cmap = font.getBestCmap()
    if not cmap:
        raise ValueError("Installed Chinese font has no Unicode cmap")
    return {"patch_mode": patch_mode, "font_sha256": hashlib.sha256(data).hexdigest(),
            "codepoints": sorted(cmap)}


def inspect_bundle(path: Path) -> dict:
    import UnityPy
    from fontTools.ttLib import TTFont
    from build_font_bundle import verify
    from patch_font import INDEPENDENT_FONT_NAME

    raw = path.read_bytes()
    environment = UnityPy.load(raw)
    fonts = [obj.read() for obj in environment.objects if obj.type.name == "Font"]
    if len(fonts) != 1 or fonts[0].m_Name != INDEPENDENT_FONT_NAME:
        raise ValueError("Module bundle must contain exactly one supported font")
    data = bytes(fonts[0].m_FontData)
    verify(raw, data)
    with TTFont(BytesIO(data)) as font:
        families = {font['name'].getDebugName(i) for i in (1, 16)}
        cmap = font.getBestCmap()
    if not families.intersection(SUPPORTED_REPLACEMENT_FAMILIES) or not cmap:
        raise ValueError("Module bundle has no supported replacement Unicode font")
    return {"patch_mode": "module-font", "font_sha256": hashlib.sha256(data).hexdigest(),
            "codepoints": sorted(cmap)}


def bundle_digest(adb_call, adb_bin: str, serial: str) -> str:
    try:
        result = adb_call(adb_bin, serial, "shell",
                          f"sha256sum {shlex.quote(BUNDLE_PATH)}").strip()
    except subprocess.CalledProcessError as error:
        raise ValueError("No installed module font bundle; launch the game with the module "
                         "enabled before deploying text") from error
    match = re.fullmatch(r"([0-9a-f]{64})\s+(.+)", result)
    if not match or match[2] != BUNDLE_PATH:
        raise ValueError("Cannot verify installed module font bundle; launch the game with "
                         "the module enabled before deploying text")
    return match[1]


def installed_bundle_payload(adb_call, adb_bin: str, serial: str,
                             cache: Path, apk_digest: str) -> dict:
    digest = bundle_digest(adb_call, adb_bin, serial)
    record_path = cache / f"{apk_digest}-{digest}.json"
    if record_path.exists():
        payload = json.loads(record_path.read_text(encoding="utf-8"))
        validate_record(payload, apk_digest, record_path)
        if payload.get("bundle_sha256") != digest or payload["patch_mode"] != "module-font":
            raise ValueError(f"Invalid module font cache: {record_path}")
    else:
        with tempfile.TemporaryDirectory(prefix="idoly-bundle-check-") as temporary:
            bundle = Path(temporary) / "resource-han-rounded.bundle"
            subprocess.run([adb_bin, "-s", serial, "pull", BUNDLE_PATH, str(bundle)],
                           check=True, capture_output=True)
            if file_digest(bundle) != digest:
                raise ValueError("Installed module font changed during inspection; retry")
            payload = {"schema_version": CACHE_SCHEMA, "apk_sha256": apk_digest,
                       "bundle_sha256": digest, **inspect_bundle(bundle)}
            validate_record(payload, apk_digest, record_path)
        if bundle_digest(adb_call, adb_bin, serial) != digest:
            raise ValueError("Installed module font changed during inspection; retry")
        record_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return payload


def installed_digest(adb_call, adb_bin: str, serial: str) -> tuple[str, str]:
    output = adb_call(adb_bin, serial, "shell", "pm", "path", PACKAGE)
    paths = [line.removeprefix("package:").strip() for line in output.splitlines()
             if line.startswith("package:")]
    bases = [path for path in paths if path.endswith("/base.apk")]
    if len(bases) != 1 or not re.fullmatch(r"/data/app/[A-Za-z0-9_=/+.~\-]+/base\.apk", bases[0]):
        raise ValueError("Cannot identify the installed game base APK")
    path = bases[0]
    result = adb_call(adb_bin, serial, "shell", f"sha256sum {shlex.quote(path)}").strip()
    match = re.fullmatch(r"([0-9a-f]{64})\s+(.+)", result)
    if not match or match[2] != path:
        raise ValueError("Cannot verify installed game APK hash")
    return path, match[1]


def validate_record(record: dict, digest: str, path: Path) -> None:
    points = record.get("codepoints") if isinstance(record, dict) else None
    if (not isinstance(record, dict) or record.get("schema_version") != CACHE_SCHEMA or
            record.get("apk_sha256") != digest or
            record.get("patch_mode") not in ("add-font", "replace-fot", "original", "module-font") or
            not isinstance(record.get("font_sha256"), str) or
            not re.fullmatch(r"[0-9a-f]{64}", record["font_sha256"]) or
            not isinstance(points, list) or not points or
            any(type(point) is not int or not 0 <= point <= 0x10ffff for point in points) or
            points != sorted(set(points))):
        raise ValueError(f"Invalid font payload cache: {path}")


def assert_compatible(adb_call, adb_bin: str, serial: str, root: Path,
                      dictionary: Path) -> str | None:
    value = json.loads(dictionary.read_text(encoding="utf-8"))
    mode = value.get("font_mode", "fallback")
    enabled = value.get("font", True)
    if type(enabled) is not bool:
        raise ValueError("Invalid font enabled flag")
    if not enabled:
        validate_font_pair(mode, "replace-fot", font_enabled=False)
        return None
    remote_path, digest = installed_digest(adb_call, adb_bin, serial)
    cache = root / ".analysis" / CACHE_DIRECTORY
    cache.mkdir(parents=True, exist_ok=True)
    record_path = cache / f"{digest}.json"
    if record_path.exists():
        record = json.loads(record_path.read_text(encoding="utf-8"))
        validate_record(record, digest, record_path)
        if record["patch_mode"] == "module-font":
            raise ValueError(f"Module font cannot be cached by APK alone: {record_path}")
        payload = record
    else:
        with tempfile.TemporaryDirectory(prefix="idoly-font-check-") as temporary:
            base = Path(temporary) / "base.apk"
            subprocess.run([adb_bin, "-s", serial, "pull", remote_path, str(base)],
                           check=True, capture_output=True)
            if file_digest(base) != digest:
                raise ValueError("Installed APK changed during font inspection; retry")
            try:
                payload = inspect_payload(base)
            except (KeyError, OSError) as error:
                raise ValueError("Installed APK has no supported Unity font layout") from error
        if installed_digest(adb_call, adb_bin, serial) != (remote_path, digest):
            raise ValueError("Installed APK changed during font inspection; retry")
        record = {"schema_version": CACHE_SCHEMA, "apk_sha256": digest, **payload}
        validate_record(record, digest, record_path)
        record_path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    # Current independent routing only loads the module bundle, including on
    # older games that still contain an embedded replacement font.
    if mode in INDEPENDENT_FONT_MODES or payload["patch_mode"] == "original":
        payload = installed_bundle_payload(adb_call, adb_bin, serial, cache, digest)
        if installed_digest(adb_call, adb_bin, serial) != (remote_path, digest):
            raise ValueError("Installed APK changed during font inspection; retry")
    patch_mode = payload["patch_mode"]
    try:
        validate_font_pair(mode, patch_mode)
    except ValueError as error:
        raise ValueError(f"Text font mode {mode!r} is incompatible with installed {patch_mode!r} APK; "
                         "install the matching APK before deploying text") from error
    # Recheck the current corpus and the actual deployment dictionary on every
    # call, even for an unchanged APK. New translations can require new glyphs.
    def fields():
        yield from translated_fields(root)
        yield from source_fields(root)
        yield "deployment-dictionary", dictionary.read_text(encoding="utf-8")
    require_coverage(audit_codepoints(set(payload["codepoints"]), fields()))
    return patch_mode
