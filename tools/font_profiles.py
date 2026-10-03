"""Validate the game 6.0.2 TMP font profile data before runtime packaging."""

import argparse
import copy
import json
import math
from pathlib import Path

from font_policy import DEFAULT_FONT_MODE, INDEPENDENT_FONT_NAME

PAGES = {'ordinary_story', 'portrait_love_event', 'home_talk', 'home',
         'card_support', 'costume_preview', 'shop', 'item_thumbnail', 'daily_roulette', 'generic_ui'}
ADJUSTMENTS = {
    'outline_width': (0, 1), 'face_dilate': (-1, 1),
    'font_size_ratio': (0.25, 2), 'font_size_base_unity': (1, 200),
    'line_spacing_delta_unity': (-100, 100), 'anchored_y_delta_unity': (-100, 100),
    'auto_size_when_characters_gt': (0, 20),
    'rebuild_parent_layout_below_width_unity': (1, 1000),
    'fit_font_to_width_unity': (1, 1000), 'font_size_max_unity': (1, 200),
    'font_size_unity': (1, 200), 'anchored_y_unity': (-1000, 1000),
    'character_spacing_unity': (-100, 100),
    'content_width_ratio': (0.1, 1),
}
GUARDS = {'original_font_size_unity', 'original_outline_width',
          'original_line_spacing_unity', 'rect_size_unity', 'rect_height_unity'}
COLORS = {'face_color_rgba', 'outline_color_rgba', 'vertex_color_rgba'}
BOOLEANS = {'rebind_font_before_render', 'disable_auto_size', 'outline_outward'}
PROFILE_SPECS = {
    'ordinary_story_subtitle': ('ordinary_story', ['Message Label'],
                                {'outline_width', 'face_dilate', 'font_size_unity',
                                 'character_spacing_unity'}),
    'ordinary_story_name': ('ordinary_story', ['Name Label'],
                            {'outline_width', 'face_dilate', 'face_color_rgba',
                             'outline_color_rgba', 'vertex_color_rgba'}),
    'portrait_love_event_name': ('portrait_love_event', ['Name Label', 'Text'],
                                 {'font_size_ratio', 'font_size_base_unity', 'alignment',
                                  'auto_size_when_characters_gt'}),
    'portrait_love_event_dialogue': ('portrait_love_event', ['Message Root', 'Message Text'],
                                     {'line_spacing_delta_unity', 'anchored_y_delta_unity'}),
    'home_talk_dialogue': ('home_talk', ['Bottom', 'SerifWIndow', 'Serif'],
                           {'font_size_ratio', 'anchored_y_delta_unity'}),
    'home_talk_name': ('home_talk', ['Bottom', 'NameHeader', 'Name'],
                       {'rebind_font_before_render', 'rebuild_parent_layout_below_width_unity'}),
    'home_venus_rank': ('home', ['HomeVenusRankButton', 'Button', 'Rank'],
                        {'disable_auto_size', 'fit_font_to_width_unity', 'font_size_max_unity'}),
    'home_company_rank': ('home', ['HomeCompanyButton', 'Button', 'Rank'],
                          {'disable_auto_size', 'fit_font_to_width_unity', 'font_size_max_unity'}),
    'card_support_label': ('card_support', ['Card', 'CardSupport', 'Label'],
                           {'font_size_unity', 'rect_size_unity', 'anchored_y_unity',
                            'character_spacing_unity'}),
    'costume_preview_hide_ui': ('costume_preview', ['Preview3dWindow(Clone)',
        'CostumeFittingRoomScreen(Clone)', 'ScreenCanvas', 'ScreenRoot', 'UIRoot',
        'Fotter', 'HideButton', 'Label'], {'alignment', 'font_size_max_unity', 'content_width_ratio'}),
    'costume_preview_reaction': ('costume_preview', ['Preview3dWindow(Clone)',
        'CostumeFittingRoomScreen(Clone)', 'ScreenCanvas', 'ScreenRoot', 'UIRoot',
        'Fotter', 'ReactionButton', 'Label'], {'alignment', 'font_size_max_unity', 'content_width_ratio'}),
    'costume_selection_hide_ui': ('costume_preview', ['CharacterWindow(Clone)',
        'CharacterCostumeScreen(Clone)', 'ScreenCanvas', 'ScreenRoot', 'UIRoot',
        'Fotter', 'HideButton', 'Label'], {'alignment', 'font_size_max_unity', 'content_width_ratio'}),
    'costume_selection_reaction': ('costume_preview', ['CharacterWindow(Clone)',
        'CharacterCostumeScreen(Clone)', 'ScreenCanvas', 'ScreenRoot', 'UIRoot',
        'Fotter', 'ReactionButton', 'Label'], {'alignment', 'font_size_max_unity', 'content_width_ratio'}),
    'costume_selection_determine': ('costume_preview', ['CharacterWindow(Clone)',
        'CharacterCostumeScreen(Clone)', 'ScreenCanvas', 'ScreenRoot', 'UIRoot',
        'Fotter', 'DetermineButton', 'Label'], {'alignment', 'font_size_max_unity', 'content_width_ratio'}),
    'shop_new_arrivals': ('shop', ['ShopWindow(Clone)', 'ShopTopScreen(Clone)', 'ScreenCanvas',
        'ScreenRoot', 'FullBottomVerticalLayoutScroll', 'Viewport', 'ScrollMask', 'Content',
        'MenuContainer', 'Market', 'Noti', 'Text'], {'alignment', 'font_size_max_unity', 'content_width_ratio'}),
    'item_floor_badge': ('item_thumbnail', ['ShowcaseToyThumbnail(Clone)',
        'SizeAdjustScaleRoot', 'ButtonScaleRoot', 'FrontRoot', 'ToyPutTypeLayout',
        'FloorLabel', 'Label'], {'font_size_max_unity', 'content_width_ratio'}),
    'item_wall_badge': ('item_thumbnail', ['ShowcaseToyThumbnail(Clone)',
        'SizeAdjustScaleRoot', 'ButtonScaleRoot', 'FrontRoot', 'ToyPutTypeLayout',
        'WallLabel', 'Label'], {'font_size_max_unity', 'content_width_ratio'}),
    'daily_roulette_description': ('daily_roulette', ['RouletteWindow(Clone)',
        'RouletteTopScreen(Clone)', 'ScreenCanvas', 'ScreenRoot', 'TopScreen',
        'RouletteDrawButton', 'DescriptionLabel'], {'outline_outward'}),
    'generic_ui': ('generic_ui', ['*'], set()),
}
PROFILE_GUARDS = {
    'ordinary_story_subtitle': {'original_font_size_unity', 'original_outline_width'},
    'ordinary_story_name': {'original_font_size_unity', 'original_outline_width'},
    'portrait_love_event_name': {'original_font_size_unity', 'rect_size_unity'},
    'portrait_love_event_dialogue': {'original_font_size_unity', 'rect_size_unity',
                                     'original_line_spacing_unity'},
    'home_talk_dialogue': {'original_font_size_unity', 'rect_size_unity'},
    'home_talk_name': {'original_font_size_unity', 'rect_height_unity'},
    'home_venus_rank': {'rect_size_unity'},
    'home_company_rank': {'rect_size_unity'},
    'card_support_label': {'original_font_size_unity'},
    'costume_preview_hide_ui': {'original_font_size_unity', 'rect_size_unity'},
    'costume_preview_reaction': {'original_font_size_unity', 'rect_size_unity'},
    'costume_selection_hide_ui': {'original_font_size_unity', 'rect_size_unity'},
    'costume_selection_reaction': {'original_font_size_unity', 'rect_size_unity'},
    'costume_selection_determine': {'original_font_size_unity', 'rect_size_unity'},
    'shop_new_arrivals': {'original_font_size_unity', 'rect_size_unity'},
    'item_floor_badge': {'original_font_size_unity', 'rect_size_unity'},
    'item_wall_badge': {'original_font_size_unity', 'rect_size_unity'},
    'daily_roulette_description': {'original_font_size_unity', 'rect_size_unity'},
    'generic_ui': set(),
}


