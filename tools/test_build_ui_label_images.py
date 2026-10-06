from copy import deepcopy
import struct
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

from build_ui_label_images import fill_enclosed_transparency, mesh_mask, render_label, restore_solid_label_background, validate_rectangle_image


class LabelMeshTests(unittest.TestCase):
    def setUp(self):
        self.sprite = {'m_RD': {
            'm_VertexData': {
                'm_Channels': [{'stream': 0, 'offset': 0, 'format': 0, 'dimension': 3}],
                'm_VertexCount': 3,
                'm_DataSize': struct.pack('<9f', 0, 0, 0, 8, 0, 0, 0, 8, 0)},
            'm_IndexBuffer': struct.pack('<3H', 0, 1, 2),
            'm_SubMeshes': [{'topology': 0, 'indexCount': 3, 'firstByte': 0, 'baseVertex': 0}]}}
        self.packed = {'uvTransform': {'x': 1, 'y': 20, 'z': 1, 'w': 60}}

    def test_uv_coordinates_preserve_empty_triangle_area(self):
        mask = mesh_mask(self.sprite, self.packed, [20, 32, 29, 41], 100)
        self.assertEqual(mask.size, (9, 9))
        self.assertEqual(mask.getpixel((0, 0)), 255)
        self.assertEqual(mask.getpixel((8, 8)), 255)
        self.assertEqual(mask.getpixel((8, 0)), 0)

    def test_unknown_vertex_layout_is_rejected(self):
        for field, value in [('stream', 1), ('offset', 4), ('format', 1), ('dimension', 2)]:
            sprite = deepcopy(self.sprite)
            sprite['m_RD']['m_VertexData']['m_Channels'][0][field] = value
            with self.assertRaisesRegex(ValueError, 'position layout'):
                mesh_mask(sprite, self.packed, [20, 32, 29, 41], 100)

    def test_non_triangular_topology_is_rejected(self):
        self.sprite['m_RD']['m_SubMeshes'][0]['topology'] = 1
        with self.assertRaisesRegex(ValueError, 'triangular'):
            mesh_mask(self.sprite, self.packed, [20, 32, 29, 41], 100)

    def test_rectangle_rendering_requires_verified_image_layout(self):
        raw = bytearray(136)
        struct.pack_into('<iq', raw, 16, 1, 9016)
        validate_rectangle_image(raw)
        raw[128] = 1
        with self.assertRaisesRegex(ValueError, 'uses sprite mesh'):
            validate_rectangle_image(raw)
        with self.assertRaisesRegex(ValueError, 'serialized layout'):
            validate_rectangle_image(raw[:-1])

    def test_empty_label_is_rejected(self):
        args = (Path('unused.ttf'), [255] * 4, [255] * 4, Image.new('L', (20, 10), 255))
        with self.assertRaisesRegex(ValueError, 'nonempty text'):
            render_label((20, 10), {'text': ''}, *args)

    def test_closed_counter_filled_but_open_counter_and_outer_alpha_preserved(self):
        image = Image.new('RGBA', (20, 10))
        draw = ImageDraw.Draw(image)
        pink = (255, 109, 133, 255)
        draw.rectangle((2, 2, 7, 7), outline=pink)
        draw.rectangle((12, 2, 17, 7), outline=pink)
        image.putpixel((14, 2), (0, 0, 0, 0))  # Open shape must stay transparent.
        result = fill_enclosed_transparency(image, [255, 255, 255, 255])
        for y in range(10):
            for x in range(20):
                expected = (255, 255, 255, 255) if 2 < x < 7 and 2 < y < 7 else image.getpixel((x, y))
                self.assertEqual(result.getpixel((x, y)), expected)

    def test_solid_background_repair_preserves_outer_edges_and_alpha(self):
        background = [64, 204, 255, 255]
        image = Image.new('RGBA', (7, 3), (101, 101, 168, 0))
        image.putpixel((0, 1), (90, 140, 190, 180))
        image.putpixel((6, 1), (90, 140, 190, 180))
        image.putpixel((1, 1), tuple(background))
        image.putpixel((5, 1), tuple(background))
        for x in range(2, 5):
            image.putpixel((x, 1), (255, 255, 255, 255))
        result = restore_solid_label_background(image, background, 8)
        for y in range(3):
            for x in range(7):
                expected = tuple(background) if y == 1 and 1 <= x <= 5 else image.getpixel((x, y))
                self.assertEqual(result.getpixel((x, y)), expected)
        with self.assertRaisesRegex(ValueError, 'Unreviewed'):
            restore_solid_label_background(image, background, 30)


if __name__ == '__main__':
    unittest.main()
