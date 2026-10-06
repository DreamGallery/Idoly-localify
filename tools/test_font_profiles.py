"""Contract tests for the editable TMP profile file."""

import copy
from pathlib import Path
import unittest
import shutil
import subprocess
import tempfile

from font_profiles import load, validate, render_header

PROFILE = Path(__file__).resolve().parents[1] / 'config/font-profiles.json'


class FontProfileTests(unittest.TestCase):
    def setUp(self):
        self.data = load(PROFILE)

    def test_previous_metadata_migration_preserves_parameters_and_validation(self):
        old = copy.deepcopy(self.data)
        old['font']['mode'] = 'preserve-non-cjk'
        old['profiles'][0]['adjust']['outline_width'] = 0.6
        upgraded = validate(old)
        self.assertEqual(upgraded['font']['mode'], 'replace-ui')
        self.assertEqual(upgraded['profiles'][0]['adjust']['outline_width'], 0.6)
        self.assertEqual(old['font']['mode'], 'preserve-non-cjk')
        old['profiles'][0]['path_suffix'] = ['Other']
        with self.assertRaises(ValueError):
            validate(old)

    def test_native_baseline_is_current(self):
        header = PROFILE.parent.parent / 'app/src/main/cpp/font_profile_defaults.h'
        self.assertEqual(header.read_text(), render_header(self.data))

    def test_retired_portrait_auto_size_migrates_without_resetting_adjustments(self):
        old = copy.deepcopy(self.data)
        adjust = next(p['adjust'] for p in old['profiles']
                      if p['id'] == 'portrait_love_event_name')
        adjust['auto_size_when_characters_gt'] = 2
        adjust['font_size_ratio'] = 1.1
        old['profiles'][0]['adjust']['outline_width'] = 0.6
        upgraded = validate(old)
        portrait = next(p['adjust'] for p in upgraded['profiles']
                        if p['id'] == 'portrait_love_event_name')
        self.assertNotIn('auto_size_when_characters_gt', portrait)
        self.assertEqual(portrait['font_size_ratio'], 1.1)
        self.assertEqual(upgraded['profiles'][0]['adjust']['outline_width'], 0.6)
        self.assertEqual(adjust['auto_size_when_characters_gt'], 2)
        for invalid in (True, -1, 21, 2.5, '2'):
            adjust['auto_size_when_characters_gt'] = invalid
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                validate(old)

    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_native_rejects_invalid_update_without_mutation(self):
        root = PROFILE.parent.parent
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'font-profile-test'
            subprocess.run(['c++', '-std=c++17', '-I', str(root/'app/src/main/cpp'),
                            '-I', str(root/'app/src/main/cpp/third_party'),
                            str(root/'tools/test_font_profile_runtime.cpp'), '-o', str(binary)],
                           check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)

    def test_checked_in_profiles_are_valid_and_page_scoped(self):
        self.assertEqual(len(self.data['profiles']), 19)
        self.assertEqual(self.data['font']['mode'], 'replace-ui')
        self.assertEqual(self.data['profiles'][-1]['adjust'], {})
        self.assertTrue(all(profile['page'] and profile['path_suffix']
                            for profile in self.data['profiles']))

    def test_unknown_fields_and_unsupported_adjustment_rejected(self):
        changed = copy.deepcopy(self.data)
        changed['profiles'][0]['adjust']['screen_pixels'] = 10
        with self.assertRaisesRegex(ValueError, 'unknown fields'):
            validate(changed)
        changed = copy.deepcopy(self.data)
        changed['profiles'][0]['adjust']['font_size_ratio'] = 0.9
        with self.assertRaisesRegex(ValueError, 'unsupported parameter'):
            validate(changed)

    def test_unscoped_selector_and_missing_guard_rejected(self):
        changed = copy.deepcopy(self.data)
        changed['profiles'][0]['path_suffix'] = ['*']
        with self.assertRaisesRegex(ValueError, 'invalid path_suffix'):
            validate(changed)
        changed = copy.deepcopy(self.data)
        del changed['profiles'][0]['guard']['original_outline_width']
        with self.assertRaisesRegex(ValueError, 'required guard'):
            validate(changed)

    def test_rejects_invalid_ranges_and_types(self):
        changed = copy.deepcopy(self.data)
        changed['profiles'][0]['adjust']['outline_width'] = 1.2
        with self.assertRaisesRegex(ValueError, 'finite number'):
            validate(changed)
        changed = copy.deepcopy(self.data)
        changed['profiles'][6]['adjust']['disable_auto_size'] = 1
        with self.assertRaisesRegex(ValueError, 'boolean'):
            validate(changed)
        changed = copy.deepcopy(self.data)
        changed['profiles'][0]['guard']['original_font_size_unity'] = [58, 50]
        with self.assertRaisesRegex(ValueError, 'inverted'):
            validate(changed)
        changed = copy.deepcopy(self.data)
        changed['profiles'][3]['guard']['rect_size_unity'] = [0, 168]
        with self.assertRaisesRegex(ValueError, 'zero dimension'):
            validate(changed)

    def test_captured_labels_keep_full_page_paths_and_width_bounds(self):
        by_id = {p['id']: p for p in self.data['profiles']}
        for name in ('costume_preview_hide_ui', 'costume_preview_reaction', 'costume_selection_hide_ui',
                     'costume_selection_reaction', 'costume_selection_determine', 'shop_new_arrivals'):
            profile = by_id[name]
            self.assertGreater(len(profile['path_suffix']), 5)
            self.assertEqual(profile['adjust']['alignment'], 'midline-center')
            self.assertLessEqual(profile['adjust']['content_width_ratio'], 1)
        changed = copy.deepcopy(self.data)
        target = next(p for p in changed['profiles'] if p['id'] == 'shop_new_arrivals')
        target['adjust']['content_width_ratio'] = 1.1
        with self.assertRaisesRegex(ValueError, 'finite number'):
            validate(changed)
        target['adjust']['content_width_ratio'] = 0.85
        target['path_suffix'] = ['Market', 'Noti', 'Text']
        with self.assertRaisesRegex(ValueError, 'unsupported profile selector'):
            validate(changed)

    def test_unique_profiles_and_version_required(self):
        changed = copy.deepcopy(self.data)
        changed['profiles'][1]['id'] = changed['profiles'][0]['id']
        with self.assertRaisesRegex(ValueError, 'duplicate id'):
            validate(changed)
        changed = copy.deepcopy(self.data)
        changed['schema_version'] = 2
        with self.assertRaisesRegex(ValueError, 'schema version'):
            validate(changed)


if __name__ == '__main__':
    unittest.main()
