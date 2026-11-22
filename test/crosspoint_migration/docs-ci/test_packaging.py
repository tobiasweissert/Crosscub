"""Execute the workflow's actual provenance Python on SYNTHETIC NOT FIRMWARE fixtures.

Subprocess core/compiler version observations are doubled. No SDK, compiler, build,
network, GitHub or PlatformIO execution is performed by these packaging tests.
"""
from pathlib import Path
import ast
import hashlib
import importlib.util
import json
import os
import re
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]


def staging_python():
    text = (ROOT / '.github/workflows/ci.yml').read_text()
    text = text[text.index('      - name: Stage exact Sticky BIN ELF and provenance'):]
    match = re.search(r"          python3 - <<'PY'\n(.*?)\n          PY", text, re.S)
    if not match:
        raise AssertionError('workflow staging Python is absent')
    source = '\n'.join(line[10:] if line.startswith('          ') else line for line in match[1].splitlines())
    ast.parse(source)
    return compile(source, str(ROOT / '.github/workflows/ci.yml') + ':stage', 'exec')


class Packaging(unittest.TestCase):
    def run_stage(self, corrupt=False, environment='sticky'):
        spec = importlib.util.spec_from_file_location('synthetic_ota_fixture', ROOT / 'test/ota_release/test_release.py')
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(dir=os.environ.get('RUNNER_TEMP') or tempfile.gettempdir()) as tmp:
            root = Path(tmp)
            expected_temp = Path(os.environ.get('RUNNER_TEMP') or tempfile.gettempdir())
            self.assertEqual(root.parent.resolve(), expected_temp.resolve())
            (root / 'scripts').symlink_to(ROOT / 'scripts', target_is_directory=True)
            build = root / '.pio/build' / environment
            build.mkdir(parents=True)
            binary = module.synthetic_image(version='1.6.5.5')
            if corrupt:
                binary = binary[:-1]
            (build / 'firmware.bin').write_bytes(binary)
            elf = b'SYNTHETIC ELF PLACEHOLDER - NOT FIRMWARE'
            (build / 'firmware.elf').write_bytes(elf)
            (root / 'platformio.ini').write_text('[crosssmudge]\nversion=1.6.5.5\n[crossink]\nversion=1.6.5.5\n')
            out = root / 'sticky-evidence'
            out.mkdir()
            home = root / 'home'
            compiler = home / '.platformio/packages/toolchain-fixture/bin/fixture-g++'
            compiler.parent.mkdir(parents=True)
            compiler.write_text('SYNTHETIC COMPILER PLACEHOLDER')
            old = Path.cwd()
            try:
                os.chdir(root)
                with patch.dict(os.environ, {'PIO_ENV': environment}), patch('pathlib.Path.home', return_value=home), patch('subprocess.check_output', return_value='SYNTHETIC Core 6.1.19 / compiler fixture'):
                    if corrupt:
                        with self.assertRaises(SystemExit):
                            exec(staging_python(), {'__name__': '__workflow_staging_fixture__'})
                    else:
                        exec(staging_python(), {'__name__': '__workflow_staging_fixture__'})
                self.assertEqual((out / 'firmware.bin').read_bytes(), binary)
                self.assertEqual((out / 'crosscub-sticky-v1.6.5.5.bin').read_bytes(), binary)
                self.assertEqual((out / 'firmware.elf').read_bytes(), elf)
                report = json.loads((out / 'image-validation.json').read_text())
                self.assertEqual(report['valid'], not corrupt)
                self.assertFalse(report['publication_authorized'])
                hashes = json.loads((out / 'artifact-sha256.json').read_text())
                self.assertEqual(hashes['firmware.bin'], hashlib.sha256(binary).hexdigest())
                self.assertIn('toolchain-manifest.json', hashes)
                self.assertIn('merged-initializer-receipt.json', hashes)
                self.assertIn('merged-initializer.txt', hashes)
                provenance = json.loads((out / 'build-environment.json').read_text())
                self.assertEqual(provenance, {'environment': environment, 'hardware_board': 'Sticky', 'publication_authorized': False})
            finally:
                os.chdir(old)

    def test_exact_bin_elf_and_hashes(self):
        self.run_stage()

    def test_rejected_image_is_retained_with_failed_report(self):
        self.run_stage(corrupt=True)

    def test_runner_temp_without_host_tmpdir(self):
        with tempfile.TemporaryDirectory(dir=tempfile.gettempdir()) as runner_temp:
            with patch.dict(os.environ, {'RUNNER_TEMP': runner_temp}, clear=True):
                self.run_stage()

    def test_runner_temp_rejected_image_without_host_tmpdir(self):
        with tempfile.TemporaryDirectory(dir=tempfile.gettempdir()) as runner_temp:
            with patch.dict(os.environ, {'RUNNER_TEMP': runner_temp}, clear=True):
                self.run_stage(corrupt=True)


if __name__ == '__main__':
    unittest.main()
