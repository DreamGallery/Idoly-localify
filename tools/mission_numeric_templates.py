"""Derive bounded, one-number templates from reviewed event mission fields.

Only Love and IDOLY PRIDE day missions are eligible. Runtime matching remains
full-string matching of one ASCII number; no character-name slots are inferred.
"""

import json
from pathlib import Path

from numeric_text_templates import compile_templates


PREFIXES = ("event-love-", "event-ms-idoly-pride-day-")
FIELDS = ("name", "shortDescription", "longDescription")
TOKEN = "{threshold}"


def derive(source_path: Path, translation_path: Path) -> tuple[dict, dict]:
    if not source_path.exists() and not translation_path.exists():
        return {}, {}  # Small pipeline fixtures may omit the entire Mission table.
    if not source_path.is_file() or not translation_path.is_file():
        raise ValueError("Mission numeric templates require both source and translation tables")
    source = json.loads(source_path.read_text(encoding="utf-8"))
    target = json.loads(translation_path.read_text(encoding="utf-8"))
    if not isinstance(source, dict) or not isinstance(target, dict):
        raise ValueError("Mission numeric template tables must be record dictionaries")
    templates, origins = {}, {}
    for record_id, fields in sorted(source.items()):
        if not record_id.startswith(PREFIXES):
            continue
        if not isinstance(fields, dict):
            raise ValueError(f"Mission/{record_id}: expected field dictionary")
        translated = target.get(record_id, {})
        if not isinstance(translated, dict):
            raise ValueError(f"Mission/{record_id}: expected translated field dictionary")
        for field in FIELDS:
            original = fields.get(field, "")
            if not isinstance(original, str):
                raise ValueError(f"Mission/{record_id}/{field}: expected source string")
            if TOKEN not in original:
                continue
            label = f"Mission/{record_id}/{field}"
            value = translated.get(field)
            if not isinstance(value, str) or not value.strip():
                raise ValueError(f"{label}: missing mission translation")
            # compile_templates additionally rejects any other brace placeholders,
            # too-short literals, NULs and templates exceeding the byte limit.
            if original.count(TOKEN) != 1 or value.count(TOKEN) != 1:
                raise ValueError(f"{label}: expected exactly one {{threshold}} in source and translation")
            template = original.replace(TOKEN, "{number}")
            translation = value.replace(TOKEN, "{number}")
            try:
                compile_templates({template: template}, {template: translation})
            except ValueError as error:
                raise ValueError(f"{label}: {error}") from error
            if template in templates and templates[template] != translation:
                raise ValueError(f"{label}: conflicting mission translation for {template!r}")
            templates[template] = translation
            origins.setdefault(template, []).append(label)
    return dict(sorted(templates.items())), dict(sorted(origins.items()))


def merge(manual: dict, derived: dict) -> dict:
    result = dict(manual)
    for template, translation in derived.items():
        if template in result and result[template] != translation:
            raise ValueError(f"Conflicting manual and mission numeric template: {template!r}")
        result[template] = translation
    # Apply the existing shared 128-template limit to the merged result.
    return compile_templates({template: template for template in result}, result)
