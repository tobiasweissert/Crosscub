"""Configuration-only probe isolation, genuine input and fail-closed contracts."""
import importlib.util
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('target_config', ROOT/'scripts/cppcheck_target_config.py')
assert spec is not None and spec.loader is not None
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class ProbeTests(unittest.TestCase):
    def fixture(self, tmp):
        root = Path(tmp)/'project'
        root.mkdir()
        (root/'.git').mkdir()
        (root/'.git/HEAD').write_text('unchanged Git identity')
        (root/'platformio.ini').write_text('[env:sticky]\ncustom_sdkconfig = CONFIG_TEST=y\n')
        (root/'sdkconfig.defaults').write_text('original defaults\n')
        (root/'scripts').mkdir()
        (root/'scripts/hook.py').write_text('# exact shipped hook\n')
        core = Path(tmp)/'core'
        (core/'platforms').mkdir(parents=True)
        (core/'platforms/source.py').write_text('original platform')
        for name in ('framework-arduinoespressif32', 'framework-arduinoespressif32-libs', 'framework-espidf', 'tool-cmake'):
            (core/'packages'/name).mkdir(parents=True)
            (core/'packages'/name/'original').write_text('original package')
        chip = core/'packages/framework-arduinoespressif32-libs/esp32s3'
        chip.mkdir()
        (chip/'sdkconfig.orig').write_text('authentic original template')
        (chip/'sdkconfig').write_text('cached configured template')
        return root, core

    def generate(self, root, core, mode='success'):
        expected = b'#define CONFIG_IDF_TARGET_ESP32S3 1\n#define CONFIG_TEST 1\n'

        def run(argv, **kwargs):
            source = kwargs['cwd']
            private = Path(kwargs['env']['PLATFORMIO_CORE_DIR'])
            self.assertEqual(argv[-5:], ['run', '-e', 'sticky', '-t', 'envdump'])
            self.assertEqual((source/'.git').read_text(), 'gitdir: ' + str(root/'.git') + '\n')
            self.assertEqual(kwargs['env']['GIT_OPTIONAL_LOCKS'], '0')
            self.assertEqual(kwargs['env']['PLATFORMIO_BUILD_DIR'], str(source/'.pio/build'))
            self.assertEqual(kwargs['env']['PLATFORMIO_PACKAGES_DIR'], str(private/'packages'))
            self.assertEqual((source/'platformio.ini').read_bytes(), (root/'platformio.ini').read_bytes())
            self.assertEqual((source/'scripts/hook.py').read_bytes(), (root/'scripts/hook.py').read_bytes())
            self.assertEqual((private/'packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig').read_text(), 'authentic original template')
            self.assertFalse((private/'packages/framework-espidf').is_symlink())
            self.assertTrue((private/'packages/tool-cmake').is_symlink())
            (source/'sdkconfig.defaults').write_text('probe generated marker')
            (private/'platforms/source.py').write_text('probe platform mutation')
            (private/'packages/framework-espidf/original').write_text('probe package mutation')
            if mode == 'nonzero':
                raise subprocess.CalledProcessError(1, argv)
            if mode == 'drift':
                (root/'platformio.ini').write_text('unexpected source drift')
            if mode != 'missing':
                header = source/'.pio/build/sticky/config/sdkconfig.h'
                header.parent.mkdir(parents=True)
                header.write_bytes(expected)
            return subprocess.CompletedProcess(argv, 0)

        with patch.object(helper, 'run_config', side_effect=run):
            result = helper.configured_header(root, core, 'sticky', root.parent/'configure.log')
        return result, expected

    def test_genuine_source_configuration_isolated_from_original_inputs_and_packages(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, core = self.fixture(tmp)
            (data, inputs), expected = self.generate(root, core)
            self.assertEqual(data, expected)
            self.assertIn('platformio.ini', inputs)
            self.assertEqual((root/'sdkconfig.defaults').read_text(), 'original defaults\n')
            self.assertEqual((core/'platforms/source.py').read_text(), 'original platform')
            self.assertEqual((core/'packages/framework-espidf/original').read_text(), 'original package')
            self.assertTrue((core/'packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig.orig').exists())
            self.assertEqual(list((root/'.pio').iterdir()), [])

    def test_missing_generated_header_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, core = self.fixture(tmp)
            with self.assertRaisesRegex(RuntimeError, 'did not generate'):
                self.generate(root, core, 'missing')

    def test_source_drift_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, core = self.fixture(tmp)
            with self.assertRaisesRegex(RuntimeError, 'input changed'):
                self.generate(root, core, 'drift')

    def test_failed_configuration_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, core = self.fixture(tmp)
            with self.assertRaises(subprocess.CalledProcessError):
                self.generate(root, core, 'nonzero')
            self.assertEqual(list((root/'.pio').iterdir()), [])

    def test_external_directory_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, core = self.fixture(tmp)
            (root/'external').symlink_to(core, target_is_directory=True)
            with self.assertRaisesRegex(RuntimeError, 'symlink escapes'):
                self.generate(root, core)

    def test_invalid_environment_is_rejected_before_subprocess(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, core = self.fixture(tmp)
            with self.assertRaisesRegex(RuntimeError, 'environment identity'):
                helper.configured_header(root, core, '../sticky', root.parent/'configure.log')

    def test_configure_timeout_and_normal_exit_terminate_descendants(self):
        for timeout in (False, True):
            with self.subTest(timeout=timeout), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                child = 'import pathlib,time; time.sleep(0.5); pathlib.Path("late-write").write_text("bad")'
                driver = ('import subprocess,sys,time; '
                          'subprocess.Popen([sys.executable,"-c",' + repr(child) + ']); '
                          + ('time.sleep(5)' if timeout else 'time.sleep(0.1)'))
                command = [sys.executable, '-c', driver]
                if timeout:
                    with self.assertRaises(subprocess.TimeoutExpired):
                        helper.run_config(command, cwd=root, env=os.environ.copy(), log=root/'log', timeout=0.2)
                else:
                    helper.run_config(command, cwd=root, env=os.environ.copy(), log=root/'log', timeout=2)
                time.sleep(0.6)
                self.assertFalse((root/'late-write').exists())

    def test_prepare_keeps_metadata_caches_one_real_generation_and_records_bounded_receipt(self):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as tmp, patch.object(sys, 'path', [str(ROOT/'scripts'), *sys.path]):
            root, core = self.fixture(tmp)
            out = Path(tmp)/'out'
            out.mkdir()
            tool = SimpleNamespace(cpp_defines=['ORIGINAL=1'], cpp_includes=['original/include'],
                                   envname='sticky', project_dir=root,
                                   config=SimpleNamespace(get=lambda *args: str(core)),
                                   cc_flags=['-std=gnu11'], cxx_flags=['-std=gnu++20'],
                                   toolchain_defines={'c': [], 'c++': []})
            # A large source tree must not overflow the existing receipt cap.
            inputs = {str(i): '0'*64 for i in range(10000)}
            with patch.object(helper, 'configured_header', return_value=(b'authentic generated bytes', inputs)) as generate:
                header = helper.prepare(tool, out, ['cppcheck', '--enable=all', 'source.c'])
                self.assertEqual(helper.prepare(tool, out, ['cppcheck', 'source.cpp']), header)
                self.assertEqual(generate.call_count, 1)
            import json
            receipt = json.loads((out/'cppcheck-target-config.json').read_text())
            self.assertEqual(receipt['input_file_count'], len(inputs))
            self.assertEqual(receipt['metadata'], {'defines': ['ORIGINAL=1'], 'includes': ['original/include']})
            self.assertEqual(tool.cpp_defines, ['ORIGINAL=1'])
            self.assertEqual(tool.cpp_includes, ['original/include'])
            self.assertEqual(header.read_bytes(), b'authentic generated bytes')

    def test_prepare_rejects_metadata_drift(self):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as tmp, patch.object(sys, 'path', [str(ROOT/'scripts'), *sys.path]):
            root, core = self.fixture(tmp)
            out = Path(tmp)/'out'
            out.mkdir()
            tool = SimpleNamespace(cpp_defines=['ORIGINAL=1'], cpp_includes=['original/include'],
                                   envname='sticky', project_dir=root,
                                   config=SimpleNamespace(get=lambda *args: str(core)))
            def drift(*args):
                tool.cpp_includes.append('unexpected/package')
                return b'header', {}
            with patch.object(helper, 'configured_header', side_effect=drift):
                with self.assertRaisesRegex(RuntimeError, 'metadata changed'):
                    helper.prepare(tool, out, ['cppcheck', 'source.c'])
            self.assertFalse((out/'cppcheck-sdkconfig.h').exists())


if __name__ == '__main__':
    unittest.main()
