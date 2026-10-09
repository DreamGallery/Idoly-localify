#!/usr/bin/env python3
"""Collect captured game strings and compile a checked runtime dictionary."""

import argparse
from collections import Counter, defaultdict
import json
import os
from pathlib import Path
import re
import sys
import tempfile

from numeric_text_templates import compile_templates
import mission_numeric_templates
from home_activity_names import compile_names
from font_policy import DEFAULT_FONT_MODE, FONT_MODES

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "translations" / "ui" / "source.json"
TRANSLATION = ROOT / "translations" / "ui" / "zh-Hans.json"
STORY = ROOT / "translations" / "story"
NAME_GLOSSARY = ROOT / "translations/glossary/names.json"
TERM_GLOSSARY = ROOT / "translations/glossary/terms.json"
MASTER_TEXT = ROOT / ".analysis" / "compiled" / "master-text.json"
STORY_TITLES = ROOT / ".analysis" / "compiled" / "story-title-map.json"
OUTPUT = ROOT / "app" / "src" / "main" / "assets" / "idoly-localify" / "dict.json"
TOKEN = re.compile(
    r"\{\d+(?::[^{}]+)?\}|%(?:\d+\$)?[-+#0 ]*\d*(?:\.\d+)?[sdif]|"
    r"<[/]?[A-Za-z][^>]*>|<#[0-9A-Fa-f]{6,8}>")


