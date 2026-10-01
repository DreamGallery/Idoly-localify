from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

@unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
class ShopExchangeDescriptionTests(unittest.TestCase):
    def test_scoped_reviewed_body_and_strict_dynamic_reset_date(self):
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'shop-description'
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(root/'tools/test_shop_exchange_description_matcher.cpp'),'-o',str(binary)],check=True,capture_output=True)
            subprocess.run([str(binary)],check=True,capture_output=True)

if __name__=='__main__':
    unittest.main()
