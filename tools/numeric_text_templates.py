"""Validate one-number UI templates shared by the compiler and runtime audit."""

import re


TOKEN = '{number}'
ASCII_NUMBER = re.compile(r'[0-9]{1,6}', re.ASCII)


def split_template(value: str, *, source: bool) -> tuple[str, str]:
    if not isinstance(value, str) or value.count(TOKEN) != 1 or '\0' in value:
        raise ValueError('numeric template requires exactly one {number} token')
    prefix, suffix = value.split(TOKEN)
    if '{' in prefix + suffix or '}' in prefix + suffix:
        raise ValueError('numeric template cannot contain other placeholders')
    if len(value.encode('utf-8')) > 256:
        raise ValueError('numeric template exceeds 256 UTF-8 bytes')
    if source and len((prefix + suffix).encode('utf-8')) < 12:
        raise ValueError('numeric source template needs at least 12 literal bytes')
    return prefix, suffix


def compile_templates(source: dict, translation: dict) -> dict[str, str]:
    if not isinstance(source, dict) or not isinstance(translation, dict):
        raise ValueError('numeric_text_templates must be objects')
    if len(source) > 128 or set(source) != set(translation):
        raise ValueError('numeric templates must be fully paired and at most 128')
    result = {}
    for template, original in source.items():
        if not isinstance(template, str) or original != template:
            raise ValueError('numeric source template must map to itself')
        split_template(template, source=True)
        target = translation[template]
        split_template(target, source=False)
        result[template] = target
    return result


def match_template(value: str, template: str) -> bool:
    """Mirror C++ full-string matching; Unicode digits and names never match."""
    prefix, suffix = split_template(template, source=False)
    if not value.startswith(prefix) or not value.endswith(suffix):
        return False
    middle = value[len(prefix):len(value) - len(suffix) if suffix else len(value)]
    return ASCII_NUMBER.fullmatch(middle) is not None


def matches_any(value: str, templates: dict[str, str], *, rendered=False) -> bool:
    count = sum(match_template(value, target if rendered else source)
                for source, target in templates.items())
    return count > 0 if rendered else count == 1
