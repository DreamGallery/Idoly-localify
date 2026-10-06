#!/usr/bin/env python3
"""Check the project file lists before separate publication."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


from workspace_paths import toolkit_root, translator_root

ROOT = Path(__file__).resolve().parents[1]
REPOSITORIES = (ROOT, toolkit_root(ROOT),
                translator_root(ROOT))
LOCAL_NAMES = {".env", "octo-settings.json", "octocacheevai", "master-tag.pb"}
LOCAL_SUFFIXES = {".apk", ".apks", ".xapk", ".aab", ".db", ".sqlite", ".sqlite3",
                  ".pem", ".key", ".jks", ".keystore", ".p12",
                  ".otf", ".ttf", ".ttc", ".log"}
LOCAL_PARTS = {".analysis", ".gradle", ".venv", "build"}
MAX_FILE_SIZE = 95 * 1024 * 1024


def git(repo: Path, *args: str) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(["git", "-C", str(repo), *args],
                          check=True, capture_output=True)


def candidates(repo: Path) -> list[Path]:
    result = git(repo, "ls-files", "--cached", "--others", "--exclude-standard", "-z")
    return [Path(raw.decode("utf-8")) for raw in result.stdout.split(b"\0") if raw]


def problem(relative: Path, path: Path) -> str | None:
    name = relative.name.lower()
    if name != ".env.example" and (
            name in LOCAL_NAMES or name.startswith(".env.") or ".env." in name or
            name.endswith(".env")):
        return "local credential or captured setting"
    if relative.suffix.lower() in LOCAL_SUFFIXES:
        return "local binary or log"
    if any(part in LOCAL_PARTS for part in relative.parts):
        return "generated directory"
    if "cache" in relative.parts and relative.name != ".gitkeep":
        return "game resource cache"
    if path.is_symlink():
        return "symlink requires individual review"
    if path.stat().st_size >= MAX_FILE_SIZE:
        return "file is at least 95 MiB"
    return None


def check(repo: Path) -> tuple[int, int, list[tuple[Path, str]]]:
    if not (repo / ".git").exists():
        raise ValueError(f"Missing Git repository: {repo}")
    files = 0
    size = 0
    problems = []
    for relative in candidates(repo):
        path = repo / relative
        if not path.is_file() and not path.is_symlink():
            continue  # A tracked path may have been intentionally deleted.
        files += 1
        if path.is_file():
            size += path.stat().st_size
        reason = problem(relative, path)
        if reason:
            problems.append((relative, reason))
    return files, size, problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", action="append", type=Path,
                        help="Check only this Git repository; repeatable")
    args = parser.parse_args()
    repositories = [path.resolve() for path in args.repo] if args.repo else REPOSITORIES
    failures = 0
    for repo in repositories:
        try:
            files, size, problems = check(repo)
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            print(f"{repo}: FAILED: {error}")
            failures += 1
            continue
        print(f"{repo.name}: {files} candidate files, {size / 1024**2:.1f} MiB, "
              f"{len(problems)} flagged")
        for relative, reason in problems:
            print(f"  {relative}: {reason}")
        failures += len(problems)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
