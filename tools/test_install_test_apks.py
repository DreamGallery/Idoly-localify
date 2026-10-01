"""Installation must stop before ADB writes when the selected AVD differs."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import install_test_apks


class InstallTestApksTests(unittest.TestCase):
    def test_wrong_avd_never_reaches_install_multiple(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory) / "base.apk"
            split = Path(directory) / "split.apk"
            base.write_bytes(b"base")
            split.write_bytes(b"split")
            with patch.object(install_test_apks, "sdk_tool", return_value="tool"), \
                 patch.object(install_test_apks, "preflight", return_value="6.0.2"), \
                 patch.object(install_test_apks, "assert_expected_avd",
                              side_effect=ValueError("deployment refused")), \
                 patch.object(install_test_apks.subprocess, "run") as run:
                with self.assertRaisesRegex(ValueError, "deployment refused"):
                    install_test_apks.install(base, split, "emulator-5554", "Idoly_Test",
                                              "adb", Path(directory))
                run.assert_not_called()

    def test_check_only_validates_without_install(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory) / "base.apk"
            split = Path(directory) / "split.apk"
            base.write_bytes(b"base")
            split.write_bytes(b"split")
            with patch.object(install_test_apks, "sdk_tool", return_value="tool"), \
                 patch.object(install_test_apks, "preflight", return_value="6.0.2"), \
                 patch.object(install_test_apks, "assert_expected_avd") as guard, \
                 patch.object(install_test_apks.subprocess, "run") as run:
                result = install_test_apks.install(base, split, "emulator-5560",
                                                   "Idoly_Test", "adb", Path(directory),
                                                   check_only=True)
                self.assertEqual(result, "6.0.2")
                guard.assert_called_once_with("adb", "emulator-5560", "Idoly_Test")
                run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
