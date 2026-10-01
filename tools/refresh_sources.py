#!/usr/bin/env python3
"""Refresh official static sources and optionally translate only missing text.

No publishing, account creation, or simulator access. Existing
reviewed translations survive when their source identity and text still match.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

# Executed by --api-python because Solis dependencies may use a separate runtime.
# Deliberately does not import client_config.json or any account/token file.
MASTER_TAG_PROBE = '''
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import grpc, papi_grpc
from google.protobuf.empty_pb2 import Empty
channel = grpc.secure_channel('api.game-idolypride.jp:443',
    grpc.ssl_channel_credentials(root_certificates=(Path(sys.argv[1])/'iprroots.pem').read_bytes()),
    options=[('grpc.primary_user_agent', 'grpc-csharp/2.37.0-dev grpc-c/15.0.0 (android; chttp2)')])
try:
    response, context = papi_grpc.MasterStub(channel).Get.with_call(Empty(),
        metadata=[('x-app-version',sys.argv[2]),('content-type','application/grpc')], timeout=30)
    for key,value in list(context.initial_metadata() or [])+list(context.trailing_metadata() or []):
        if key=='x-error-code' and str(value) not in ('','0'):
            raise RuntimeError('Official Master.Get application error')
    if not response.masterTag.version or not response.masterTag.masterTagPacks:
        raise RuntimeError('Empty official MasterTag')
    target=Path(sys.argv[3]); target.parent.mkdir(parents=True,exist_ok=True)
    temporary=target.with_suffix('.tmp'); temporary.write_bytes(response.masterTag.SerializeToString())
    temporary.replace(target)
except Exception as error:
    print('MasterTag fetch failed ('+type(error).__name__+')',file=sys.stderr)
    sys.exit(1)
finally:
    channel.close()
'''


def read_json(path: Path, default=None):
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else default


def write_json(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + ".tmp")
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temp.replace(path)


def source_fields(directory: Path):
    return {(p.stem, record, field): text
            for p in directory.glob("*.json")
            for record, fields in read_json(p, {}).items()
            for field, text in fields.items()}


def field_difference(before: dict, after: dict) -> dict:
    return {"added": len(after.keys() - before.keys()),
            "removed": len(before.keys() - after.keys()),
            "changed": sum(before[k] != after[k] for k in before.keys() & after.keys())}


def synchronize_scripts(downloaded: Path, destination: Path, manifest: dict) -> dict:
    """Copy verified official ADV only; never silently remove retired local data."""
    destination.mkdir(parents=True, exist_ok=True)
    counts = {"added": 0, "changed": 0, "unchanged": 0}
    for item in manifest.get("resourceList", []):
        name = item["name"]
        if not name.startswith("adv_") or not name.endswith(".txt"):
            continue
        if Path(name).name != name:
            raise ValueError("Unsafe adventure resource name")
        source, target = downloaded / name, destination / name
        data = source.read_bytes()
        if hashlib.md5(data).hexdigest() != item["md5"]:
            raise ValueError("Official adventure content hash mismatch")
        previous = target.read_bytes() if target.is_file() else None
        state = "unchanged" if previous == data else "added" if previous is None else "changed"
        counts[state] += 1
        if previous != data and source.resolve() != target.resolve():
            temp = target.with_suffix(".txt.tmp")
            temp.write_bytes(data)
            temp.replace(target)
    return counts


def story_memory(csv_module, csv_dir: Path, source_dir: Path, memory_path: Path, *, restore: bool) -> int:
    """Resume matching story fields, keeping the current reviewed CSV authoritative."""
    memory = read_json(memory_path, {})
    applied = 0
    for path in csv_dir.glob("adv_*.csv"):
        rows = csv_module.load_csv(path)
        csv_module.verify_source(rows, source_dir)
        changed = False
        entries = memory.setdefault(path.name, {})
        for row in rows[:-2]:
            identity = json.dumps([row["id"], row["name"], row["text"]], ensure_ascii=False)
            if restore and not row["trans"] and entries.get(identity):
                csv_module.validate_translation(row["text"], entries[identity], row["id"])
                row["trans"] = entries[identity]
                changed = True
                applied += 1
            if row["trans"]:
                entries[identity] = row["trans"]
        if changed:
            csv_module.save_csv(path, rows)
    write_json(memory_path, memory)
    return applied


def seed_result_log(source: Path, destination: Path):
    """Bootstrap private legacy translation memory before any new model calls."""
    if source.is_file() and not destination.exists():
        # The workflow readers validate every record against current source.
        destination.write_bytes(source.read_bytes())


def fetched_state(args) -> dict:
    """Bind reuse to the exact successful official batch, excluding translations."""
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    def directory(path, pattern):
        values = {p.name: digest(p) for p in sorted(path.glob(pattern))}
        if not values:
            raise ValueError("Prepared source directory is empty")
        return hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()
    raw = args.work_dir / "masterdata"
    if (raw / ".master-fetch-in-progress.json").exists():
        raise ValueError("Master fetch is incomplete")
    return {"app_version": args.app_version, "source_dir": str(args.source_dir),
            "master_tag": digest(args.work_dir / "master-tag.pb"),
            "masterdata": directory(raw, "*.json"),
            "scripts": directory(args.source_dir, "adv_*.txt"),
            "manifest": digest(args.toolkit_dir / "cache/OctoManifest.json"),
            "notice_index": digest(args.work_dir / "notice-index.json"),
            "notice_pages": digest(args.work_dir / "notice-pages.json")}


def run(args) -> dict:
    from workspace_paths import load_module
    os.environ["IDOLY_TOOLKIT_DIR"] = str(args.toolkit_dir)
    os.environ["IDOLY_TRANSLATOR_DIR"] = str(args.translator_dir)
    stages = ["octo", "story-export", "story-rebase", "master-tag", "master-fetch",
              "master-export", "story-catalog", "notice-index", "notice-pages", "notice-import", "translation-plan"]
    if args.translate:
        stages += ["translate-story", "translate-master", "translate-notice", "validate"]
    summary = {"schema_version": 1, "started_at": datetime.now(timezone.utc).isoformat(),
               "dry_run": args.dry_run, "translate": args.translate, "stages": stages,
               "reuse_fetched": args.reuse_fetched,
               "master_tag": str(args.work_dir / "master-tag.pb"), "complete": False}
    if args.dry_run:
        summary["complete"] = True
        return summary
    args.work_dir.mkdir(parents=True, exist_ok=True)

    def command(stage, argv, cwd=ROOT):
        summary["stage"] = stage
        write_json(args.summary, summary)
        # Public Actions logs expose progress, never arguments or private output.
        print(f"Source refresh: {stage} started", flush=True)
        with (args.work_dir / (stage + ".log")).open("w", encoding="utf-8") as log:
            result = subprocess.run([str(x) for x in argv], cwd=cwd, stdout=log,
                                    stderr=subprocess.STDOUT, check=False)
        if result.returncode:
            raise RuntimeError(f"Stage {stage} failed; see private work directory")
        print(f"Source refresh: {stage} completed", flush=True)

    try:
        receipt = args.work_dir / "fetched-receipt.json"
        if args.reuse_fetched:
            if read_json(receipt) != fetched_state(args):
                raise ValueError("Missing or changed successful fetched batch")
        before = source_fields(args.translations_dir / "master/orig")
        manifest_path = args.toolkit_dir / "cache/OctoManifest.json"
        old_manifest = read_json(manifest_path, {})
        # Snapshot hashes before Toolkit writes into its default cache.
        old_scripts = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in args.source_dir.glob("adv_*.txt")}
        if not args.reuse_fetched:
            command("octo", [args.python, args.toolkit_dir / "main.py", "--source", "api",
                             "--prefix", "adv_", "--download-type", "resource", "--no-images"], args.toolkit_dir)
        manifest = read_json(manifest_path)
        synchronize_scripts(args.toolkit_dir / "cache/resource/adventure", args.source_dir, manifest)
        current_scripts = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in args.source_dir.glob("adv_*.txt")}
        summary["story_sources"] = field_difference(old_scripts, current_scripts)
        summary["octo"] = {"before": old_manifest.get("revision"), "after": manifest["revision"]}
        from pipeline import Pipeline
        pipe = Pipeline(ROOT, source_dir=args.source_dir, csv_dir=args.csv_dir)
        pipe.patch_dir = args.translations_dir / "story"
        summary["stage"] = "story-export"
        summary["story_csv_exported"] = pipe.export()
        summary["stage"] = "story-rebase"
        summary["story_rebase"] = pipe.refresh(accept_dropped=args.accept_story_rebase)

        raw_master = args.work_dir / "masterdata"
        if not args.reuse_fetched:
            command("master-tag", [args.api_python, "-c", MASTER_TAG_PROBE,
                    args.solis_dir, args.app_version, args.work_dir / "master-tag.pb"])
            command("master-fetch", [args.api_python, args.toolkit_dir / "master_fetch.py",
                    "--solis-dir", args.solis_dir, "--master-tag", args.work_dir / "master-tag.pb",
                    "--output-dir", raw_master], args.toolkit_dir)
        command("master-export", [args.python, ROOT / "tools/master_json.py", "export",
                "--input-dir", raw_master, "--orig-dir", args.translations_dir / "master/orig",
                "--zh-dir", args.translations_dir / "master/zh-Hans", "--prune-missing",
                "--name-glossary", args.translations_dir / "glossary/names.json",
                "--archive-dir", args.work_dir / "master-archive"])
        summary["master_version"] = read_json(raw_master / "master-fetch-state.json")["version"]
        summary["master_fields"] = field_difference(before, source_fields(args.translations_dir / "master/orig"))
        command("story-catalog", [args.python, ROOT / "tools/story_catalog.py", raw_master,
                                 args.translations_dir / "story-metadata/catalog.json"])

        notice_cli = [args.api_python, args.toolkit_dir / "notice_fetch.py"]
        index, pages = args.work_dir / "notice-index.json", args.work_dir / "notice-pages.json"
        if not args.reuse_fetched:
            day = ["--initialize-day"] if args.initialize_collector_day else []
            command("notice-index", [*notice_cli, "official-index", "--solis-dir", args.solis_dir,
                    "--app-version", args.app_version, "--account", args.notice_account, "--output", index, *day], args.toolkit_dir)
            command("notice-pages", [*notice_cli, "archive-pages", "--input", index,
                    "--cache-dir", args.toolkit_dir / "cache/notice/refresh-page-cache", "--output", pages,
                    "--report", args.work_dir / "notice-fetch-report.json", "--refresh"], args.toolkit_dir)
        from web_notice import import_pages
        summary["stage"] = "notice-import"
        notice_source = args.translations_dir / "notice/source.json"
        previous_pages = read_json(notice_source, {"pages": {}})["pages"]
        incoming_pages = read_json(pages)["pages"]
        summary["notice_pages"] = {"fetched": len(incoming_pages),
            "new": len(incoming_pages.keys() - previous_pages.keys()),
            "new_texts": sum(len(set(v["texts"]) - set(previous_pages.get(k, {}).get("texts", [])))
                             for k, v in incoming_pages.items())}
        import_pages(pages, notice_source, index)

        csv = load_module("Hoshimi_Teleprompter", "src.csv_workflow")
        master = load_module("Hoshimi_Teleprompter", "src.master_workflow")
        notice = load_module("Hoshimi_Teleprompter", "src.notice_workflow")
        credits = load_module("Hoshimi_Teleprompter", "src.notice_credit_policy")
        master_log, notice_log = args.work_dir / "master-results.jsonl", args.work_dir / "notice-results.jsonl"
        seed_result_log(args.translations_dir / "master/working/results.jsonl", master_log)
        seed_result_log(args.translations_dir / "notice/working/results.jsonl", notice_log)
        master_args = [args.translations_dir / "master/orig", args.translations_dir / "master/zh-Hans", master_log]
        notice_args = [notice_source, args.translations_dir / "notice/zh-Hans.json",
                       args.translations_dir / "ui/zh-Hans.json", notice_log]
        initial_notice = args.work_dir / "notice-initial.json"
        write_json(initial_notice, read_json(notice_args[1]))
        credits.apply_policy(notice_args[0], notice_args[1], initial_notice, notice_log,
                             args.work_dir / "notice-credit-report.json")
        summary["story_reused"] = story_memory(csv, args.csv_dir, args.source_dir,
                    args.work_dir / "story-memory.json", restore=True)

        def pending_counts():
            story = 0
            for path in args.csv_dir.glob("adv_*.csv"):
                rows = csv.load_csv(path)
                csv.verify_source(rows, args.source_dir)
                ready, total = csv.coverage(rows)
                story += total - ready
            return {"story_fields": story, "master_strings": len(master.collect(*master_args)[0]),
                    "notice_nodes": len(notice.collect(*notice_args)[0])}

        summary["stage"] = "translation-plan"
        summary["pending_before"] = pending_counts()
        # Reuse only validated text memory; the source export already retained
        # unchanged human edits and restored original music production credits.
        master.apply_memory(*master_args[:2], master.collect(*master_args)[1])
        notice.apply_memory(*notice_args[:3], notice.collect(*notice_args)[1])
        if args.translate:
            api_args = ["--glossary", args.translations_dir / "glossary/names.json",
                        "--term-glossary", args.translations_dir / "glossary/terms.json",
                        "--workers", str(args.workers)]
            if args.env_file:
                api_args += ["--env-file", args.env_file]
            translator = [args.python, args.translator_dir / "main.py"]
            if summary["pending_before"]["story_fields"]:
                try:
                    command("translate-story", [*translator, "translate", "--csv-dir", args.csv_dir,
                            "--source-dir", args.source_dir, *api_args])
                finally:
                    story_memory(csv, args.csv_dir, args.source_dir,
                                 args.work_dir / "story-memory.json", restore=False)
            if summary["pending_before"]["master_strings"]:
                command("translate-master", [*translator, "master-translate", "--orig-dir", master_args[0],
                        "--zh-dir", master_args[1], "--log-file", master_log, *api_args])
            if summary["pending_before"]["notice_nodes"]:
                review = args.work_dir / "notice-review.json"
                pending = notice.collect(*notice_args)[0]
                approved = {}
                for page, text in pending:
                    approved.setdefault(page, []).append(text)
                write_json(review, {"source_sha256": notice.checksum(notice_source), "pages": approved})
                command("translate-notice", [*translator, "notice-translate", "--source", notice_args[0],
                        "--translations", notice_args[1], "--ui", notice_args[2], "--log-file", notice_log,
                        "--review-list", review, *api_args])
            credits.apply_policy(notice_args[0], notice_args[1], initial_notice, notice_log,
                                 args.work_dir / "notice-credit-report.json")
        summary["pending_after"] = pending_counts()
        if args.translate and any(summary["pending_after"].values()):
            raise ValueError("Translation coverage incomplete")
        summary["complete"] = True
        write_json(receipt, fetched_state(args))
        summary.pop("stage", None)
        return summary
    except Exception as error:
        summary["error_type"] = type(error).__name__
        write_json(args.summary, summary)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolkit-dir", type=Path, default=ROOT.parent / "HoshimiToolkit")
    parser.add_argument("--translator-dir", type=Path, default=ROOT.parent / "Hoshimi_Teleprompter")
    parser.add_argument("--solis-dir", type=Path, required=True)
    parser.add_argument("--app-version", required=True)
    parser.add_argument("--translations-dir", type=Path, default=ROOT / "translations")
    parser.add_argument("--source-dir", type=Path)
    parser.add_argument("--csv-dir", type=Path)
    parser.add_argument("--notice-account", type=Path)
    parser.add_argument("--work-dir", type=Path, default=ROOT / ".analysis/source-refresh")
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--api-python", default=sys.executable)
    parser.add_argument("--env-file", type=Path)
    parser.add_argument("--workers", type=int, default=64)
    translation = parser.add_mutually_exclusive_group()
    translation.add_argument("--translate", action="store_true", help="Send pending official static text to the configured model")
    translation.add_argument("--no-translate", dest="translate", action="store_false")
    parser.set_defaults(translate=False)
    parser.add_argument("--reuse-fetched", action="store_true", help="Reuse the exact successful batch in work-dir without network fetching")
    parser.add_argument("--initialize-collector-day", action="store_true", help="Allow daily initialization and automatic rewards only for the existing dedicated notice collector")
    parser.add_argument("--accept-story-rebase", action="store_true", help="Archive unmatched old story fields and retranslate changed source")
    parser.add_argument("--dry-run", action="store_true", help="Print stages only, without network or file changes")
    args = parser.parse_args(argv)
    if not 1 <= args.workers <= 100:
        parser.error("workers must be 1-100")
    for name in ("toolkit_dir", "translator_dir", "solis_dir", "translations_dir", "work_dir"):
        setattr(args, name, getattr(args, name).expanduser().resolve())
    for name in ("python", "api_python"):
        if os.sep in getattr(args, name):
            # Keep the virtualenv executable path rather than resolving its
            # symlink to a base interpreter without the installed dependencies.
            setattr(args, name, str(Path(getattr(args, name)).expanduser().absolute()))
    if args.env_file:
        args.env_file = args.env_file.expanduser().resolve()
    args.source_dir = (args.source_dir or args.toolkit_dir / "cache/resource/adventure").resolve()
    args.csv_dir = (args.csv_dir or args.toolkit_dir / "cache/csv").resolve()
    args.notice_account = (args.notice_account or args.toolkit_dir / "cache/notice/collector-account.json").resolve()
    args.summary = (args.summary or args.work_dir / "summary.json").resolve()
    try:
        summary = run(args)
        if not args.dry_run:
            write_json(args.summary, summary)
        print(json.dumps(summary, ensure_ascii=False))
        return 0
    except Exception as error:
        print(f"Source refresh failed ({type(error).__name__}); see private summary", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
