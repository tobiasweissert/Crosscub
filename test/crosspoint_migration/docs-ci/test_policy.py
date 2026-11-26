"""Portable source-policy checks; not actionlint or a firmware build."""
from pathlib import Path
import re
import subprocess
import shutil
import os
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[3]


class Policy(unittest.TestCase):
    def test_actual_web_scripts(self):
        self.assertIsNotNone(shutil.which('node'), 'Node is required for the actual web-script contract probe')
        subprocess.run(['node', str(Path(__file__).with_name('web_contract.js'))], cwd=ROOT, check=True, timeout=20)

    def test_standard_build_validates_locally_without_public_debug_uploads(self):
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertIn('workflow_dispatch:', text)
        self.assertIn('pioarduino==6.1.19', text)
        self.assertIn('634a3f8c067a3714a6a7c622015138af096ab5d2', text)
        self.assertIn('55.03.311', text)
        self.assertIn('firmware.elf', text)
        self.assertIn('image-validation.json', text)
        self.assertIn('toolchain-manifest.json', text)
        self.assertLess(text.index('name: Stage exact Sticky BIN ELF and provenance'),
                        text.index('name: Check portable docs and workflow policy'))
        self.assertNotIn('github.event.repository.private', text)
        self.assertNotIn('RECIPIENT_CERT', text)
        self.assertNotIn('seal_ci_artifacts.py', text)
        self.assertNotIn('uses: actions/upload-artifact@', text)
        self.assertIn("if: always() && steps.firmware.outcome != 'skipped'", text)
        self.assertIn('test "$startup_capture_status" -eq 0', text)
        self.assertNotIn('contents: write', text)
        self.assertNotIn('gh release', text)
        self.assertNotIn('secrets.RELEASE_PAT', text)

    def test_firmware_stats_do_not_publish_log_lines(self):
        workflow = (ROOT / '.github/workflows/ci.yml').read_text()
        body = re.search(r'(?m)^      - name: Extract firmware stats\n        run: \|\n((?:          .*\n|\n)+)', workflow)
        if body is None:
            self.fail('missing firmware stats step body')
        with tempfile.TemporaryDirectory() as tmp:
            summary = Path(tmp) / 'summary'
            (Path(tmp) / 'pio-sticky.log').write_text(
                'RAM:   [==        ] 20.0% (used 20 bytes from 100 bytes)\n'
                'synthetic diagnostic Flash: not a size record\n'
                'Flash: [===       ] 30.0% (used 30 bytes from 100 bytes) synthetic trailer\n')
            result = subprocess.run(['bash', '-ec', textwrap.dedent(body[1])], cwd=tmp,
                                    env={'PATH': os.defpath, 'PIO_ENV': 'sticky', 'GITHUB_STEP_SUMMARY': str(summary)},
                                    capture_output=True, timeout=5, check=True)
            self.assertEqual((result.stdout, result.stderr), (b'', b''))
            self.assertEqual(summary.read_bytes(), b'Firmware size diagnostics remain private.\n')

    def test_sticky_build_is_verbose_without_weakening_cppcheck(self):
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        build = text.split('      - name: Build ${{ matrix.environment }} firmware', 1)[1]
        build = build.split('      - name: Stage exact Sticky BIN ELF and provenance', 1)[0]
        commands = re.findall(r'(?m)^          pio run .+$', build)
        self.assertEqual(commands, ['          pio run --verbose -j1 -e "$PIO_ENV" 2>&1 | tee -a "pio-$PIO_ENV.log"'])
        analyzer = text.split('      - name: Run cppcheck', 1)[1].split('  build:', 1)[0]
        self.assertIn('python3 scripts/ci_cppcheck_capture.py check --verbose --fail-on-defect low --fail-on-defect medium --fail-on-defect high 2>&1 | tee cppcheck-evidence/check.log', analyzer)
        self.assertNotIn('continue-on-error', analyzer)
        self.assertIn('set -euo pipefail', analyzer)
        self.assertNotIn('path: cppcheck-evidence', text)

    def test_analyzer_readable_streams_and_exact_failure_exit(self):
        workflow = (ROOT / '.github/workflows/ci.yml').read_text()
        analyzer = workflow.split('      - name: Run cppcheck\n', 1)[1].split('  build:', 1)[0]
        body = textwrap.dedent(analyzer.split('        run: |\n', 1)[1].split('      - name:', 1)[0])
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            tool = root / 'python3'
            tool.write_text('#!/bin/sh\necho ANALYZER_STDOUT\necho ANALYZER_STDERR >&2\nexit "$SYNTHETIC_EXIT"\n')
            tool.chmod(0o700)
            for code in (0, 29):
                result = subprocess.run(['bash', '-c', body], cwd=root, capture_output=True,
                    env={**os.environ, 'PATH': str(root) + os.pathsep + os.environ['PATH'],
                         'GITHUB_WORKSPACE': str(root), 'SYNTHETIC_EXIT': str(code)})
                self.assertEqual(result.returncode, code, result.stderr)
                for canary in (b'ANALYZER_STDOUT', b'ANALYZER_STDERR'):
                    self.assertIn(canary, result.stdout + result.stderr)
                    self.assertIn(canary, (root / 'cppcheck-evidence/check.log').read_bytes())

    def test_release_is_owner_sticky_and_guarded(self):
        text = (ROOT / '.github/workflows/release.yml').read_text()
        self.assertNotRegex(text, r'(?m)^[<=>]{7}')
        self.assertIn("github.repository == 'tobiasweissert/Crosscub'", text)
        self.assertIn('scripts/prepare_ota_release.py --ci-check', text)
        self.assertIn('scripts/validate_ota_release.py', text)
        self.assertIn('default: false', text)
        self.assertNotIn('device: x3-x4', text)
        self.assertNotIn('git push', text)
        self.assertNotIn('--clobber', text)

    def test_pkg_list_matches_pinned_core_cli(self):
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        stage = text[text.index('name: Stage exact Sticky BIN ELF and provenance'):]
        self.assertRegex(stage, r'(?m)^\s+pio pkg list -e "\$PIO_ENV" > sticky-evidence/resolved-packages\.txt$')
        self.assertNotIn('pio pkg list -e "$PIO_ENV" --json-output', stage)

    def test_cppcheck_declares_compatible_os_and_pcre_runtime(self):
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        job = text.split('  cppcheck:', 1)[1].split('  build:', 1)[0]
        self.assertIn('runs-on: ubuntu-24.04', job)
        self.assertIn('sudo apt-get install -y --no-install-recommends libpcre3', job)
        self.assertIn('ctypes.CDLL("libpcre.so.3")', job)

    def test_candidate_cannot_publish(self):
        text = (ROOT / '.github/workflows/release_candidate.yml').read_text()
        self.assertNotIn('contents: write', text)
        self.assertNotIn('action-gh-release', text)
        self.assertNotIn('publish-release-candidate:', text)


if __name__ == '__main__':
    unittest.main()
