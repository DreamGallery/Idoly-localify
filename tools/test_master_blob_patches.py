from contextlib import nullcontext
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
from threading import Thread
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, call, patch

import requests

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
                return nullcontext(SimpleNamespace(content=b"ciphered", raise_for_status=lambda: None))

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


class MasterPackDownloadTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.cache = Path(temporary.name)
        self.pack = SimpleNamespace(type="Music", downloadUrl="https://example.test/Music",
                                    fileSize=8, cryptoKey="fixture-key")
        sleeping = patch("master_blob_patches.time.sleep")
        self.sleep = sleeping.start()
        self.addCleanup(sleeping.stop)
        printing = patch("builtins.print")
        self.print = printing.start()
        self.addCleanup(printing.stop)

    def test_complete_download_recovers_from_broken_body_length_and_server_error(self):
        responses = [(200, 8, b"cut"), (200, 3, b"cut"), (503, 0, b""),
                     (200, 8, b"ciphered")]
        seen_files = []
        cache = self.cache

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                seen_files.append([p for p in cache.rglob("*") if p.is_file()])
                status, size, body = responses.pop(0)
                self.send_response(status)
                self.send_header("Content-Length", str(size))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(body)
                self.close_connection = True

            def log_message(self, *args):
                pass

        with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
            thread = Thread(target=server.serve_forever, daemon=True)
            thread.start()
            self.pack.downloadUrl = f"http://127.0.0.1:{server.server_port}/Music"
            try:
                with requests.Session() as client:
                    client.trust_env = False
                    output = encrypted_pack(self.pack, "v1", cache, client)
                    self.assertEqual(output.read_bytes(), b"ciphered")
                    self.assertEqual(encrypted_pack(self.pack, "v1", cache, client), output)
            finally:
                server.shutdown()
                thread.join()
        self.assertEqual(seen_files, [[], [], [], []])
        self.assertEqual(self.sleep.call_args_list, [call(1), call(2), call(4)])
        logs = [item.args[0] for item in self.print.call_args_list]
        for index, message in enumerate(logs, 1):
            self.assertIn("MasterDB: Music: download failed", message)
            self.assertIn(f"retry {index}/3", message)
            self.assertNotIn(self.pack.downloadUrl, message)
        self.assertEqual(len(logs), 3)

    def test_network_failures_stop_after_three_retries_without_writing_cache(self):
        for failure_type in (requests.exceptions.ConnectionError, requests.exceptions.ConnectTimeout,
                             requests.exceptions.ReadTimeout, requests.exceptions.ChunkedEncodingError):
            with self.subTest(failure=failure_type.__name__):
                self.sleep.reset_mock()
                client = Mock()
                client.get.side_effect = failure_type("download interrupted")
                with self.assertRaises(failure_type):
                    encrypted_pack(self.pack, "v1", self.cache, client)
                self.assertEqual(client.get.call_count, 4)
                self.assertEqual(self.sleep.call_args_list, [call(1), call(2), call(4)])
                self.assertFalse(any(p.is_file() for p in self.cache.rglob("*")))

    def test_incomplete_download_never_becomes_a_valid_cache_entry(self):
        client = Mock()
        client.get.return_value = nullcontext(SimpleNamespace(
            content=b"cut", raise_for_status=lambda: None))
        with self.assertRaisesRegex(ValueError, "download length differs"):
            encrypted_pack(self.pack, "v1", self.cache, client)
        self.assertEqual(client.get.call_count, 4)
        self.assertFalse(any(p.is_file() for p in self.cache.rglob("*")))

    def test_permanent_http_errors_fail_without_retry(self):
        for status in (400, 401, 403, 404):
            with self.subTest(status=status):
                client = Mock()
                client.get.side_effect = requests.exceptions.HTTPError(
                    response=SimpleNamespace(status_code=status))
                with self.assertRaises(requests.exceptions.HTTPError):
                    encrypted_pack(self.pack, "v1", self.cache, client)
                self.assertEqual(client.get.call_count, 1)
                self.sleep.assert_not_called()


if __name__ == "__main__":
    unittest.main()
