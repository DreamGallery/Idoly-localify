#!/usr/bin/env python3
"""Compatibility entry point for HoshimiToolkit's MasterDB fetcher."""

from pathlib import Path
import sys

from workspace_paths import load_module

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

try:
    _fetch = load_module("HoshimiToolkit", "src.master_fetch")
    MASTER_FETCH_API_VERSION = _fetch.MASTER_FETCH_API_VERSION
    MARKER_FILE = _fetch.MARKER_FILE
    STATE_FILE = _fetch.STATE_FILE
    atomic_bytes = _fetch.atomic_bytes
    atomic_json = _fetch.atomic_json
    decode_pack = _fetch.decode_pack
    fetch_packs = _fetch.fetch_packs
    file_digest = _fetch.file_digest
    get_master_tag = _fetch.get_master_tag
    load_modules = _fetch.load_modules
    main = _fetch.main
    pack_fingerprint = _fetch.pack_fingerprint
    token_from_xml = _fetch.token_from_xml

except ModuleNotFoundError as error:
    if error.name and error.name.startswith("HoshimiToolkit"):
        raise RuntimeError("HoshimiToolkit is missing; set IDOLY_TOOLKIT_DIR to its checkout") from error
    raise

if MASTER_FETCH_API_VERSION != 1:
    raise RuntimeError(f"HoshimiToolkit MasterDB fetch API {MASTER_FETCH_API_VERSION!r} "
                       "is incompatible; expected version 1")


if __name__ == "__main__":
    main()
