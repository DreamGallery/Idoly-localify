from copy import deepcopy
import struct
import unittest
from pathlib import Path

from PIL import Image

from build_ui_label_images import mesh_mask, render_label, validate_rectangle_image


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


if __name__ == '__main__':
    unittest.main()