def _object(value, allowed, label):
    if not isinstance(value, dict) or set(value) - allowed:
        raise ValueError(f'{label}: invalid or unknown fields')


def _number(value, low, high, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError(f'{label}: expected finite number in [{low}, {high}]')


def validate(data):
    _object(data, {'schema_version', 'game_version', 'font', 'profiles'}, 'root')
    if type(data.get('schema_version')) is not int or data.get('schema_version') != 1 or data.get('game_version') != '6.0.2':
        raise ValueError('expected schema version 1 for game 6.0.2')
    if data.get('font') == {'mode': 'preserve-non-cjk', 'unity_font_name': INDEPENDENT_FONT_NAME}:
        data = copy.deepcopy(data)
        data['font']['mode'] = DEFAULT_FONT_MODE
    font = data.get('font')
    _object(font, {'mode', 'unity_font_name'}, 'font')
    if font.get('mode') != DEFAULT_FONT_MODE or font.get('unity_font_name') != INDEPENDENT_FONT_NAME:
        raise ValueError('unsupported font selection')
    profiles = data.get('profiles')
    if not isinstance(profiles, list) or not profiles:
        raise ValueError('profiles must be a nonempty array')
    names = set()
    matches = set()
    for index, profile in enumerate(profiles):
        label = f'profiles[{index}]'
        _object(profile, {'id', 'page', 'path_suffix', 'guard', 'adjust'}, label)
        name, page = profile.get('id'), profile.get('page')
        if not isinstance(name, str) or not name.isidentifier() or name in names:
            raise ValueError(f'{label}: invalid or duplicate id')
        names.add(name)
        if page not in PAGES:
            raise ValueError(f'{label}: unsupported page')
        path = profile.get('path_suffix')
        if (not isinstance(path, list) or not 1 <= len(path) <= 16 or
                any(not isinstance(part, str) or not part or '/' in part or
                    (part == '*' and page != 'generic_ui') for part in path)):
            raise ValueError(f'{label}: invalid path_suffix')
        if page == 'generic_ui' and (path != ['*'] or profile.get('guard') != {} or profile.get('adjust') != {}):
            raise ValueError(f'{label}: generic UI must be an unmodified fallback')
        if name not in PROFILE_SPECS or (page, path) != PROFILE_SPECS[name][:2]:
            raise ValueError(f'{label}: unsupported profile selector')
        match = (page, tuple(path))
        if match in matches:
            raise ValueError(f'{label}: duplicate page/path selector')
        matches.add(match)
        guard = profile.get('guard')
        _object(guard, GUARDS, f'{label}.guard')
        if set(guard) != PROFILE_GUARDS[name]:
            raise ValueError(f'{label}.guard: required guard set changed')
        for key, value in guard.items():
            if not isinstance(value, list) or len(value) != 2:
                raise ValueError(f'{label}.{key}: expected a pair')
            low, high = value
            bounds = (0, 1000) if key != 'original_outline_width' else (0, 1)
            _number(low, *bounds, f'{label}.{key}[0]')
            _number(high, *bounds, f'{label}.{key}[1]')
            if key != 'rect_size_unity' and low > high:
                raise ValueError(f'{label}.{key}: inverted interval')
            if key == 'rect_size_unity' and (low == 0 or high == 0):
                raise ValueError(f'{label}.{key}: zero dimension')
        adjust = profile.get('adjust')
        _object(adjust, set(ADJUSTMENTS) | COLORS | BOOLEANS |
                {'alignment', 'rect_size_unity'}, f'{label}.adjust')
        if set(adjust) != PROFILE_SPECS[name][2]:
            raise ValueError(f'{label}.adjust: unsupported parameter or missing required parameter')
        for key, value in adjust.items():
            if key in ADJUSTMENTS:
                _number(value, *ADJUSTMENTS[key], f'{label}.{key}')
                if key == 'auto_size_when_characters_gt' and not isinstance(value, int):
                    raise ValueError(f'{label}.{key}: expected integer')
            elif key in COLORS:
                if not isinstance(value, list) or len(value) != 4:
                    raise ValueError(f'{label}.{key}: expected RGBA array')
                for component in value:
                    _number(component, 0, 1, f'{label}.{key}')
            elif key in BOOLEANS and not isinstance(value, bool):
                raise ValueError(f'{label}.{key}: expected boolean')
            elif key == 'alignment' and value != 'midline-center':
                raise ValueError(f'{label}.alignment: unsupported alignment')
            elif key == 'rect_size_unity':
                if not isinstance(value, list) or len(value) != 2:
                    raise ValueError(f'{label}.{key}: expected width and height')
                for dimension in value:
                    _number(dimension, 1, 1000, f'{label}.{key}')
        if 'font_size_ratio' in adjust and 'font_size_base_unity' in adjust and page != 'portrait_love_event':
            raise ValueError(f'{label}: base size only supported for portrait name')
    if names != set(PROFILE_SPECS):
        raise ValueError('all standard profile IDs are required')
    return data


def load(path: Path):
    return validate(json.loads(path.read_text(encoding='utf-8')))


def render_header(data):
    data = validate(data)
    encode = lambda value: json.dumps(value, ensure_ascii=False, separators=(',', ':'))
    return ('// Generated by tools/font_profiles.py --write-header; do not edit.\n'
            '#pragma once\n'
            'inline constexpr const char* kFontProfileDefaults = R"fontprofile('
            + encode(data) + ')fontprofile";\n'
            'inline constexpr const char* kFontProfileBounds = R"fontprofile('
            + encode(ADJUSTMENTS) + ')fontprofile";\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=Path, nargs='?',
                        default=Path(__file__).resolve().parents[1] / 'config/font-profiles.json')
    parser.add_argument('--write-header', action='store_true',
                        help='Regenerate the checked-in native baseline after validation')
    args = parser.parse_args()
    data = load(args.path)
    if args.write_header:
        header = Path(__file__).resolve().parents[1] / 'app/src/main/cpp/font_profile_defaults.h'
        header.write_text(render_header(data), encoding='utf-8')
    print(f"Valid font profile schema v{data['schema_version']}: {len(data['profiles'])} profiles")


if __name__ == '__main__':
    main()
