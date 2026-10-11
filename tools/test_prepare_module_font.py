"""Pinned font downloads and failed preparation must not replace good assets."""
from io import BytesIO
import gzip
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

import prepare_module_font as fonts


class ModuleFontTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        builder = FontBuilder(1000, isTTF=True)
        builder.setupGlyphOrder(['.notdef', 'A'])
        builder.setupCharacterMap({65: 'A'})
        builder.setupGlyf({name: TTGlyphPen(None).glyph() for name in ('.notdef', 'A')})
        builder.setupHorizontalMetrics({name: (1000, 0) for name in ('.notdef', 'A')})
        builder.setupHorizontalHeader(ascent=800, descent=-200)
        builder.setupNameTable({'familyName': 'Noto Sans CJK SC', 'styleName': 'Bold'})
        builder.setupOS2(usWeightClass=700)
        builder.setupPost()
        stream = BytesIO()
        builder.save(stream)
        self.data = stream.getvalue()
        self.spec = {'family': 'Noto Sans CJK SC', 'style': 'Bold', 'weight': 700,
                     'sha256': hashlib.sha256(self.data).hexdigest(),
                     'url': 'https://raw.githubusercontent.com/notofonts/noto-cjk/pinned/font.otf'}

    def test_download_is_hash_checked_and_cached_without_repeating_request(self):
        with patch.object(fonts.urllib.request, 'urlopen', return_value=BytesIO(self.data)) as get:
            self.assertEqual(fonts.resolve_font(self.spec, self.root), self.data)
            self.assertEqual(fonts.resolve_font(self.spec, self.root), self.data)
            get.assert_called_once()

    def test_bad_download_or_corrupt_cache_cannot_silently_select_another_font(self):
        with patch.object(fonts.urllib.request, 'urlopen', return_value=BytesIO(b'wrong')):
            with self.assertRaisesRegex(ValueError, 'SHA-256'):
                fonts.resolve_font(self.spec, self.root)
        cached = self.root / (self.spec['sha256'] + '.otf')
        self.assertFalse(cached.exists())
        cached.write_bytes(b'wrong cached file')
        with patch.object(fonts.urllib.request, 'urlopen') as get:
            with self.assertRaisesRegex(ValueError, 'SHA-256'):
                fonts.resolve_font(self.spec, self.root)
            get.assert_not_called()

    def test_verified_bytes_must_also_have_expected_family_and_weight(self):
        for key, bad in [('family', 'Other'), ('style', 'Regular'), ('weight', 500)]:
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'metadata'):
                fonts.verify_font(self.data, dict(self.spec, **{key: bad}))

    def test_local_only_spec_requires_a_pinned_file(self):
        spec = {key: value for key, value in self.spec.items() if key != 'url'}
        with patch.object(fonts.urllib.request, 'urlopen') as get:
            with self.assertRaisesRegex(ValueError, 'provide --font'):
                fonts.resolve_font(spec, self.root)
            local = self.root / 'selected.otf'
            local.write_bytes(self.data)
            self.assertEqual(fonts.resolve_font(spec, self.root, local), self.data)
            get.assert_not_called()

    def packaged_spec(self, payload=None):
        data = gzip.compress(self.data if payload is None else payload, mtime=0)
        path = self.root / 'font-sources/selected.otf.gz'
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(data)
        return dict(self.spec, source={'path': 'font-sources/selected.otf.gz', 'format': 'gzip',
                                      'sha256': hashlib.sha256(data).hexdigest()})

    def test_packaged_font_is_verified_and_prepared_without_network(self):
        spec = self.packaged_spec()
        with patch.object(fonts, 'ROOT', self.root), patch.object(fonts.urllib.request, 'urlopen') as get:
            self.assertEqual(fonts.resolve_font(spec, self.root / 'cache'), self.data)
            self.assertEqual(fonts.resolve_font(spec, self.root / 'cache'), self.data)
            get.assert_not_called()

    def test_packaged_source_and_decompressed_font_both_require_pinned_hashes(self):
        spec = self.packaged_spec()
        with patch.object(fonts, 'ROOT', self.root):
            (self.root / spec['source']['path']).write_bytes(b'corrupt archive')
            with self.assertRaisesRegex(ValueError, 'source SHA-256'):
                fonts.resolve_font(spec, self.root / 'cache')
            with self.assertRaisesRegex(ValueError, 'font SHA-256'):
                fonts.resolve_font(self.packaged_spec(b'wrong font'), self.root / 'cache')
        self.assertFalse((self.root / 'cache' / (spec['sha256'] + '.otf')).exists())

    def test_packaged_source_cannot_escape_project_or_decompress_without_limit(self):
        spec = self.packaged_spec(b'A' * 4096)
        with patch.object(fonts, 'ROOT', self.root), patch.object(fonts, 'MAX_FONT_BYTES', 1024):
            with self.assertRaisesRegex(ValueError, 'exceeds size limit'):
                fonts.resolve_font(spec, self.root / 'cache')
            for path in ('../outside.otf.gz', '/outside.otf.gz'):
                spec['source']['path'] = path
                with self.assertRaisesRegex(ValueError, 'Invalid packaged'):
                    fonts.resolve_font(spec, self.root / 'cache')

    def variable_spec(self, source):
        return dict(self.spec, source={
            'url': 'https://raw.githubusercontent.com/notofonts/noto-cjk/pinned/variable.otf',
            'sha256': hashlib.sha256(source).hexdigest()}, instance={'axes': {'wght': 600}})

    def test_variable_source_and_derived_face_are_both_cached_and_verified(self):
        source = b'pinned variable font fixture'
        spec = self.variable_spec(source)
        with patch.object(fonts.urllib.request, 'urlopen', return_value=BytesIO(source)) as get, \
                patch('instantiate_noto.instantiate_font', return_value=self.data) as instantiate:
            self.assertEqual(fonts.resolve_font(spec, self.root), self.data)
            self.assertEqual(fonts.resolve_font(spec, self.root), self.data)
            get.assert_called_once()
            instantiate.assert_called_once_with(source, spec)
        self.assertEqual((self.root / (spec['source']['sha256'] + '.otf')).read_bytes(), source)
        (self.root / (spec['sha256'] + '.otf')).unlink()
        with patch.object(fonts.urllib.request, 'urlopen') as get, \
                patch('instantiate_noto.instantiate_font', return_value=self.data):
            self.assertEqual(fonts.resolve_font(spec, self.root), self.data)
            get.assert_not_called()

    def test_bad_variable_download_is_rejected_before_instantiation(self):
        spec = self.variable_spec(b'pinned variable font fixture')
        with patch.object(fonts.urllib.request, 'urlopen', return_value=BytesIO(b'wrong')), \
                patch('instantiate_noto.instantiate_font') as instantiate:
            with self.assertRaisesRegex(ValueError, 'source SHA-256'):
                fonts.resolve_font(spec, self.root)
            instantiate.assert_not_called()
        self.assertFalse((self.root / (spec['sha256'] + '.otf')).exists())

    def test_bad_static_derivation_cannot_enter_the_output_cache(self):
        source = b'pinned variable font fixture'
        spec = self.variable_spec(source)
        with patch.object(fonts.urllib.request, 'urlopen', return_value=BytesIO(source)), \
                patch('instantiate_noto.instantiate_font', return_value=b'wrong derived font'):
            with self.assertRaisesRegex(ValueError, 'SHA-256'):
                fonts.resolve_font(spec, self.root)
        self.assertFalse((self.root / (spec['sha256'] + '.otf')).exists())

    def test_failed_template_validation_keeps_previous_output(self):
        config = self.root / 'source.json'
        config.write_text(json.dumps(self.spec))
        template = self.root / 'template.bundle'
        template.write_bytes(b'invalid template')
        output = self.root / 'module.bundle'
        output.write_bytes(b'previous good bundle')
        local = self.root / 'font.otf'
        local.write_bytes(self.data)
        with patch.object(fonts, 'replace_bundle_font', side_effect=ValueError('bad template')):
            with self.assertRaisesRegex(ValueError, 'bad template'):
                fonts.prepare(template, output, config=config, font_path=local)
        self.assertEqual(output.read_bytes(), b'previous good bundle')
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            fonts.prepare(template, template, config=config, font_path=local)


if __name__ == '__main__':
    unittest.main()
