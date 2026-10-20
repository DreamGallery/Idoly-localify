import json
import unittest

from master_identity import (
    BUSINESS_KEY_RULES, IdentityError, index_records, record_key,
    rules_metadata, validate_keyed_table,
)


class MasterIdentityTests(unittest.TestCase):
    def test_text_and_order_do_not_define_identity(self):
        a = {"homeActionId": "a", "text": "same"}
        b = {"homeActionId": "b", "text": "same"}
        original = index_records("HomeAction", [a, b])
        self.assertEqual(original, index_records("HomeAction", [b, a]))
        self.assertNotEqual(record_key("HomeAction", a), record_key("HomeAction", b))
        self.assertIn(record_key("HomeAction", b), index_records("HomeAction", [b]))
        c = {"homeActionId": "c", "text": "new"}
        self.assertEqual(original[record_key("HomeAction", a)],
                         index_records("HomeAction", [c, b, a])[record_key("HomeAction", a)])

    def test_duplicates_rejected(self):
        with self.assertRaisesRegex(IdentityError, "duplicate"):
            index_records("Wording", [{"key": "a", "word": "one"},
                                      {"key": "a", "word": "two"}])

    def test_integer_zero_and_decimal_strings(self):
        row = {"cardId": "001", "evolutionLevel": 0, "number": 1}
        equivalent = {"cardId": "001", "evolutionLevel": "0", "number": "01"}
        self.assertEqual(record_key("CardEvolutionMessage", row),
                         record_key("CardEvolutionMessage", equivalent))
        self.assertNotEqual(record_key("CardEvolutionMessage", row),
                            record_key("CardEvolutionMessage", dict(row, cardId="1")))
        self.assertTrue(record_key("StaffTraining", {"parameterType": 0}))
        for value in [None, True, 1.0, "1.0", " 1", {}, [], 2**31]:
            with self.subTest(value=value), self.assertRaises(IdentityError):
                record_key("StaffTraining", {"parameterType": value})
        with self.assertRaisesRegex(IdentityError, "missing"):
            record_key("CardEvolutionMessage", {"cardId": "001", "number": 1})

    def test_strings_and_separator_are_preserved(self):
        a = {"characterId": "a|b", "patternId": "c"}
        b = {"characterId": "a", "patternId": "b|c"}
        self.assertNotEqual(record_key("HomeTalkCallPattern", a),
                            record_key("HomeTalkCallPattern", b))
        value = ' 001|["日本語"] '
        self.assertEqual(json.loads(record_key("Wording", {"key": value})[4:]),
                         [["key", value]])
        for value in [1, None, False, ""]:
            with self.subTest(value=value), self.assertRaises(IdentityError):
                record_key("Wording", {"key": value})

    def test_standard_id_and_no_fallback(self):
        self.assertEqual(record_key("Card", {"id": 0}), "0")
        self.assertEqual(record_key("Card", {"id": "001"}), "001")
        for row in [{}, {"id": None}, {"id": True}, {"id": []}, {"id": {}},
                    {"id": ""}, {"id": 1.5}]:
            with self.subTest(row=row), self.assertRaises(IdentityError):
                record_key("Card", row)

    def test_all_rules_and_metadata(self):
        self.assertEqual(len(BUSINESS_KEY_RULES), 15)
        for table, rule in BUSINESS_KEY_RULES.items():
            row = {field: "identity" if kind == "string" else 0
                   for field, kind in rule}
            validate_keyed_table(table, {record_key(table, row): {}})
            for field in row:
                incomplete = dict(row)
                del incomplete[field]
                with self.subTest(table=table, field=field), self.assertRaises(IdentityError):
                    record_key(table, incomplete)
        metadata = rules_metadata()
        json.dumps(metadata)
        metadata["business_keys"].clear()
        self.assertEqual(len(rules_metadata()["business_keys"]), 15)

    def test_legacy_and_malformed_keys_rejected(self):
        for key in ["0", "bk2:[]", "bk1:{}", "bk1:[]",
                    'bk1:[["key","a"],["key","a"]]', 'bk1:[["key", "a"]]']:
            with self.subTest(key=key), self.assertRaises(IdentityError):
                validate_keyed_table("Wording", {key: {}})
        validate_keyed_table("Card", {"0": {}})

    def test_protobuf_numeric_and_json_identity_agree(self):
        try:
            from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
        except ImportError:
            self.skipTest("protobuf is not installed")
        file = descriptor_pb2.FileDescriptorProto(name="identity_test.proto", syntax="proto3")
        message = file.message_type.add(name="CardEvolutionMessage")
        for number, name, kind in [(1, "cardId", 9), (2, "evolutionLevel", 5), (3, "number", 5)]:
            message.field.add(name=name, number=number, type=kind, label=1)
        pool = descriptor_pool.DescriptorPool()
        pool.Add(file)
        descriptor = pool.FindMessageTypeByName("CardEvolutionMessage")
        if hasattr(message_factory, "GetMessageClass"):
            cls = message_factory.GetMessageClass(descriptor)
        else:
            cls = message_factory.MessageFactory(pool).GetPrototype(descriptor)
        record = cls(cardId="001", number=1)
        self.assertEqual(record_key("CardEvolutionMessage", record),
                         record_key("CardEvolutionMessage",
                                    {"cardId": "001", "evolutionLevel": "0", "number": 1}))


if __name__ == "__main__":
    unittest.main()
