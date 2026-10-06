"""Reviewed names for home and in-work footer labels; levels remain runtime values."""
import re

PATH_PREFIX = ('WindowSystem/WindowRoot/HomeWindow(Clone)/HomeTopScreen(Clone)/'
               'ScreenCanvas/ScreenRoot/ActivityPage/SlideRoot/Parent/ActivityCells/')
PATHS = frozenset(PATH_PREFIX + slot + '/NameBase/Name'
                  for slot in ('FanEvent', 'Promotion', 'Refresh'))
PATHS |= frozenset('WindowSystem/WindowRoot/ActivityWindow(Clone)/' + screen +
                   'Screen(Clone)/ScreenCanvas/ScreenRoot/ActGameFooter/ActGameInfo/Level'
                   for screen in ('FanEvent', 'Promotion', 'Refresh'))
LEVEL_LABEL = re.compile(r'(.+) Lv([0-9]{1,6})\Z', re.ASCII)


def valid_name(value: str) -> bool:
    return (isinstance(value, str) and 1 <= len(value.encode('utf-8')) <= 64 and
            not any(ord(c) <= 32 or ord(c) == 127 or c in '<>{}' for c in value))


def compile_names(source: dict, translation: dict) -> dict[str, str]:
    if not isinstance(source, dict) or not isinstance(translation, dict):
        raise ValueError('home_activity_names must be objects')
    if len(source) > 64 or set(source) != set(translation):
        raise ValueError('home activity names must be fully paired and at most 64')
    for name, original in source.items():
        if not valid_name(name) or original != name or not valid_name(translation[name]):
            raise ValueError('home activity name must be a plain name of 1..64 UTF-8 bytes')
    for name, target in translation.items():
        if target in translation and target != name and translation[target] != target:
            raise ValueError('home activity name translations cannot chain or cycle')
    return dict(translation)


def may_match(value: str, names: dict[str, str]) -> bool:
    match = LEVEL_LABEL.fullmatch(value)
    return bool(match and (match[1] in names or match[1] in names.values()))


def translate(value: str, path: str, names: dict[str, str]) -> str | None:
    if path not in PATHS:
        return None
    match = LEVEL_LABEL.fullmatch(value)
    if not match or match[1] not in names:
        return None
    return names[match[1]] + ' Lv' + match[2]
