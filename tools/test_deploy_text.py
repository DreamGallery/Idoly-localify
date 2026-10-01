"""Device deployment compares exact hashes before changing the test emulator."""

import unittest
import subprocess
import shlex
from types import SimpleNamespace
from unittest.mock import patch

import deploy_text
from deploy_text import assert_expected_avd, parse_hashes, plan, sync


class DeployTextTests(unittest.TestCase):
    def test_font_payload_mismatch_stops_apply_before_any_text_writes(self):
        with patch('sys.argv', ['deploy_text.py', 'apply', '--serial', 'emulator-5560',
                                '--expected-avd', 'Idoly_Test']), \
             patch.object(deploy_text, 'assert_expected_avd'), \
             patch.object(deploy_text, 'Pipeline'), \
             patch.object(deploy_text, 'assert_compatible',
                          side_effect=ValueError('incompatible font payload')), \
             patch.object(deploy_text, 'sync') as sync_mock, \
             patch.object(deploy_text, 'device_hashes') as hashes:
            self.assertEqual(deploy_text.main(), 1)
            sync_mock.assert_not_called()
            hashes.assert_not_called()

    def test_adb_failure_bounds_partial_stdout_and_keeps_exit_status(self):
        partial = ("a" * 64 + "  /sdcard/resource/adv_one.txt\n") * 1710 + "HALF"
        result = subprocess.CompletedProcess([], 255, stdout=partial, stderr="")
        with patch.object(deploy_text.subprocess, "run", return_value=result):
            with self.assertRaises(RuntimeError) as raised:
                deploy_text.adb("adb", "emulator-5560", "shell", "scan")
        self.assertIn("failed (255)", str(raised.exception))
        self.assertIn("truncated", str(raised.exception))
        self.assertTrue(str(raised.exception).endswith("HALF"))
        self.assertLess(len(str(raised.exception)), deploy_text.ERROR_TAIL_LIMIT + 100)

    def test_resource_scan_batches_and_rechecks_complete_directory(self):
        paths = [f"{deploy_text.REMOTE_RESOURCE}/adv_{i:04}.txt" for i in range(130)]
        listing = "\0".join(paths + [deploy_text.LIST_END, ""])
        batches = []
        listings = []

        def fake_adb(_adb_bin, _serial, *args):
            command = args[1]
            if "-print0" in command:
                listings.append(command)
                return listing
            batch = shlex.split(command)[1:]
            batches.append(batch)
            return "".join(f"{'a' * 64}  {path}\n" for path in batch)

        with patch.object(deploy_text, "adb", fake_adb):
            actual = deploy_text.directory_hashes("adb", "serial", deploy_text.REMOTE_RESOURCE, True)
        self.assertEqual(len(actual), 130)
        self.assertEqual([len(batch) for batch in batches], [64, 64, 2])
        self.assertEqual(len(listings), 2)
        self.assertEqual([path for batch in batches for path in batch], paths)

    def test_incomplete_listing_cannot_be_treated_as_missing_resources(self):
        for output in ("", deploy_text.REMOTE_RESOURCE + "/adv_one.txt\0", "partial"):
            with self.subTest(output=output), patch.object(deploy_text, "adb", return_value=output):
                with self.assertRaisesRegex(ValueError, "Incomplete device directory"):
                    deploy_text.directory_hashes("adb", "serial", deploy_text.REMOTE_RESOURCE, True)

    def test_hash_batch_rejects_partial_missing_duplicate_and_wrong_path_output(self):
        paths = [deploy_text.REMOTE_RESOURCE + "/adv_one.txt",
                 deploy_text.REMOTE_RESOURCE + "/adv_two.txt"]
        listing = "\0".join(paths + [deploy_text.LIST_END, ""])
        first = f"{'a' * 64}  {paths[0]}\n"
        for output in ("", first, first + "abcdef", first + first,
                       first + f"{'b' * 64}  /other/adv_two.txt\n"):
            with self.subTest(output=output), patch.object(deploy_text, "adb", side_effect=[listing, output]):
                with self.assertRaisesRegex(ValueError, "hash batch"):
                    deploy_text.directory_hashes("adb", "serial", deploy_text.REMOTE_RESOURCE, True)

    def test_adb_failure_mid_scan_propagates_without_partial_result(self):
        path = deploy_text.REMOTE_RESOURCE + "/adv_one.txt"
        listing = path + "\0" + deploy_text.LIST_END + "\0"
        with patch.object(deploy_text, "adb", side_effect=[listing, RuntimeError("adb failed (255)")]):
            with self.assertRaisesRegex(RuntimeError, "255"):
                deploy_text.directory_hashes("adb", "serial", deploy_text.REMOTE_RESOURCE, True)

    def test_directory_change_during_scan_is_rejected(self):
        path = deploy_text.REMOTE_RESOURCE + "/adv_one.txt"
        listing = path + "\0" + deploy_text.LIST_END + "\0"
        with patch.object(deploy_text, "adb", side_effect=[listing,
                f"{'a' * 64}  {path}\n", deploy_text.LIST_END + "\0"]):
            with self.assertRaisesRegex(ValueError, "directory changed"):
                deploy_text.directory_hashes("adb", "serial", deploy_text.REMOTE_RESOURCE, True)

    def test_invalid_or_duplicate_directory_paths_are_rejected(self):
        path = deploy_text.REMOTE_RESOURCE + "/adv_one.txt"
        for paths in ([path, path], ["/other/adv_one.txt"],
                      [deploy_text.REMOTE_RESOURCE + "/adv_bad name.txt"]):
            output = "\0".join(paths + [deploy_text.LIST_END, ""])
            with self.subTest(paths=paths), patch.object(deploy_text, "adb", return_value=output):
                with self.assertRaises(ValueError):
                    deploy_text.directory_hashes("adb", "serial", deploy_text.REMOTE_RESOURCE, True)

    def test_hash_scan_failure_stops_apply_before_sync(self):
        pipeline = SimpleNamespace(dictionary="dict.json", web_notice_dictionary="web-notice.json",
                                   run=lambda **kwargs: None)
        with patch('sys.argv', ['deploy_text.py', 'apply', '--serial', 'emulator-5560',
                                '--expected-avd', 'Idoly_Test']), \
             patch.object(deploy_text, 'assert_expected_avd'), \
             patch.object(deploy_text, 'Pipeline', return_value=pipeline), \
             patch.object(deploy_text, 'assert_compatible'), \
             patch.object(deploy_text, 'expected_hashes', return_value=('a' * 64, {})), \
             patch.object(deploy_text, 'device_hashes', side_effect=RuntimeError('adb failed (255)')), \
             patch.object(deploy_text, 'sync') as sync_mock:
            self.assertEqual(deploy_text.main(), 1)
            sync_mock.assert_not_called()

    def test_complete_empty_listing_is_allowed(self):
        with patch.object(deploy_text, "adb", return_value=deploy_text.LIST_END + "\0"):
            self.assertEqual(deploy_text.directory_hashes("adb", "serial", deploy_text.REMOTE_RESOURCE, True), {})

    def test_apply_requires_the_named_test_avd_before_writing(self):
        with patch.object(deploy_text, "adb", return_value="Idoly_Test\r\nOK\r\n") as mocked:
            assert_expected_avd("adb", "emulator-5560", "Idoly_Test")
            mocked.assert_called_once_with("adb", "emulator-5560", "emu", "avd", "name")
            with self.assertRaisesRegex(ValueError, "deployment refused"):
                assert_expected_avd("adb", "emulator-5560", "Pixel_9_Pro_XL")
        with patch.object(deploy_text, "adb", return_value="unexpected response"):
            with self.assertRaisesRegex(ValueError, "Cannot identify AVD"):
                assert_expected_avd("adb", "emulator-5560", "Idoly_Test")

    def test_hash_parser_uses_filename_and_rejects_duplicates(self):
        first = "a" * 64
        second = "b" * 64
        lines = (f"{first}  /sdcard/resource/adv_one.txt\n"
                 f"{second}  /sdcard/resource/adv_two.txt\n")
        self.assertEqual(parse_hashes(lines),
                         {"adv_one.txt": first, "adv_two.txt": second})
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            parse_hashes(lines + f"{first}  /other/adv_one.txt\n")
        with self.assertRaisesRegex(ValueError, "Unexpected"):
            parse_hashes("garbled hash output")

    def test_plan_selects_only_mismatches_and_reports_device_only_files(self):
        expected = {"adv_one.txt": "a" * 64, "adv_two.txt": "b" * 64}
        remote = {"adv_one.txt": "a" * 64, "adv_two.txt": "c" * 64,
                  "adv_old.txt": "d" * 64}
        result = plan("e" * 64, expected, "e" * 64, remote)
        self.assertFalse(result["dictionary_changed"])
        self.assertEqual(result["resources_changed"], ["adv_two.txt"])
        self.assertEqual(result["device_only"], ["adv_old.txt"])

    def test_apply_archives_device_only_resource_without_deleting_it(self):
        calls = []

        def fake_adb(_adb_bin, _serial, *args):
            calls.append(args)
            if len(args) == 2 and "-print0" in args[1]:
                return deploy_text.LIST_END + "\0"
            return ""

        differences = {"dictionary_changed": False,
                       "resources_changed": [], "device_only": ["adv_old.txt"]}
        with patch.object(deploy_text, "adb", fake_adb):
            archive = sync("adb", "emulator-5560", SimpleNamespace(), differences,
                           "a" * 64, {})
        self.assertIn("/archive/resource/", archive)
        self.assertEqual(sum(1 for args in calls if args[0] == "shell" and
                             len(args) == 2 and args[1].startswith("mv ")), 1)
        self.assertIn("adv_old.txt", " ".join(map(str, calls)))
        self.assertTrue(all(".text-stage-" in " ".join(args)
                            for args in calls if args[:2] == ("shell", "rm")))

    def test_apply_rejects_unexpected_device_filename_before_mutation(self):
        differences = {"dictionary_changed": False,
                       "resources_changed": [], "device_only": ["adv_bad name.txt"]}
        with patch.object(deploy_text, "adb") as mocked:
            with self.assertRaisesRegex(ValueError, "unexpected filename"):
                sync("adb", "emulator-5560", SimpleNamespace(), differences,
                     "a" * 64, {})
        mocked.assert_not_called()


if __name__ == "__main__":
    unittest.main()
