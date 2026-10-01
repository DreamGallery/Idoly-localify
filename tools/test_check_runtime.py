"""A matching file is insufficient: the current process must have loaded it."""

import unittest

from check_runtime import inspect_log, process_start_bounds


class CheckRuntimeTests(unittest.TestCase):
    def test_process_start_from_proc_stat_with_parenthesized_name(self):
        fields = ["S"] + ["0"] * 19
        fields[19] = "12000"
        lower, upper = process_start_bounds(
            "7 (game name) " + " ".join(fields), "200.00 100.00", "100", "1000", "1001")
        self.assertEqual((lower, upper), (920, 921))

    def test_only_current_log_with_hook_and_matches_passes(self):
        log = (" 900.000 7 7 I IdolyLocalify: Loaded 1 MasterDB Blob patches; "
               "suppressed 0 global Text sources\n"
               " 921.000 7 7 I IdolyLocalify: Loaded 3 MasterDB Blob patches; "
               "suppressed 1 global Text sources\n"
               " 922.000 7 8 I IdolyLocalify: Qua.Master.SQLite.ResultSet.GetBlob hook installed\n"
               " 925.000 7 8 I IdolyLocalify: Replaced MasterDB Blob; matches=4\n")
        result = inspect_log(log, 920, 3, 1, require_matches=True)
        self.assertEqual(result["matches_logged_at_least"], 4)
        with self.assertRaisesRegex(ValueError, "loaded"):
            inspect_log(log, 920, 2, 1, require_matches=True)
        with self.assertRaisesRegex(ValueError, "replacement"):
            inspect_log(log.replace("Replaced MasterDB Blob", "Skipped MasterDB Blob"),
                        920, 3, 1, require_matches=True)


if __name__ == "__main__":
    unittest.main()
