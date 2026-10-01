"""MasterDB Blob deployment must verify bytes and the selected emulator."""

import unittest
from pathlib import Path
from unittest.mock import patch

import deploy_master_blobs as module


class DeployMasterBlobsTests(unittest.TestCase):
    def test_wrong_avd_cannot_write(self):
        with patch.object(module, "assert_expected_avd", side_effect=ValueError("wrong")):
            with patch.object(module, "adb") as adb:
                with self.assertRaisesRegex(ValueError, "wrong"):
                    module.deploy("adb", "emulator-5560", "Idoly_Test",
                                  Path("patch.bin"), "a" * 64, lambda: None)
                adb.assert_not_called()

    def test_matching_device_skips_write(self):
        with patch.object(module, "assert_expected_avd"):
            with patch.object(module, "device_hash", return_value="a" * 64):
                with patch.object(module, "adb") as adb:
                    self.assertFalse(module.deploy("adb", "emulator-5560",
                                                   "Idoly_Test", Path("patch.bin"),
                                                   "a" * 64, lambda: None))
                    adb.assert_not_called()

    def test_bad_staged_hash_keeps_previous_file(self):
        calls = []

        def fake_adb(_bin, _serial, *args):
            calls.append(args)
            return "b" * 64 + "  /sdcard/.master-blobs-test.bin\n" if args[:2] == (
                "shell", "sha256sum") else ""

        with patch.object(module, "assert_expected_avd"):
            with patch.object(module, "device_hash", return_value=None):
                with patch.object(module, "adb", fake_adb):
                    with self.assertRaisesRegex(ValueError, "Staged"):
                        module.deploy("adb", "emulator-5560", "Idoly_Test",
                                      Path("patch.bin"), "a" * 64, lambda: None)
        self.assertFalse(any(args[:2] == ("shell", "mv") for args in calls))
        self.assertTrue(any(args[:2] == ("shell", "rm") for args in calls))

    def test_changed_local_input_cannot_replace_staged_file(self):
        calls = []

        def fake_adb(_bin, _serial, *args):
            calls.append(args)
            if args[:2] == ("shell", "sha256sum"):
                return "a" * 64 + "  " + args[2] + "\n"
            return ""

        with patch.object(module, "assert_expected_avd"):
            with patch.object(module, "device_hash", return_value=None):
                with patch.object(module, "adb", fake_adb):
                    with self.assertRaisesRegex(ValueError, "source changed"):
                        module.deploy("adb", "emulator-5560", "Idoly_Test",
                                      Path("patch.bin"), "a" * 64,
                                      lambda: (_ for _ in ()).throw(
                                          ValueError("source changed")))
        self.assertFalse(any(args[:2] == ("shell", "mv") for args in calls))
        self.assertTrue(any(args[:2] == ("shell", "rm") for args in calls))


if __name__ == "__main__":
    unittest.main()
