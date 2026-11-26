"""Execute release workflow scripts with synthetic bytes, never target firmware."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_release import ROOT, synthetic_image


def steps():
    text = (ROOT / '.github/workflows/release.yml').read_text()
    return {m[0]: m[1] for m in re.findall(
        r'^      - name: ([^\n]+)\n(.*?)(?=^      - |^  [a-z].*:\n|\Z)', text, re.M | re.S)}


def script(step):
    body = step.split('        run: |\n', 1)[1]
    return '\n'.join(line[10:] if line.startswith('          ') else line
                     for line in body.splitlines()) + '\n'


class ArtifactPrivacyTests(unittest.TestCase):
    def run_step(self, name, cwd, **env):
        return subprocess.run(['bash', '-euo', 'pipefail', '-c', script(steps()[name])],
                              cwd=cwd, env={**os.environ, **env}, capture_output=True)

    def test_public_stage_contains_only_validated_bin_and_report(self):
        with tempfile.TemporaryDirectory(prefix='synthetic-public-stage-') as tmp:
            cwd = Path(tmp)
            (cwd / 'scripts').symlink_to(ROOT / 'scripts', target_is_directory=True)
            build = cwd / '.pio/build/sticky'
            build.mkdir(parents=True)
            image = build / 'crosscub-sticky-v1.6.5.4.bin'
            image.write_bytes(synthetic_image())
            (build / 'firmware.elf').write_bytes(b'PRIVATE_ELF_CANARY')
            (cwd / 'release-build.log').write_bytes(b'PRIVATE_LOG_CANARY')
            result = self.run_step('Validate exact OTA image before artifact upload', cwd,
                                   RELEASE_VERSION='1.6.5.4')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual({p.name for p in (cwd / 'release-qualified').iterdir()},
                             {image.name, 'ota-validation.json'})
            image.write_bytes(b'SYNTHETIC INVALID IMAGE - NOT FIRMWARE')
            result = self.run_step('Validate exact OTA image before artifact upload', cwd,
                                   RELEASE_VERSION='1.6.5.4')
            self.assertNotEqual(result.returncode, 0)

    def test_readable_diagnostics_preserve_producer_exit_without_recipient(self):
        workflow = steps()
        text = (ROOT / '.github/workflows/release.yml').read_text()
        self.assertNotIn('RECIPIENT_CERT', text)
        self.assertNotIn('seal_ci_artifacts.py', text)
        self.assertNotIn('release-private', text)
        public = workflow['Upload qualified firmware artifact']
        self.assertNotIn('always()', public)
        paths = re.search(r'          path: \|\n((?:            .+\n)+)', public)[1]
        self.assertEqual([line.strip() for line in paths.splitlines()], [
            'release-qualified/crosscub-sticky-v${{ needs.prepare-release.outputs.release_version }}.bin',
            'release-qualified/ota-validation.json'])
        self.assertEqual(text.count('uses: actions/upload-artifact@'), 1)
        with tempfile.TemporaryDirectory(prefix='synthetic-readable-release-') as tmp:
            cwd = Path(tmp)
            tools = cwd / 'tools'
            tools.mkdir()
            pio = tools / 'pio'
            pio.write_text('#!/bin/bash\necho COMPILER_STDOUT_CANARY\necho COMPILER_STDERR_CANARY >&2\nexit "$SYNTHETIC_EXIT"\n')
            pio.chmod(0o700)
            for code in (0, 23):
                result = self.run_step('Build Crosscub Sticky', cwd,
                    PATH=str(tools) + os.pathsep + os.environ['PATH'], SYNTHETIC_EXIT=str(code))
                self.assertEqual(result.returncode, code, result.stderr)
                for canary in (b'COMPILER_STDOUT_CANARY', b'COMPILER_STDERR_CANARY'):
                    self.assertIn(canary, result.stdout + result.stderr)
                    self.assertIn(canary, (cwd / 'release-build.log').read_bytes())


if __name__ == '__main__':
    unittest.main()
