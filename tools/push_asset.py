#!/usr/bin/env python3
"""Copy locally repacked AssetBundles to the Android test game's override folder."""

import argparse
import os
from pathlib import Path
import subprocess


REMOTE = "/sdcard/Android/data/game.qualiarts.idolypride/files/idoly-localify/asset"
from workspace_paths import asset_output_dir

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path,
                        default=asset_output_dir(ROOT))
    parser.add_argument("--file", action="append", default=[],
                        help="One AssetBundle basename; repeat to select several")
    parser.add_argument("--adb", default=os.environ.get("ADB", "adb"))
    parser.add_argument("--serial", default=os.environ.get("ANDROID_SERIAL"))
    args = parser.parse_args()
    if args.file and any(Path(name).name != name or name in {".", ".."}
                         for name in args.file):
        parser.error("--file must be an AssetBundle basename")
    files = ([args.source_dir / name for name in args.file] if args.file else
             sorted(path for path in args.source_dir.iterdir() if path.is_file())
             if args.source_dir.is_dir() else [])
    if not files:
        parser.error("No local AssetBundles selected")
    for path in files:
        if not path.is_file() or path.stat().st_size > 64 * 1024 * 1024:
            parser.error(f"Invalid UnityFS AssetBundle: {path}")
        with path.open("rb") as stream:
            signature = stream.read(8)
        if signature != b"UnityFS\0":
            parser.error(f"Invalid UnityFS AssetBundle: {path}")
    adb = [args.adb] + (["-s", args.serial] if args.serial else [])
    subprocess.run(adb + ["shell", "mkdir", "-p", REMOTE], check=True)
    for path in files:
        subprocess.run(adb + ["push", str(path), f"{REMOTE}/{path.name}"], check=True)
    print(f"Copied {len(files)} AssetBundles; restart the game to reload them")


if __name__ == "__main__":
    main()
