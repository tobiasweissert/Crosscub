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

    def test_standard_build_archives_before_stats(self):
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertIn('workflow_dispatch:', text)
        self.assertIn('pioarduino==6.1.19', text)
        self.assertIn('634a3f8c067a3714a6a7c622015138af096ab5d2', text)
        self.assertIn('55.03.311', text)
        self.assertIn('firmware.elf', text)
        self.assertIn('image-validation.json', text)
        self.assertIn('toolchain-manifest.json', text)
        self.assertLess(text.index('name: Seal exact Sticky evidence'), text.index('name: Retain encrypted Sticky evidence'))
        self.assertLess(text.index('name: Retain encrypted Sticky evidence'), text.index('name: Extract firmware stats'))
        portable = text.split('  portable-policy:', 1)[1].split('  build:', 1)[0]
        self.assertNotIn('needs:', portable)
        self.assertIn('name: Check portable docs and workflow policy', portable)
        self.assertNotIn('github.event.repository.private', text)
        self.assertIn('vars.CROSSCUB_ARTIFACT_RECIPIENT_CERT', text)
        self.assertIn("if: always() && steps.seal.outcome == 'success'", text)
        uploads = text[text.index('      - name: Retain encrypted Sticky evidence'):text.index('      - name: Extract firmware stats')]
        upload_paths = re.search(r'(?m)^          path: \|\n((?:            .+\n)+)', uploads)
        self.assertIsNotNone(upload_paths)
        self.assertEqual([p.strip() for p in upload_paths[1].splitlines()],
                         ['sticky-sealed/sealed.envelope', 'sticky-sealed/receipt.json'])
        self.assertIn("if: always() && steps.recipient.outcome == 'success' && steps.firmware.outcome != 'skipped'", text)
        self.assertIn('retention-days: 31', uploads)
        self.assertNotIn('tee pio-sticky.log', text)
        self.assertNotIn('sticky-evidence/', uploads)
        self.assertNotIn('*.elf', uploads)
        self.assertNotIn('*.bin', uploads)
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
        commands = re.findall(r'(?m)^            pio run .+$', build)
        self.assertEqual(commands, ['            pio run --verbose "-j$BUILD_JOBS" -e "$PIO_ENV" >> "pio-$PIO_ENV.log" 2>&1'])
        analyzer = text.split('      - name: Run cppcheck', 1)[1].split('      - name: Seal analyzer', 1)[0]
        self.assertIn('python3 scripts/ci_cppcheck_capture.py check --verbose --fail-on-defect low --fail-on-defect medium --fail-on-defect high > cppcheck-evidence/check.log 2>&1', analyzer)
        self.assertNotIn('continue-on-error', analyzer)
        self.assertIn('cppcheck-sealed/sealed.envelope', text)
        self.assertNotIn('path: cppcheck-evidence', text)

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
