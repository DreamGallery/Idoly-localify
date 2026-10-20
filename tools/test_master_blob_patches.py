import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

from master_blob_patches import encrypted_pack, field_slot, input_snapshot, patch_blob, retained_conflicts
from master_identity import rules_metadata
from master_field_paths import field_rules_metadata


class FakeMessage:
    def __init__(self, payload):
        self.id = payload["id"]
        self.name = payload["name"]
        self.choices = [SimpleNamespace(text=item) for item in payload["choices"]]

    @classmethod
    def FromString(cls, blob):
        return cls(json.loads(blob))

    def SerializeToString(self):
        return json.dumps({"id": self.id, "name": self.name,
                           "choices": [choice.text for choice in self.choices]},
                          ensure_ascii=False, separators=(",", ":")).encode("utf-8")


class MasterBlobPatchTests(unittest.TestCase):
    def test_cached_snapshot_rejects_interrupted_migration_and_unknown_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tag, orig, zh = root / "tag.pb", root / "master/orig", root / "master/zh-Hans"
            orig.mkdir(parents=True)
            zh.mkdir()
            tag.write_bytes(b"tag")
            for path in (orig / "Story.json", zh / "Story.json"):
                path.write_text("{}")
            snapshot = input_snapshot(tag, orig, zh, {"Story"})
            self.assertEqual(snapshot["record_keys"], rules_metadata())
            self.assertEqual(snapshot["field_keys"], field_rules_metadata())
            marker = orig.parent / ".master-key-migration-in-progress.json"
            marker.write_text("{}")
            with self.assertRaisesRegex(ValueError, "interrupted"):
                input_snapshot(tag, orig, zh, {"Story"})
            marker.unlink()
            for filename in ("record-keys.json", "field-keys.json"):
                schema = orig.parent / filename
                schema.write_text('{"schema_version":999}')
                with self.assertRaisesRegex(ValueError, "unsupported"):
                    input_snapshot(tag, orig, zh, {"Story"})
                schema.unlink()

    def test_encrypted_pack_cache_is_bound_to_tag_and_hash(self):
        class Request:
            calls = 0

            @classmethod
            def get(cls, url, timeout):
                cls.calls += 1
                self.assertEqual((url, timeout), ("https://example.test/Music", 60))
                return SimpleNamespace(content=b"ciphered", raise_for_status=lambda: None)

        pack = SimpleNamespace(type="Music", downloadUrl="https://example.test/Music",
                               fileSize=8, cryptoKey="secret")
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory)
            first = encrypted_pack(pack, "v1", cache, Request)
            self.assertEqual(first.read_bytes(), b"ciphered")
            self.assertEqual(encrypted_pack(pack, "v1", cache, Request), first)
            self.assertEqual(Request.calls, 1)
            first.write_bytes(b"corrupt!")
            self.assertEqual(encrypted_pack(pack, "v1", cache, Request).read_bytes(),
                             b"ciphered")
            self.assertEqual(Request.calls, 2)
            self.assertNotEqual(encrypted_pack(pack, "v2", cache, Request), first)
            self.assertEqual(Request.calls, 3)
            first.with_suffix(".sha256").write_bytes(b"\xff\xfe")
            self.assertEqual(encrypted_pack(pack, "v1", cache, Request).read_bytes(),
                             b"ciphered")
            self.assertEqual(Request.calls, 4)
            with self.assertRaisesRegex(ValueError, "cache is missing"):
                encrypted_pack(pack, "v3", cache, Request, offline=True)
            self.assertEqual(Request.calls, 4)

    def test_retained_conflicts_need_global_text_suppression(self):
        reviewed = {"Music": {"one": {
            "singer": ("同じ", "同一"), "composer": ("同じ", "同じ")}},
            "Story": {"two": {"name": ("別", "其他")}}}
        self.assertEqual(retained_conflicts(reviewed), ["同じ"])

    def test_patch_validates_and_changes_only_selected_fields(self):
        original = FakeMessage({"id": "one", "name": "原題",
                                "choices": ["元の選択", "維持"]}).SerializeToString()
        output = patch_blob(original, FakeMessage, "one", {
            "name": ("原題", "译名"),
            "choices[0].text": ("元の選択", "新的选择"),
            "choices[1].text": ("維持", "維持")}, "Story")
        message = FakeMessage.FromString(output)
        self.assertEqual((message.id, message.name,
                          [choice.text for choice in message.choices]),
                         ("one", "译名", ["新的选择", "維持"]))
        with self.assertRaisesRegex(ValueError, "source changed"):
            patch_blob(original, FakeMessage, "one", {
                "name": ("別の題", "译名")}, "Story")

    def test_rejects_invalid_path_and_noncanonical_original(self):
        message = FakeMessage({"id": "one", "name": "原題", "choices": []})
        with self.assertRaisesRegex(ValueError, "index out of range"):
            field_slot(message, "choices[0].text")
        with self.assertRaisesRegex(ValueError, "round-trip"):
            patch_blob('{ "id": "one", "name": "原題", "choices": [] }'.encode(),
                       FakeMessage, "one", {}, "Story")


if __name__ == "__main__":
    unittest.main()
