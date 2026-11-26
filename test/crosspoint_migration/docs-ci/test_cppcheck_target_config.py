"""Target configuration is taken from compiler include search order, never -D guesses."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('cppcheck_capture_config', ROOT/'scripts/ci_cppcheck_capture.py')
assert spec is not None and spec.loader is not None
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class TargetConfigTests(unittest.TestCase):
    def test_compiler_selected_header_is_forced_without_other_package_includes(self):
        with tempfile.TemporaryDirectory() as tmp:
            first, second = Path(tmp)/'first', Path(tmp)/'second'
            first.mkdir()
            second.mkdir()
            for directory in (first, second):
                (directory/'sdkconfig.h').write_text('#define CONFIG_IDF_TARGET_ESP32S3 1\n')
            command = ['cppcheck', '--enable=all', 'source.cpp']
            actual = helper.with_target_config(command, [str(first), str(second)])
            self.assertEqual(actual, ['cppcheck', '--include='+str(first/'sdkconfig.h'), '--enable=all', 'source.cpp'])
            self.assertEqual(command, ['cppcheck', '--enable=all', 'source.cpp'])

    def test_missing_compiler_config_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(RuntimeError, 'sdkconfig.h'):
                helper.with_target_config(['cppcheck', 'source.cpp'], [tmp])


if __name__ == '__main__':
    unittest.main()