def read_json(path: Path) -> dict:
    if not path.exists():
        return {"text": {}, "i18n": {}}
    with path.open(encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected JSON object")
    for name in ("text", "i18n"):
        if not isinstance(value.get(name, {}), dict):
            raise ValueError(f"{path}: {name} must be an object")
    if not isinstance(value.get("i18n_by_source", {}), dict):
        raise ValueError(f"{path}: i18n_by_source must be an object")
    if not isinstance(value.get("numeric_text_templates", {}), dict):
        raise ValueError(f"{path}: numeric_text_templates must be an object")
    if not isinstance(value.get("home_activity_names", {}), dict):
        raise ValueError(f"{path}: home_activity_names must be an object")
    return value


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent,
                                     prefix=".localize-", delete=False) as stream:
        temporary = Path(stream.name)
        json.dump(data, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def captured_entries(dump_path: Path):
    """Read a captured JSONL file strictly, recovering NUL-padded log records."""
    with dump_path.open(encoding="utf-8") as stream:
        for number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            recovered = line.startswith("\x00")
            if recovered:
                line = line.lstrip("\x00")
            try:
                item = json.loads(line)
                kind = item["kind"]
                original = item["source"]
                key = original if kind == "text" else item["key"]
                if kind not in ("text", "i18n") or not isinstance(original, str) or not isinstance(key, str):
                    raise ValueError("invalid entry")
            except (ValueError, KeyError, TypeError) as error:
                raise ValueError(f"{dump_path}:{number}: {error}") from error
            yield number, kind, key, original, recovered


def collect(dump_path: Path, source_path: Path, kind_filter: str | None = None,
            review_report: Path | None = None,
            reviewed_variants: Path | None = None) -> int:
    source = read_json(source_path)
    source.setdefault("text", {})
    source.setdefault("i18n", {})
    observed_text = set()
    observed_i18n = defaultdict(set)
    recovered_lines = []
    for number, kind, key, original, recovered in captured_entries(dump_path):
        if recovered:
            recovered_lines.append(number)
        if kind_filter is not None and kind != kind_filter:
            continue
        if kind == "text":
            observed_text.add(original)
        else:
            observed_i18n[key].add(original)

    reviewed = {}
    if reviewed_variants is not None and reviewed_variants.is_file():
        reviewed = json.loads(reviewed_variants.read_text(encoding="utf-8"))
        if not isinstance(reviewed, dict) or any(
                not isinstance(key, str) or not isinstance(values, list) or
                any(not isinstance(value, str) for value in values)
                for key, values in reviewed.items()):
            raise ValueError(f"{reviewed_variants}: invalid reviewed variants")

    changes = 0
    for original in sorted(observed_text):
        if original not in source["text"]:
            source["text"][original] = original
            changes += 1

    candidates = []
    for key, values in sorted(observed_i18n.items()):
        canonical = source["i18n"].get(key)
        if canonical is None and len(values) == 1:
            source["i18n"][key] = next(iter(values))
            changes += 1
            continue
        known = set(reviewed.get(key, []))
        if canonical is not None:
            known.add(canonical)
        unreviewed = sorted(values - known)
        if unreviewed:
            candidates.append({"key": key, "canonical": canonical,
                               "unreviewed_sources": unreviewed})

    if review_report is not None and kind_filter != "text":
        write_json(review_report, {"candidates": candidates,
                                   "recovered_nul_lines": recovered_lines})
    if changes:
        write_json(source_path, source)
    return changes


def validate(source: str, translated: str, label: str) -> None:
    if not isinstance(translated, str):
        raise ValueError(f"{label}: translation must be a string")
    if not translated:
        return
    if Counter(TOKEN.findall(source)) != Counter(TOKEN.findall(translated)):
        raise ValueError(f"{label}: placeholders or rich-text tags differ from source")


def story_values(story_dir: Path | None, name_glossary: Path | None) -> list[str]:
    """Collect translated TXT fields so the font hook recognizes their rendered text."""
    values = set()
    glossary = {}
    if name_glossary is not None and name_glossary.exists():
        with name_glossary.open(encoding="utf-8") as stream:
            glossary = json.load(stream)
        if not isinstance(glossary, dict) or any(
            not isinstance(source, str) or not isinstance(value, str) or not value
            for source, value in glossary.items()
        ):
            raise ValueError(f"{name_glossary}: invalid name glossary")
    user_honorifics = {source: target for source, target in glossary.items()
                       if source.startswith("{user}") and source != "{user}" and
                       source != target}
    if story_dir is not None and story_dir.exists():
        for path in sorted(story_dir.rglob("*.json")):
            with path.open(encoding="utf-8") as stream:
                patch = json.load(stream)
            if not isinstance(patch, dict) or not isinstance(patch.get("translations"), dict):
                raise ValueError(f"{path}: invalid story patch")
            for identifier, translated in patch["translations"].items():
                if not isinstance(identifier, str) or not isinstance(translated, str):
                    raise ValueError(f"{path}: story translations must map IDs to strings")
                if translated:
                    variants = {translated}
                    for original, replacement in user_honorifics.items():
                        if original in translated:
                            variants.add(translated.replace(original, replacement))
                    for variant in tuple(variants):
                        # The adventure script stores a literal backslash-n;
                        # the player passes an actual newline to TMP_Text.
                        if r"\n" in variant:
                            variants.add(variant.replace(r"\n", "\n"))
                    values.update(variants)
    values.update(glossary.values())
    return sorted(values)


def build(source_path: Path, translation_path: Path, output_path: Path, dump: bool,
          font: bool = True, font_mode: str = DEFAULT_FONT_MODE,
          story_dir: Path | None = None, name_glossary: Path | None = None,
          story_titles: Path | None = None,
          term_glossary: Path | None = None,
          master_text: Path | None = None,
          conflict_report: Path | None = None,
          notice_headings: list[tuple[str, str]] | None = None,
          card_display_aliases: dict[str, str] | None = None,
          legal_runtime: Path | None = None,
          legal_source: Path | None = None,
          mission_source: Path | None = None,
          mission_translation: Path | None = None,
          mission_template_report: Path | None = None) -> dict:
    source = read_json(source_path)
    translation = read_json(translation_path)
    if font_mode not in FONT_MODES:
        raise ValueError(f"invalid font mode: {font_mode}")
    result = {"text": {}, "i18n": {}, "i18n_by_source": {},
              "numeric_text_templates": compile_templates(
                  source.get("numeric_text_templates", {}),
                  translation.get("numeric_text_templates", {})),
              "home_activity_names": compile_names(
                  source.get("home_activity_names", {}),
                  translation.get("home_activity_names", {})),
              "dump": dump, "font": font,
              "font_mode": font_mode}
    mission_origins = {}
    if (mission_source is None) != (mission_translation is None):
        raise ValueError("Mission numeric templates require source and translation paths together")
    if mission_source is not None:
        derived, origins = mission_numeric_templates.derive(mission_source, mission_translation)
        result["numeric_text_templates"] = mission_numeric_templates.merge(
            result["numeric_text_templates"], derived)
        mission_origins = origins
    if legal_runtime is not None and legal_runtime.exists():
        import hashlib
        runtime = json.loads(legal_runtime.read_text(encoding="utf-8"))
        originals = json.loads(legal_source.read_text(encoding="utf-8")) if legal_source else {}
        if (runtime.get("schema_version") != 1 or not isinstance(runtime.get("rules"), dict)
                or originals.get("schema_version") != 1):
            raise ValueError("Invalid legal runtime/source schema")
        legal = {}
        for rule_type, rule in runtime["rules"].items():
            if rule_type not in {str(i) for i in range(1, 8)} or not isinstance(rule, dict):
                raise ValueError("Invalid legal rule type")
            original, target = rule.get("source"), rule.get("translation")
            if not isinstance(original, str) or not isinstance(target, str):
                raise ValueError(f"Invalid legal text: {rule_type}")
            if original != originals.get("rules", {}).get(rule_type, {}).get("source"):
                raise ValueError(f"Legal runtime source is stale: {rule_type}")
            for label, text in (("source", original), ("translation", target)):
                if (len(text.encode("utf-8")) > 1024 * 1024 or
                        hashlib.sha256(text.encode("utf-8")).hexdigest() != rule.get(label + "_sha256")):
                    raise ValueError(f"Legal runtime checksum mismatch: {rule_type}/{label}")
            if original:
                if not target.strip():
                    raise ValueError(f"Legal runtime has empty translation: {rule_type}")
                legal[rule_type] = {"source": original, "translation": target}
        result["legal_by_type"] = legal
    origins: dict[str, str] = {}
    conflicts: dict[str, dict] = {}

    def add_text(original: str, translated: str, origin: str) -> None:
        selected = result["text"].get(original)
        if selected is None:
            result["text"][original] = translated
            origins[original] = origin
        elif selected != translated:
            conflict = conflicts.setdefault(original, {
                "source": original,
                "selected": {"origin": origins[original], "translation": selected},
                "discarded": [],
            })
            conflict["discarded"].append({"origin": origin, "translation": translated})

    for kind in ("text", "i18n"):
        for key, translated in translation.get(kind, {}).items():
            if not isinstance(key, str):
                raise ValueError(f"{kind}: key must be a string")
            original = key if kind == "text" else source.get(kind, {}).get(key)
            if original is None:
                raise ValueError(f"i18n key {key!r} has no captured source text")
            validate(original, translated, f"{kind}:{key}")
            # Preserve the reviewed source and identity decisions. A key such
            # as graphic.VeryHigh may intentionally retain 最高 while dialogue
            # translates the same characters differently.
            if kind == "i18n" and translated:
                result["i18n_by_source"].setdefault(key, {})[original] = translated
            if translated and translated != original:
                if kind == "text":
                    add_text(key, translated, "ui-text")
                else:
                    result[kind][key] = translated
    # This prompt inserts an untranslated work name after I18n resolution.
    # Expand only complete prompts for reviewed names, never the bare name.
    duration_key = "activity.characterSelect.duration"
    duration_source = source.get("i18n", {}).get(duration_key, "")
    duration_target = translation.get("i18n", {}).get(duration_key, "")
    if (duration_source.count("{0}") == 1 and duration_target.count("{0}") == 1 and
            "{" not in duration_source.replace("{0}", "") and
            "{" not in duration_target.replace("{0}", "")):
        for name, translated_name in result["home_activity_names"].items():
            rendered = duration_target.replace("{0}", translated_name)
            for template in (duration_source, duration_target):
                original = template.replace("{0}", name)
                if original != rendered:
                    add_text(original, rendered, "activity-duration")
    # The notice list omits the preview marker from its card title even when
    # the server-supplied title includes it. Keep an explicit translation for
    # the short title authoritative when one exists.
    preview_source = "【予告】"
    preview_target = "【预告】"
    for original, translated in notice_headings or []:
        validate(original, translated, f"notice_heading:{original}")
        if translated and translated != original:
            add_text(original, translated, "notice-heading")
    for original, translated in [*translation.get("text", {}).items(),
                                 *(notice_headings or [])]:
        if (original.startswith(preview_source) and
                translated.startswith(preview_target) and
                len(original) > len(preview_source) and
                len(translated) > len(preview_target)):
            short_source = original[len(preview_source):]
            short_target = translated[len(preview_target):]
            validate(short_source, short_target, f"notice_preview:{short_source}")
            if short_source != short_target:
                add_text(short_source, short_target, "notice-preview-alias")
    for key, variants in translation.get("i18n_by_source", {}).items():
        if not isinstance(key, str) or not isinstance(variants, dict):
            raise ValueError("i18n_by_source must map keys to source/translation objects")
        for original, translated in variants.items():
            if not isinstance(original, str):
                raise ValueError(f"i18n_by_source:{key}: source must be a string")
            validate(original, translated, f"i18n_by_source:{key}")
            if translated:
                result["i18n_by_source"].setdefault(key, {})[original] = translated
    if term_glossary is not None and term_glossary.exists():
        with term_glossary.open(encoding="utf-8") as stream:
            terms = json.load(stream)
        if not isinstance(terms, dict) or any(
                not isinstance(source, str) or not isinstance(value, str) or not value
                for source, value in terms.items()):
            raise ValueError(f"{term_glossary}: invalid term glossary")
        for original, translated in terms.items():
            validate(original, translated, f"term_glossary:{original}")
            if translated != original:
                add_text(original, translated, "term-glossary")
    if master_text is not None and master_text.exists():
        master = read_json(master_text)
        for group in ("text", "aliases"):
            values = master.get(group, {})
            if not isinstance(values, dict):
                raise ValueError(f"{master_text}: {group} must be a dictionary")
            for original, translated in values.items():
                validate(original, translated, f"master_{group}:{original}")
                if translated and translated != original:
                    add_text(original, translated, f"master-{group}")
    for original, translated in (card_display_aliases or {}).items():
        validate(original, translated, f"card_display:{original}")
        if translated and translated != original:
            add_text(original, translated, "card-display")
    if story_titles is not None and story_titles.exists():
        with story_titles.open(encoding="utf-8") as stream:
            titles = json.load(stream)
        if not isinstance(titles, dict):
            raise ValueError(f"{story_titles}: expected title mapping")
        for original, translated in titles.items():
            if not isinstance(original, str) or not isinstance(translated, str):
                raise ValueError(f"{story_titles}: title mapping must contain strings")
            validate(original, translated, f"story_titles:{original}")
            if translated and translated != original:
                add_text(original, translated, "story-title")
    if name_glossary is not None and name_glossary.exists():
        with name_glossary.open(encoding="utf-8") as stream:
            names = json.load(stream)
        if not isinstance(names, dict) or any(
                not isinstance(source, str) or not isinstance(value, str) or not value
                for source, value in names.items()):
            raise ValueError(f"{name_glossary}: invalid name glossary")
        for source, value in names.items():
            if source != value:
                add_text(source, value, "name-glossary")
    # A reviewed UI text entry is an explicit override. Derived dictionaries
    # have no such authority: conflicting candidates stay in their scoped
    # data and must not pick a winner merely because a file was loaded first.
    for original, conflict in conflicts.items():
        # A generated preview-prefix alias is weaker evidence than an exact
        # heading actually supplied by the server. Do not turn that synthetic
        # mismatch into an ambiguity in the real heading.
        only_synthetic_alternatives = (
            origins[original] != "notice-preview-alias" and
            all(item["origin"] == "notice-preview-alias" for item in conflict["discarded"]))
        if origins[original] != "ui-text" and not only_synthetic_alternatives:
            result["text"].pop(original, None)
            conflict["omitted_from_global"] = True
    result["translated_values"] = story_values(story_dir, name_glossary)
    if conflict_report is not None:
        write_json(conflict_report, {"conflicts": [conflicts[source]
                                                  for source in sorted(conflicts)]})
    if mission_template_report is not None:
        write_json(mission_template_report, {"schema_version": 1, "sources": mission_origins})
    write_json(output_path, result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    collect_cmd = sub.add_parser("collect", help="merge dump.jsonl into translations/ui/source.json")
    collect_cmd.add_argument("dump", type=Path)
    collect_cmd.add_argument("--source", type=Path, default=SOURCE)
    collect_cmd.add_argument("--kind", choices=("text", "i18n"),
                             default="i18n", help="Collect i18n by default; text must be explicit")
    collect_cmd.add_argument("--review-report", type=Path,
                             default=ROOT / ".analysis/i18n-source-candidates.json",
                             help="Write newly observed i18n source variants for local review")
    collect_cmd.add_argument("--reviewed-variants", type=Path,
                             default=ROOT / "translations/ui/variants-source.json")
    build_cmd = sub.add_parser("build", help="validate and compile zh-Hans.json")
    build_cmd.add_argument("--source", type=Path, default=SOURCE)
    build_cmd.add_argument("--translations", type=Path, default=TRANSLATION)
    build_cmd.add_argument("--story-dir", type=Path, default=STORY,
                           help="sparse story patches whose translated text needs the CJK font")
    build_cmd.add_argument("--name-glossary", type=Path, default=NAME_GLOSSARY,
                           help="speaker-name translations whose rendered text needs the CJK font")
    build_cmd.add_argument("--term-glossary", type=Path, default=TERM_GLOSSARY,
                           help="canonical translations for group and franchise terms")
    build_cmd.add_argument("--master-text", type=Path, default=MASTER_TEXT,
                           help="unambiguous source translations exported from MasterDB")
    build_cmd.add_argument("--mission-source", type=Path,
                           default=ROOT / "translations/master/orig/Mission.json")
    build_cmd.add_argument("--mission-translations", type=Path,
                           default=ROOT / "translations/master/zh-Hans/Mission.json")
    build_cmd.add_argument("--mission-template-report", type=Path,
                           default=ROOT / ".analysis/mission-template-sources.json")
    build_cmd.add_argument("--story-title-map", type=Path, default=STORY_TITLES,
                           help="conflict-free titles extracted from translated adventure CSV")
    build_cmd.add_argument("--output", type=Path, default=OUTPUT)
    build_cmd.add_argument("--dump", action="store_true", help="record new original strings in game")
    build_cmd.add_argument("--no-font", action="store_true", help="disable CJK font handling")
    build_cmd.add_argument("--font-mode", choices=FONT_MODES,
                           default=DEFAULT_FONT_MODE, help="how to apply the CJK font to TextMeshPro text")
    args = parser.parse_args()
    try:
        if args.command == "collect":
            changes = collect(args.dump, args.source, args.kind,
                              args.review_report, args.reviewed_variants)
            print(f"Updated {changes} source entries in {args.source}")
            if args.kind == "i18n":
                report = read_json(args.review_report)
                print(f"Review {len(report['candidates'])} new source variants in {args.review_report}; "
                      f"recovered {len(report['recovered_nul_lines'])} NUL-padded dump lines")
        else:
            result = build(args.source, args.translations, args.output, args.dump,
                           not args.no_font, args.font_mode, args.story_dir,
                           args.name_glossary, args.story_title_map,
                           args.term_glossary, args.master_text,
                           mission_source=args.mission_source,
                           mission_translation=args.mission_translations,
                           mission_template_report=args.mission_template_report)
            variant_count = sum(len(values) for values in result["i18n_by_source"].values())
            print(f"Built {len(result['text'])} text, {len(result['i18n'])} i18n, "
                  f"{variant_count} source variants, "
                  f"and {len(result['translated_values'])} story values in {args.output}")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
