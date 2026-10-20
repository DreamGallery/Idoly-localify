import json
from pathlib import Path
import tempfile
import unittest

from master_field_paths import (
    FieldPaths, NESTED_KEY_RULES, field_rules_metadata, validate_field_path,
)
from master_identity import IdentityError
from master_json import export, iter_fields, read_table


class FieldPathTests(unittest.TestCase):
    def test_reorder_insert_and_delete_follow_identity_not_equal_text(self):
        a = {"messageDetailId": "a", "text": "同じ原文"}
        b = {"messageDetailId": "b", "text": "同じ原文"}
        original = FieldPaths("Message", {"details": [a, b]})
        key = original.canonicalize("details[1].text")
        self.assertEqual(key, 'details[key={"messageDetailId":"b"}].text')
        reordered = FieldPaths("Message", {"details": [
            b, {"messageDetailId": "new", "text": "新しい文章"}, a]})
        self.assertIs(reordered.locate(key)[0], b)
        self.assertEqual(reordered.canonicalize("details[0].text"), key)
        with self.assertRaisesRegex(IdentityError, "absent"):
            FieldPaths("Message", {"details": [a]}).locate(key)

    def test_parent_scope_and_composite_zero_identity(self):
        for text in ("ひとつ", "ふたつ"):
            path = FieldPaths("Tutorial", {"stepInfo": [
                {"step": 0, "subStep": 1, "texts": [text, "そのまま"]},
                {"step": 0, "subStep": 2, "texts": ["別の文章"]},
            ]})
            key = path.canonicalize("stepInfo[0].texts[1]")
            self.assertEqual(key, 'stepInfo[key={"step":0,"subStep":1}].texts[1]')
            self.assertEqual(path.locate(key)[2], "そのまま")
            validate_field_path("Tutorial", key)

    def test_unknown_list_and_string_list_keep_numeric_positions(self):
        path = FieldPaths("Unknown", {"choices": [{"text": "選択"}], "texts": ["原文"]})
        self.assertEqual(path.canonicalize("choices[0].text"), "choices[0].text")
        self.assertEqual(path.canonicalize("texts[0]"), "texts[0]")
        self.assertEqual(path.locate("texts[0]")[2], "原文")
        with self.assertRaisesRegex(IdentityError, "no reviewed"):
            path.locate('choices[key={"id":"x"}].text')

    def test_asset_identity_changes_do_not_fall_back_to_equal_text(self):
        old = FieldPaths("HomeTalk", {"characterTalks": [{"voiceAssetId": "old", "text": "同じ"}]})
        key = old.canonicalize("characterTalks[0].text")
        with self.assertRaisesRegex(IdentityError, "absent"):
            FieldPaths("HomeTalk", {"characterTalks": [
                {"voiceAssetId": "new", "text": "同じ"}]}).locate(key)

    def test_escaping_keeps_address_unambiguous(self):
        identity = '引号".方括号[0],反斜线\\\n'
        paths = FieldPaths("Message", {"details": [{"messageDetailId": identity, "text": "文章"}]})
        key = paths.canonicalize("details[0].text")
        self.assertEqual(paths.locate(key)[2], "文章")
        validate_field_path("Message", key)

    def test_missing_empty_duplicate_and_wrong_type_fail(self):
        cases = [
            [{"text": "文章"}],
            [{"messageDetailId": "", "text": "文章"}],
            [{"messageDetailId": 1, "text": "文章"}],
            [{"messageDetailId": "a", "text": "文章"},
             {"messageDetailId": "a", "text": "別の文章"}],
        ]
        for rows in cases:
            with self.subTest(rows=rows), self.assertRaises(IdentityError):
                FieldPaths("Message", {"details": rows}).canonicalize("details[0].text")

    def test_legacy_positions_and_noncanonical_selectors_fail_in_consumers(self):
        bad = [
            "levels[0].description",
            'levels[key={"level":true}].description',
            'levels[key={"level":"1"}].description',
            'levels[key={"level":1.0}].description',
            'levels[key={ "level":1}].description',
            'levels[key={"level":1,"level":2}].description',
            'levels[key={"unknown":1}].description',
            "levels[-1].description",
            "levels[01].description",
            "levels.description",
            "levels[0]..description",
        ]
        for path in bad:
            with self.subTest(path=path), self.assertRaises(IdentityError):
                validate_field_path("Skill", path)

    def test_all_declared_rules_resolve_json_and_schema_metadata(self):
        self.assertEqual(len(NESTED_KEY_RULES), 16)
        for (table, array), rule in NESTED_KEY_RULES.items():
            child = {name: "item" if kind == "string" else 0 for name, kind in rule}
            child["text"] = "原文"
            paths = FieldPaths(table, {array: [child]})
            key = paths.canonicalize(array + "[0].text")
            self.assertEqual(paths.locate(key)[2], "原文")
            validate_field_path(table, key)
        self.assertEqual(len(field_rules_metadata()["nested_keys"]), 16)

    def test_export_preserves_distinct_translations_through_reorder_and_invalidates_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw, orig, zh = root / "raw", root / "master/orig", root / "master/zh-Hans"
            raw.mkdir()
            a = {"messageDetailId": "a", "text": "同じ原文"}
            b = {"messageDetailId": "b", "text": "同じ原文"}
            def source(rows):
                (raw / "Message.json").write_text(
                    json.dumps([{"id": "parent", "details": rows}], ensure_ascii=False))
            source([a, b])
            export(raw, orig, zh)
            keys = [FieldPaths("Message", {"details": [a, b]}).canonicalize(
                f"details[{index}].text") for index in (0, 1)]
            (zh / "Message.json").write_text(json.dumps({
                "parent": {keys[0]: "甲译文", keys[1]: "乙译文"}}, ensure_ascii=False))
            source([b, {"messageDetailId": "new", "text": "新しい文章"}, a])
            export(raw, orig, zh)
            self.assertEqual(read_table(zh / "Message.json")["parent"],
                             {keys[0]: "甲译文", keys[1]: "乙译文"})
            source([dict(b, text="変更された文章")])
            export(raw, orig, zh)
            self.assertEqual(read_table(zh / "Message.json"), {})
            self.assertEqual(len(list(iter_fields(orig, zh))), 1)
            self.assertEqual(json.loads((orig.parent / "field-keys.json").read_text()),
                             field_rules_metadata())

    def test_export_and_reader_refuse_unmigrated_nested_fields_without_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw, orig, zh = root / "raw", root / "master/orig", root / "master/zh-Hans"
            for folder in (raw, orig, zh):
                folder.mkdir(parents=True)
            (raw / "Message.json").write_text(json.dumps([{
                "id": "parent", "details": [{"messageDetailId": "a", "text": "原文"}]}]))
            for folder, text in ((orig, "原文"), (zh, "译文")):
                (folder / "Message.json").write_text(
                    json.dumps({"parent": {"details[0].text": text}}))
            before = [(folder / "Message.json").read_bytes() for folder in (orig, zh)]
            with self.assertRaisesRegex(IdentityError, "migrate field keys"):
                export(raw, orig, zh)
            with self.assertRaisesRegex(IdentityError, "migrate field keys"):
                list(iter_fields(orig, zh))
            self.assertEqual(before, [(folder / "Message.json").read_bytes() for folder in (orig, zh)])

    def test_protobuf_and_json_resolve_same_identity_and_patch_the_correct_message(self):
        try:
            from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
        except ImportError:
            self.skipTest("protobuf required")
        file = descriptor_pb2.FileDescriptorProto(name="nested_test.proto", syntax="proto3")
        detail = file.message_type.add(name="Detail")
        for number, name in enumerate(("messageDetailId", "text"), 1):
            detail.field.add(name=name, number=number, type=9)
        record = file.message_type.add(name="Message")
        record.field.add(name="id", number=1, type=9)
        record.field.add(name="details", number=2, type=11, label=3, type_name=".Detail")
        pool = descriptor_pool.DescriptorPool()
        pool.Add(file)
        cls = message_factory.GetMessageClass(pool.FindMessageTypeByName("Message"))
        message = cls(id="parent")
        for identity in ("b", "a"):
            message.details.add(messageDetailId=identity, text="同じ原文")
        from master_blob_patches import patch_blob
        key = FieldPaths("Message", {"details": [
            {"messageDetailId": "a", "text": "同じ原文"}]}).canonicalize("details[0].text")
        result = cls.FromString(patch_blob(message.SerializeToString(), cls, "parent",
                                          {key: ("同じ原文", "甲译文")}, "Message"))
        self.assertEqual([item.text for item in result.details], ["同じ原文", "甲译文"])
        with self.assertRaisesRegex(ValueError, "source changed"):
            patch_blob(message.SerializeToString(), cls, "parent",
                       {key: ("別の原文", "甲译文")}, "Message")


if __name__ == "__main__":
    unittest.main()
