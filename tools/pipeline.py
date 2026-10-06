#!/usr/bin/env python3
"""Reproducible, guarded text pipeline for IDOLY PRIDE localization.

The network translation and download steps are deliberately explicit. This
command takes reviewed CSV/JSON inputs and builds local runtime artifacts.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

from workspace_paths import toolkit_root, story_source_dir, story_csv_dir, story_output_dir, load_module

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from font_policy import DEFAULT_FONT_MODE

import build_story_titles
import card_display_aliases
import story_card_catalog
import story_event_catalog
import localize
import master_json
import web_notice

VERSION = 2
TOOLKIT_API_VERSION = 1


def load_toolkit():
    """Load the separately maintained resource project with a clear error."""
    try:
        toolkit = load_module("HoshimiToolkit", "src.adv_csv")
    except ModuleNotFoundError as error:
        if error.name and error.name.startswith("HoshimiToolkit"):
            raise ValueError("HoshimiToolkit is missing. Clone its repository into "
                             f"{toolkit_root()} or set IDOLY_TOOLKIT_DIR before running the text pipeline") from error
        raise
    version = getattr(toolkit, "WORKFLOW_API_VERSION", None)
    if version != TOOLKIT_API_VERSION:
        raise ValueError(f"HoshimiToolkit workflow API {version!r} is incompatible; "
                         f"this project requires version {TOOLKIT_API_VERSION}")
    return toolkit


def digest(path: Path) -> str | None:
    if not path.is_file():
        return None
    hash_value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hash_value.update(block)
    return hash_value.hexdigest()


def json_file(path: Path) -> dict:
    if not path.is_file():
        return {}
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected JSON object")
    return value


def atomic_copy(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=target.parent, prefix=".pipeline-", delete=False) as stream:
        temporary = Path(stream.name)
    try:
        shutil.copyfile(source, temporary)
        os.replace(temporary, target)
    finally:
        temporary.unlink(missing_ok=True)


class Pipeline:
    def __init__(self, root: Path = ROOT, *, source_dir: Path | None = None,
                 csv_dir: Path | None = None, output_dir: Path | None = None):
        self.root = root
        self.source_dir = source_dir or story_source_dir(root)
        self.csv_dir = csv_dir or story_csv_dir(root)
        self.patch_dir = root / "translations/story"
        self.txt_dir = output_dir or story_output_dir(root)
        self.glossary = root / "translations/glossary/names.json"
        self.term_glossary = root / "translations/glossary/terms.json"
        self.manifest = root / ".analysis/text-pipeline-manifest.json"
        self.title_map = root / ".analysis/compiled/story-title-map.json"
        self.title_conflicts = root / ".analysis/story-title-conflicts.json"
        self.mission_template_sources = root / ".analysis/mission-template-sources.json"
        self.runtime_conflicts = root / ".analysis/runtime-text-conflicts.json"
        self.master_text = root / ".analysis/compiled/master-text.json"
        self.master_keyed = root / ".analysis/master-keyed.json"
        self.dictionary = root / "app/src/main/assets/idoly-localify/dict.json"
        self.web_notice_source = root / "translations/notice/source.json"
        self.web_notice_translation = root / "translations/notice/zh-Hans.json"
        self.web_notice_title_resolutions = root / "translations/notice/title-resolutions.json"
        self.web_notice_title_conflicts = root / ".analysis/notice-title-conflicts.json"
        self.web_notice_dictionary = root / "app/src/main/assets/idoly-localify/web-notice.json"
        self.legal_runtime = root / "translations/legal/runtime.json"
        self.legal_source = root / "translations/legal/source.json"
        self.legal_translation = root / "translations/legal/zh-Hans.json"

    def names(self) -> list[str]:
        sources = {path.stem for path in self.source_dir.glob("adv_*.txt")}
        csvs = {path.stem for path in self.csv_dir.glob("adv_*.csv")}
        if not sources:
            raise ValueError(f"No adventure TXT files in {self.source_dir}")
        if sources != csvs:
            missing = sorted(sources - csvs)
            extra = sorted(csvs - sources)
            raise ValueError(f"TXT/CSV set differs: missing CSV {missing[:5]} "
                             f"({len(missing)} total); orphan CSV {extra[:5]} ({len(extra)} total). "
                             "Run `python3 tools/pipeline.py export` for new TXT files.")
        return sorted(sources)

    def status(self) -> dict:
        """Inspect the next text-pipeline step without changing review files."""
        sources = {path.stem for path in self.source_dir.glob("adv_*.txt")}
        csvs = {path.stem for path in self.csv_dir.glob("adv_*.csv")}
        missing = sorted(sources - csvs)
        orphan = sorted(csvs - sources)
        stale = []
        invalid = []
        for name in sorted(sources & csvs):
            path = self.csv_dir / f"{name}.csv"
            try:
                with path.open(encoding="utf-8", newline="") as stream:
                    reader = csv.DictReader(stream)
                    rows = list(reader)
                if (reader.fieldnames != ["id", "name", "text", "trans"] or
                        len(rows) < 2 or rows[-2]["id"] != "info" or
                        rows[-2]["name"] != f"{name}.txt" or
                        rows[-1]["id"] != "译者" or
                        not re.fullmatch(r"[0-9a-f]{64}", rows[-2]["text"] or "") or
                        any(set(row) != {"id", "name", "text", "trans"} or
                            any(value is None for value in row.values())
                            for row in rows)):
                    invalid.append(name)
                elif rows[-2]["text"] != digest(self.source_dir / f"{name}.txt"):
                    stale.append(name)
            except (OSError, UnicodeError, csv.Error):
                invalid.append(name)
        result = {"sources": len(sources), "csv": len(csvs),
                  "missing_csv": missing, "orphan_csv": orphan,
                  "stale_csv": stale, "invalid_csv": invalid,
                  "ready": False, "next_command": None, "issue": None}
        if not sources:
            result["issue"] = "No downloaded adventure TXT files"
        elif invalid:
            result["issue"] = "Inspect invalid CSV files before continuing"
        elif orphan:
            result["issue"] = "Review CSV files without matching TXT before continuing"
        elif missing:
            result["next_command"] = "python3 tools/pipeline.py export"
        elif stale:
            result["next_command"] = "python3 tools/pipeline.py refresh"
        else:
            try:
                self.run(verify=True)
            except (OSError, ValueError, KeyError, ImportError,
                    json.JSONDecodeError) as error:
                result["issue"] = str(error)
                if ("story outputs or inputs changed" in str(error) or
                        "Compiled artifacts differ" in str(error)):
                    previous = json_file(self.manifest).get("files", {})
                    if not isinstance(previous, dict):
                        result["issue"] = "Text pipeline manifest has invalid file entries"
                    else:
                        changed_csv = [name for name in sorted(sources) if
                                       not isinstance(previous.get(name), dict) or
                                       previous[name].get("csv") !=
                                       digest(self.csv_dir / f"{name}.csv")]
                        try:
                            if changed_csv:
                                toolkit = load_toolkit()
                                with tempfile.TemporaryDirectory(prefix="pipeline-status-") as temp:
                                    for name in changed_csv:
                                        toolkit.save_patch(
                                            self.source_dir / f"{name}.txt",
                                            self.csv_dir / f"{name}.csv",
                                            Path(temp) / f"{name}.json")
                        except (OSError, ValueError, KeyError, ImportError,
                                csv.Error) as csv_error:
                            result["issue"] = f"Review changed CSV: {csv_error}"
                        else:
                            result["next_command"] = "python3 tools/pipeline.py build"
            else:
                result["ready"] = True
        return result

    def export(self) -> int:
        """Create CSV for new scripts, preserving all existing review files."""
        toolkit = load_toolkit()
        sources = sorted(self.source_dir.glob("adv_*.txt"))
        if not sources:
            raise ValueError(f"No adventure TXT files in {self.source_dir}")
        new = [source for source in sources if not (self.csv_dir / f"{source.stem}.csv").exists()]
        (self.root / ".analysis").mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="pipeline-export-", dir=self.root / ".analysis") as temp:
            stage = Path(temp)
            for source in new:
                patch = self.patch_dir / f"{source.stem}.json"
                toolkit.export_file(source, stage / f"{source.stem}.csv",
                                    patch if patch.is_file() else None)
            for source in new:
                atomic_copy(stage / f"{source.stem}.csv", self.csv_dir / f"{source.stem}.csv")
        return len(new)

    def refresh(self, *, accept_dropped: bool = False) -> dict[str, int]:
        """Rebase revised TXT or migrate legacy CSV identifiers without changing translations."""
        toolkit = load_toolkit()
        names = self.names()
        analysis = self.root / ".analysis"
        analysis.mkdir(parents=True, exist_ok=True)
        report_path = analysis / "rebase-report.json"
        report = {"scripts": {}, "unmatched": []}
        changed = {}
        with tempfile.TemporaryDirectory(prefix="pipeline-rebase-", dir=analysis) as temp:
            stage = Path(temp)
            for name in names:
                source = self.source_dir / f"{name}.txt"
                old_csv = self.csv_dir / f"{name}.csv"
                source_bytes = source.read_bytes()
                csv_bytes = old_csv.read_bytes()
                source_sha = hashlib.sha256(source_bytes).hexdigest()
                csv_sha = hashlib.sha256(csv_bytes).hexdigest()
                reader = csv.DictReader(io.StringIO(csv_bytes.decode("utf-8"), newline=""), strict=True)
                if reader.fieldnames != ["id", "name", "text", "trans"]:
                    raise ValueError(f"{old_csv}: invalid CSV columns")
                rows = list(reader)
                if any(set(row) != {"id", "name", "text", "trans"} or
                       any(not isinstance(value, str) for value in row.values()) for row in rows):
                    raise ValueError(f"{old_csv}: invalid CSV cells")
                if len(rows) < 2 or rows[-2]["id"] != "info":
                    raise ValueError(f"{old_csv}: missing source metadata")
                candidate = stage / f"{name}.csv"
                if rows[-2]["text"] == source_sha:
                    # Older exporters called narration/choice fields :text:. A
                    # matching file hash alone does not validate those CSV rows.
                    items = [item for item in toolkit.fields(source_bytes.decode("utf-8"))
                             if item.key != "name"]
                    if (len(rows) != len(items) + 2 or rows[-2]["name"] != source.name or
                            rows[-1]["id"] != "译者"):
                        raise ValueError(f"{old_csv}: CSV source metadata or row count differs")
                    migrated = 0
                    for item, row in zip(items, rows[:-2]):
                        identifier = toolkit.csv_identifier(item)
                        if (row["id"] not in {item.identifier, identifier} or
                                row["text"] != item.source or row["name"] != item.name):
                            raise ValueError(f"{old_csv}: CSV source mismatch at {item.identifier}")
                        if row["id"] != identifier:
                            row["id"] = identifier
                            migrated += 1
                    if not migrated:
                        continue
                    with candidate.open("w", encoding="utf-8", newline="") as stream:
                        writer = csv.DictWriter(stream, fieldnames=["id", "name", "text", "trans"],
                                                lineterminator="\n")
                        writer.writeheader()
                        writer.writerows(rows)
                    preserved = sum(bool(row["trans"]) for row in rows[:-2])
                    lost = []
                    report["scripts"][name] = {"preserved": preserved, "unmatched": 0,
                                                "migrated_ids": migrated}
                else:
                    preserved, lost = toolkit.rebase_file(source, old_csv, candidate)
                    report["scripts"][name] = {"preserved": preserved, "unmatched": len(lost)}
                report["unmatched"].extend({"file": f"{name}.csv", **entry} for entry in lost)
                changed[name] = (candidate, source_sha, csv_sha)
            localize.write_json(report_path, report)
            if report["unmatched"] and not accept_dropped:
                raise ValueError(f"{len(report['unmatched'])} translated fields no longer match "
                                 f"the updated TXT. Review {report_path}; re-run refresh "
                                 "with --accept-dropped to archive old CSV and continue.")
            for name, (_, source_sha, csv_sha) in changed.items():
                if (digest(self.source_dir / f"{name}.txt") != source_sha or
                        digest(self.csv_dir / f"{name}.csv") != csv_sha):
                    raise ValueError(f"{name}: input changed during CSV rebase")
            backup_dir = analysis / "rebase-backup"
            for name, (candidate, _, csv_sha) in changed.items():
                old_csv = self.csv_dir / f"{name}.csv"
                backup = backup_dir / f"{name}-{csv_sha[:12]}.csv"
                if not backup.is_file():
                    atomic_copy(old_csv, backup)
                atomic_copy(candidate, old_csv)
        return {"scripts": len(changed), "preserved": sum(
            value["preserved"] for value in report["scripts"].values()),
            "unmatched": len(report["unmatched"])}

    def run(self, *, verify: bool = False) -> dict[str, int]:
        toolkit = load_toolkit()
        names = self.names()
        required = [self.root / "translations/ui/source.json",
                    self.root / "translations/ui/zh-Hans.json",
                    self.term_glossary,
                    self.web_notice_source, self.web_notice_translation,
                    self.root / "translations/story-metadata/catalog.json"]
        for path in required:
            if not path.is_file():
                raise ValueError(f"Missing translation input: {path}")
        for directory in (self.root / "translations/master/orig",
                          self.root / "translations/master/zh-Hans"):
            if not any(directory.glob("*.json")):
                raise ValueError(f"No MasterDB JSON tables in {directory}")
        if not self.glossary.is_file():
            raise ValueError(f"Missing name glossary: {self.glossary}")
        glossary = json_file(self.glossary)
        if any(not isinstance(k, str) or not isinstance(v, str) or not v
               for k, v in glossary.items()):
            raise ValueError(f"{self.glossary}: invalid name glossary")
        glossary_sha = digest(self.glossary)
        master_dirs = (self.root / "translations/master/orig",
                       self.root / "translations/master/zh-Hans")
        master_paths = sorted(path for directory in master_dirs
                              for path in directory.glob("*.json"))
        master_snapshot = {path: digest(path) for path in master_paths}
        static_inputs = required + [self.glossary, Path(toolkit.__file__),
                                    self.web_notice_title_resolutions,
                                    self.root / "translations/story-metadata/love-group-names.json",
                                    Path(card_display_aliases.__file__)]
        legal_workflow = None
        if self.legal_source.exists():
            legal_workflow = load_module("Hoshimi_Teleprompter", "src.legal_workflow")
            if not self.legal_translation.is_file():
                raise ValueError("Missing legal translation JSON")
            static_inputs.extend([self.legal_source, self.legal_translation,
                                  Path(legal_workflow.__file__)])
        static_snapshot = {path: digest(path) for path in static_inputs}
        published_paths = (self.manifest, self.title_map, self.title_conflicts,
                           self.runtime_conflicts, self.mission_template_sources,
                           self.web_notice_title_conflicts,
                           self.master_text, self.master_keyed, self.dictionary,
                           self.web_notice_dictionary, self.legal_runtime,
                           self.root / "app/src/main/assets/idoly-localify/card-stories.json",
                           self.root / "app/src/main/assets/idoly-localify/event-stories.json",
                           self.root / "app/src/main/assets/idoly-localify/love-stories.json")
        published_snapshot = {path: digest(path) for path in published_paths}
        previous = json_file(self.manifest)
        if previous and previous.get("version") not in (1, VERSION):
            raise ValueError("Text pipeline manifest has an unsupported version")
        engine_sha = digest(Path(toolkit.__file__))
        entries = previous.get("files", {})
        if not isinstance(entries, dict):
            raise ValueError("Text pipeline manifest has invalid file entries")
        for directory, suffix in ((self.patch_dir, ".json"), (self.txt_dir, ".txt")):
            orphan = sorted(path.stem for path in directory.glob(f"adv_*{suffix}")
                            if path.stem not in names)
            if orphan:
                raise ValueError(f"Orphan outputs in {directory}: {orphan[:5]} "
                                 f"({len(orphan)} total); review before rebuilding")
        dirty = []
        new_entries = {}
        initial_entries = {}
        for name in names:
            source = self.source_dir / f"{name}.txt"
            csv_path = self.csv_dir / f"{name}.csv"
            patch = self.patch_dir / f"{name}.json"
            txt = self.txt_dir / f"{name}.txt"
            state = {"source": digest(source), "csv": digest(csv_path),
                     "patch": digest(patch), "txt": digest(txt)}
            if (entries.get(name) != state or previous.get("name_glossary") != glossary_sha
                    or previous.get("engine") != engine_sha
                    or previous.get("version") != VERSION):
                dirty.append(name)
            new_entries[name] = state
            initial_entries[name] = state.copy()
        obsolete = sorted(set(entries) - set(names))
        if obsolete:
            raise ValueError(f"Manifest has removed source scripts: {obsolete[:5]}; "
                             "review and remove their outputs before rebuilding")
        if verify and dirty:
            raise ValueError(f"{len(dirty)} story outputs or inputs changed; "
                             f"first: {dirty[:8]}. Run `python3 tools/pipeline.py build`.")

        analysis = self.root / ".analysis"
        if not verify:
            analysis.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="pipeline-build-",
                                         dir=None if verify else analysis) as temp:
            stage = Path(temp)
            staged_story = {}
            for name in dirty:
                source = self.source_dir / f"{name}.txt"
                csv_path = self.csv_dir / f"{name}.csv"
                stage_patch = stage / "patch" / f"{name}.json"
                stage_txt = stage / "txt" / f"{name}.txt"
                toolkit.save_patch(source, csv_path, stage_patch)
                toolkit.merge_file(source, csv_path, stage_txt, glossary)
                staged_story[name] = (stage_patch, stage_txt)
                new_entries[name]["patch"] = digest(stage_patch)
                new_entries[name]["txt"] = digest(stage_txt)

            title_path = stage / "story-title-map.json"
            conflict_path = stage / "story-title-conflicts.json"
            titles, conflicts = build_story_titles.collect_titles(self.csv_dir)
            localize.write_json(title_path, titles)
            localize.write_json(conflict_path, conflicts)

            master_path = stage / "master-text.json"
            master_json.build(self.root / "translations/master/orig",
                              self.root / "translations/master/zh-Hans", master_path)
            master_keyed_path = stage / "master-keyed.json"
            master_keyed_counts = master_json.build_keyed(
                self.root / "translations/master/orig",
                self.root / "translations/master/zh-Hans", master_keyed_path)
            card_catalog_path = stage / "card-stories.json"
            card_catalog_path.write_bytes(story_card_catalog.build(self.root))
            event_catalogs = []
            for kind in ("event", "love"):
                path = stage / f"{kind}-stories.json"
                path.write_bytes(story_event_catalog.build(self.root, kind))
                event_catalogs.append((path, self.root / f"app/src/main/assets/idoly-localify/{kind}-stories.json"))

            story_dir = self.patch_dir
            if staged_story:
                story_dir = stage / "all-patches"
                story_dir.mkdir()
                for name in names:
                    staged = staged_story.get(name)
                    patch = staged[0] if staged else self.patch_dir / f"{name}.json"
                    if not patch.is_file():
                        raise ValueError(f"Missing story patch: {patch}")
                    shutil.copyfile(patch, story_dir / f"{name}.json")

            web_notice_path = stage / "web-notice.json"
            web_notice_result = web_notice.build(
                self.web_notice_source, self.web_notice_translation,
                self.root / "translations/ui/zh-Hans.json", web_notice_path)
            notice_title_conflicts = stage / "notice-title-conflicts.json"
            notice_headings = web_notice.headline_aliases(
                self.web_notice_source, web_notice_result,
                self.web_notice_title_resolutions, notice_title_conflicts)

            dict_path = stage / "dict.json"
            legal_path = None
            if legal_workflow is not None:
                legal_path = stage / "legal-runtime.json"
                legal_source = legal_workflow.load_source(self.legal_source)
                legal_translation = legal_workflow.initialize(legal_source, self.legal_translation)
                localize.write_json(legal_path, legal_workflow.compile_runtime(
                    legal_source, legal_translation))
            runtime_conflicts = stage / "runtime-text-conflicts.json"
            mission_template_report = stage / "mission-template-sources.json"
            result = localize.build(
                self.root / "translations/ui/source.json",
                self.root / "translations/ui/zh-Hans.json", dict_path, False,
                font_mode=DEFAULT_FONT_MODE, story_dir=story_dir,
                name_glossary=self.glossary, story_titles=title_path,
                term_glossary=self.term_glossary,
                master_text=master_path, conflict_report=runtime_conflicts,
                notice_headings=notice_headings,
                card_display_aliases=card_display_aliases.derive(
                    json_file(self.root / "translations/master/orig/Card.json"),
                    json_file(self.root / "translations/master/zh-Hans/Card.json"),
                    json_file(self.root / "translations/story-metadata/catalog.json"), glossary),
                legal_runtime=legal_path, legal_source=self.legal_source,
                mission_source=self.root / "translations/master/orig/Mission.json",
                mission_translation=self.root / "translations/master/zh-Hans/Mission.json",
                mission_template_report=mission_template_report)
            runtime_conflict_count = len(json_file(runtime_conflicts)["conflicts"])

            artifacts = [(title_path, self.title_map), (conflict_path, self.title_conflicts),
                         (runtime_conflicts, self.runtime_conflicts),
                         (mission_template_report, self.mission_template_sources),
                         (notice_title_conflicts, self.web_notice_title_conflicts),
                         (master_path, self.master_text),
                         (master_keyed_path, self.master_keyed),
                         (dict_path, self.dictionary),
                         (web_notice_path, self.web_notice_dictionary),
                         (card_catalog_path, self.root / "app/src/main/assets/idoly-localify/card-stories.json")]
            artifacts.extend(event_catalogs)
            if legal_path is not None:
                artifacts.append((legal_path, self.legal_runtime))
            changed_artifacts = [(source, target) for source, target in artifacts
                                 if digest(source) != digest(target)]
            current_master_paths = sorted(path for directory in master_dirs
                                          for path in directory.glob("*.json"))
            if (self.names() != names or
                    {path: digest(path) for path in static_inputs} != static_snapshot or
                    current_master_paths != master_paths or
                    {path: digest(path) for path in master_paths} != master_snapshot or
                    any(initial_entries[name] != {
                        "source": digest(self.source_dir / f"{name}.txt"),
                        "csv": digest(self.csv_dir / f"{name}.csv"),
                        "patch": digest(self.patch_dir / f"{name}.json"),
                        "txt": digest(self.txt_dir / f"{name}.txt")}
                        for name in names)):
                raise ValueError("Text pipeline inputs changed during build; retry")
            if {path: digest(path) for path in published_paths} != published_snapshot:
                raise ValueError("Compiled outputs changed during build; retry")
            if verify:
                if changed_artifacts:
                    raise ValueError("Compiled artifacts differ: " + ", ".join(
                        str(target.relative_to(self.root)) for _, target in changed_artifacts))
            else:
                for name, (patch, txt) in staged_story.items():
                    if digest(patch) != digest(self.patch_dir / f"{name}.json"):
                        atomic_copy(patch, self.patch_dir / f"{name}.json")
                    if digest(txt) != digest(self.txt_dir / f"{name}.txt"):
                        atomic_copy(txt, self.txt_dir / f"{name}.txt")
                for source, target in changed_artifacts:
                    atomic_copy(source, target)
                localize.write_json(self.manifest, {
                    "version": VERSION, "engine": engine_sha,
                    "name_glossary": glossary_sha, "files": new_entries})

        return {"scripts": len(names), "rebuilt": len(dirty),
                "artifacts": len(changed_artifacts), "titles": len(titles),
                "title_conflicts": len(conflicts), "runtime_text": len(result["text"]),
                "runtime_conflicts": runtime_conflict_count,
                "master_keyed_fields": master_keyed_counts["indexed_fields"],
                "master_contextual_sources": master_keyed_counts["contextual_sources"],
                "web_notice": sum(len(page) for page in web_notice_result["pages"].values())}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("status", "export", "refresh", "build", "verify"))
    parser.add_argument("--accept-dropped", action="store_true",
                        help="Refresh only: archive old CSV and leave changed source fields untranslated")
    parser.add_argument("--toolkit-dir", type=Path)
    parser.add_argument("--translator-dir", type=Path)
    parser.add_argument("--source-dir", type=Path)
    parser.add_argument("--csv-dir", type=Path)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if args.toolkit_dir:
        os.environ["IDOLY_TOOLKIT_DIR"] = str(args.toolkit_dir.resolve())
    if args.translator_dir:
        os.environ["IDOLY_TRANSLATOR_DIR"] = str(args.translator_dir.resolve())
    try:
        pipeline = Pipeline(source_dir=args.source_dir, csv_dir=args.csv_dir,
                            output_dir=args.output_dir)
        if args.command == "status":
            print(json.dumps(pipeline.status(), ensure_ascii=False, sort_keys=True))
        elif args.command == "export":
            print(f"Exported {pipeline.export()} new CSV files; existing CSV files untouched")
        elif args.command == "refresh":
            print(json.dumps(pipeline.refresh(accept_dropped=args.accept_dropped),
                             ensure_ascii=False, sort_keys=True))
        else:
            result = pipeline.run(verify=args.command == "verify")
            print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"Pipeline failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
