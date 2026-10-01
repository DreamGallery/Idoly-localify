import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest

from fetch_master import fetch_packs
from master_json import export


def tag(version, *names):
    packs = [SimpleNamespace(type=name, fileName=f"{name}.db", fileSize=5,
                             cryptoKey="key", downloadUrl=f"https://example.test/{name}")
             for name in names]
    return SimpleNamespace(version=version, masterTagPacks=packs,
                           SerializeToString=lambda: version.encode())


class FetchMasterTests(unittest.TestCase):
    def test_version_change_redecodes_and_full_fetch_retires_removed_tables(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            calls = []
            (output / "A.json").write_text('[{"id":"1","text":"旧版日本語"}]',
                                           encoding="utf-8")

            def decode(pack, target, _modules, reuse):
                calls.append((pack.type, reuse))
                if not reuse:
                    (target / f"{pack.type}.json").write_text(json.dumps([
                        {"id": "1", "text": f"日本語 {pack.type}"}
                    ], ensure_ascii=False), encoding="utf-8")
                return pack.type, "cached" if reuse else 1

            self.assertEqual(fetch_packs(tag("v1", "A", "B"), output, None,
                                         workers=1, decoder=decode), (2, 0))
            self.assertIn(("A", False), calls)
            self.assertEqual(fetch_packs(tag("v1", "A", "B"), output, None,
                                         workers=1, decoder=decode), (2, 2))
            self.assertEqual(fetch_packs(tag("v2", "A"), output, None,
                                         workers=1, decoder=decode), (1, 0))
            self.assertEqual(calls[-1], ("A", False))
            self.assertFalse((output / "B.json").exists())
            self.assertEqual(len(list((output / ".retired").rglob("B.json"))), 1)
            state = json.loads((output / "master-fetch-state.json").read_text())
            self.assertEqual(state["complete_version"], "v2")

    def test_failed_fetch_blocks_export_until_full_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "raw"
            output.mkdir()

            def failed(pack, target, _modules, reuse):
                if pack.type == "B":
                    raise OSError("simulated download failure")
                (target / "A.json").write_text(json.dumps([
                    {"id": "1", "text": "日本語"}
                ], ensure_ascii=False), encoding="utf-8")
                return pack.type, 1

            with self.assertRaisesRegex(RuntimeError, "failed for 1 tables"):
                fetch_packs(tag("v1", "A", "B"), output, None, workers=1,
                            decoder=failed)
            self.assertTrue((output / ".master-fetch-in-progress.json").exists())
            with self.assertRaisesRegex(ValueError, "fetch is incomplete"):
                export(output, root / "orig", root / "zh")

            def retry(pack, target, _modules, reuse):
                if not reuse:
                    (target / f"{pack.type}.json").write_text(json.dumps([
                        {"id": "1", "text": "日本語"}
                    ], ensure_ascii=False), encoding="utf-8")
                return pack.type, "cached" if reuse else 1

            self.assertEqual(fetch_packs(tag("v1", "A", "B"), output, None,
                                         workers=1, decoder=retry), (2, 1))
            self.assertFalse((output / ".master-fetch-in-progress.json").exists())
            self.assertEqual(export(output, root / "orig", root / "zh",
                                    prune_missing=True), (2, 2))

    def test_partial_fetch_cannot_be_treated_as_complete_dump(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "raw"

            def decode(pack, target, _modules, reuse):
                (target / f"{pack.type}.json").write_text(json.dumps([
                    {"id": "1", "text": "日本語"}
                ], ensure_ascii=False), encoding="utf-8")
                return pack.type, 1

            fetch_packs(tag("v1", "A", "B"), output, None, workers=1,
                        selected={"A"}, decoder=decode)
            with self.assertRaisesRegex(ValueError, "full MasterDB fetch is required"):
                export(output, root / "orig", root / "zh", prune_missing=True)
            self.assertEqual(export(output, root / "orig", root / "zh"), (1, 1))
            (output / "A.json").write_text(json.dumps([
                {"id": "1", "text": "古い日本語"}
            ], ensure_ascii=False), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "differs from the verified"):
                export(output, root / "orig", root / "zh")


if __name__ == "__main__":
    unittest.main()
