#!/usr/bin/env python3
"""Portable release tooling tests. All image fixtures are SYNTHETIC, NOT FIRMWARE."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import subprocess
import os
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))


def validator():
    path = ROOT / 'scripts/validate_ota_release.py'
    if not path.exists():
        raise AssertionError('release validator is missing')
    spec = importlib.util.spec_from_file_location('release_validator', path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class VersionTests(unittest.TestCase):
    def test_descriptor_numeric_version_contract(self):
        mod = validator()
        for value in ['1', '1.2', '1.6.5.4', '000000001.2', '123456789.123456789.123456789.1']:
            self.assertEqual(mod.validate_version(value), value)
        for value in ['', 'v1', 'crosscub-v1', ' 1', '1\n', '1.', '.1', '1..2',
                      '1.2.3.4.5', '1234567890', '1-rc', '١', '1/2',
                      '123456789.123456789.123456789.12', '$(touch nope)']:
            with self.subTest(value=value), self.assertRaises(ValueError):
                mod.validate_version(value)


def synthetic_image(version='1.6.5.4', project='Crosscub', board='sticky', chip=9):
    """Minimal ESP framing fixture; contains no executable firmware."""
    header = bytearray(24)
    header[0:2] = bytes([0xe9, 1])
    struct.pack_into('<H', header, 12, chip)
    header[23] = 1
    data = bytearray(65536)
    struct.pack_into('<I', data, 0, 0xabcd5432)
    data[16:16+len(version)] = version.encode()
    data[48:48+len(project)] = project.encode()
    marker = ('SYNTHETIC TEST IMAGE - NOT FIRMWARE;CROSSPOINT-BOARD-V1:' + board + ';').encode()
    data[256:256+len(marker)] = marker
    body = header + struct.pack('<II', 0x3c000020, len(data)) + data
    checksum = 0xef
    for byte in data:
        checksum ^= byte
    body += bytes(((len(body) + 16) & ~15) - len(body) - 1) + bytes([checksum])
    return bytes(body) + hashlib.sha256(body).digest()


class ImageTests(unittest.TestCase):
    def test_exact_image_release_identity_and_integrity(self):
        mod = validator()
        self.assertTrue(callable(getattr(mod, 'validate_release', None)), 'image validation is missing')
        with tempfile.TemporaryDirectory(prefix='synthetic-ota-') as tmp:
            path = Path(tmp) / 'crosscub-sticky-v1.6.5.4.bin'
            good = synthetic_image()
            path.write_bytes(good)
            report = mod.validate_release(path, '1.6.5.4')
            self.assertEqual(report['sha256'], hashlib.sha256(good).hexdigest())
            self.assertEqual(report['size'], len(good))
            vectors = [synthetic_image(version='1.6.5.3'), synthetic_image(project='CrossSmudge'),
                       synthetic_image(board='x4'), synthetic_image(board=''), synthetic_image(chip=5),
                       good[:-1], good+b'\0', b'not firmware', bytes(6553601)]
            for offset in [0, 1, 23, 28, 1000, len(good)-33, len(good)-1]:
                damaged = bytearray(good); damaged[offset] ^= 1; vectors.append(bytes(damaged))
            for index, data in enumerate(vectors):
                path.write_bytes(data)
                with self.subTest(index=index), self.assertRaises(ValueError):
                    mod.validate_release(path, '1.6.5.4')
            path.write_bytes(good)
            with self.assertRaises(ValueError):
                mod.validate_release(path, '1.6.5.4', repository='Mumfee/CrossSmudge')
            wrong = path.with_name('crosscub-sticky-v1.6.5.3.bin'); wrong.write_bytes(good)
            with self.assertRaises(ValueError):
                mod.validate_release(wrong, '1.6.5.4')
            with self.assertRaises(ValueError):
                mod.validate_release(path, '1.6.5.4', expected_sha256='0'*64)


class PreparationTests(unittest.TestCase):
    def test_portable_owner_plan_and_explicit_version_write(self):
        script = ROOT / 'scripts/release.sh'
        with tempfile.TemporaryDirectory(prefix='synthetic-release-plan-') as tmp:
            config = Path(tmp) / 'platformio.ini'
            original = '[crosssmudge]\nversion = 1.2.3\n\n[crossink]\nversion = 1.2.3\n; keep me\n[other]\nversion = untouched\n'
            config.write_text(original)
            for publish in [False, True]:
                result = subprocess.run(['bash', str(script), '1.6.5.4', '--config', str(config)] + (['--publish'] if publish else []), text=True, capture_output=True, cwd=tmp)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('--repo tobiasweissert/Crosscub', result.stdout)
                self.assertIn('--ref main', result.stdout)
                self.assertIn('publish=' + str(publish).lower(), result.stdout)
                self.assertEqual(config.read_text(), original)
            result = subprocess.run(['bash', str(script), '1.6.5.4', '--config', str(config), '--write-version'], text=True, capture_output=True, cwd=tmp)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(config.read_text(), original.replace('1.2.3', '1.6.5.4'))
            before = config.read_bytes()
            result = subprocess.run(['bash', str(script), '1;touch hacked', '--config', str(config), '--write-version'], text=True, capture_output=True, cwd=tmp)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(config.read_bytes(), before)
            self.assertFalse((Path(tmp) / 'hacked').exists())


class PublicationTests(unittest.TestCase):
    def test_source_guards_and_draft_default(self):
        import prepare_ota_release as prep
        self.assertTrue(callable(getattr(prep, 'qualify_source', None)), 'source qualification is missing')
        with tempfile.TemporaryDirectory(prefix='synthetic-release-source-') as tmp:
            cwd = Path(tmp)
            def git(*args):
                return subprocess.check_output(['git', *args], cwd=cwd, text=True).strip()
            git('init', '-q', '-b', 'main')
            git('config', 'user.email', 'fixture@example.invalid'); git('config', 'user.name', 'Synthetic fixture')
            (cwd / 'platformio.ini').write_text('[crosssmudge]\nversion = 1.6.5.4\n[crossink]\nversion = 1.6.5.4\n')
            git('add', '.'); git('commit', '-qm', 'SYNTHETIC source fixture; not firmware')
            sha = git('rev-parse', 'HEAD'); git('update-ref', 'refs/remotes/origin/main', sha)
            context = dict(repository='tobiasweissert/Crosscub', event='workflow_dispatch', ref='main',
                           actor='tobiasweissert', publish=False, source_sha=sha, cwd=cwd)
            prep.qualify_source('1.6.5.4', **context)
            for change in [dict(repository='Mumfee/CrossSmudge'), dict(ref='feature'),
                           dict(actor='other', publish=True), dict(event='pull_request')]:
                with self.subTest(change=change), self.assertRaises(ValueError):
                    prep.qualify_source('1.6.5.4', **{**context, **change})
            with self.assertRaises(ValueError):
                prep.qualify_source('1.6.5.3', **context)
            git('checkout', '-qb', 'feature')
            (cwd / 'new').write_text('SYNTHETIC unapproved source'); git('add', '.'); git('commit', '-qm', 'feature')
            with self.assertRaises(ValueError):
                prep.qualify_source('1.6.5.4', **{**context, 'source_sha': git('rev-parse', 'HEAD')})

    def test_remote_release_guards_never_redraft(self):
        import prepare_ota_release as prep
        from unittest.mock import patch
        self.assertTrue(callable(getattr(prep, 'check_remote', None)), 'remote release guard is missing')
        for existing in [dict(draft=False), dict(draft=True)]:
            with patch.object(prep, 'github', return_value=existing), self.assertRaises(ValueError):
                prep.check_remote('1.6.5.4')
        for latest in ['crosscub-v1.6.5.4', 'crosscub-v1.6.5.5', 'v99', 'crosscub-v1-rc']:
            with patch.object(prep, 'github', side_effect=[None, [], {'tag_name': latest}]), self.assertRaises(ValueError):
                prep.check_remote('1.6.5.4')
        with patch.object(prep, 'github', side_effect=[None, [], {'tag_name': 'crosscub-v1.6.5.3'}]):
            prep.check_remote('1.6.5.4')
        with patch.object(prep, 'github', side_effect=[None, [], None]):
            prep.check_remote('1.6.5.4')


    def test_upload_is_validated_draft_first_and_read_back(self):
        import prepare_ota_release as prep
        from unittest.mock import patch
        self.assertTrue(callable(getattr(prep, 'upload_release', None)), 'qualified upload is missing')
        with tempfile.TemporaryDirectory(prefix='synthetic-release-upload-') as tmp:
            path = Path(tmp) / 'crosscub-sticky-v1.6.5.4.bin'
            data = synthetic_image(); path.write_bytes(data)
            sha = 'a' * 40
            base = {'id': 42, 'url': 'https://api.github.com/repos/tobiasweissert/Crosscub/releases/42',
                    'target_commitish': sha, 'tag_name': 'crosscub-v1.6.5.4', 'draft': True, 'prerelease': False,
                    'assets': [{'id': 7, 'url': 'https://api.github.com/repos/tobiasweissert/Crosscub/releases/assets/7', 'name': path.name, 'size': len(data),
                                'digest': 'sha256:' + hashlib.sha256(data).hexdigest(),
                                'browser_download_url': 'https://github.com/tobiasweissert/Crosscub/releases/download/crosscub-v1.6.5.4/' + path.name}]}
            for publish in [False, True]:
                calls = []
                def api(endpoint, **kwargs):
                    calls.append(('api', endpoint, kwargs))
                    if endpoint.startswith('git/ref/'):
                        return {'object': {'type': 'commit', 'sha': sha}}
                    if endpoint == 'releases/latest':
                        return {**base, 'draft': False}
                    return {**base, 'draft': not any(c[2].get('method') == 'PATCH' for c in calls)}
                with patch.object(prep, 'check_remote') as guard, patch.object(prep, 'github', side_effect=api), patch.object(prep.subprocess, 'run', return_value=type('R', (), {'stdout': data})()), patch('urllib.request.urlopen') as download:
                    download.return_value.__enter__.return_value.read.return_value = data
                    prep.upload_release('1.6.5.4', path, sha, publish=publish)
                    guard.assert_called_once()
                writes = [c for c in calls if c[2].get('fields')]
                self.assertEqual(writes[0][1], 'releases')
                self.assertIs(writes[0][2]['fields']['draft'], True)
                self.assertEqual(len(writes), 2 if publish else 1)
                if publish:
                    self.assertEqual(writes[1][1], 'releases/42')
                    self.assertEqual(writes[1][2]['fields'], {'draft': False, 'prerelease': False, 'make_latest': 'true'})
            bad = {**base, 'assets': [{**base['assets'][0], 'digest': 'sha256:' + '0'*64}]}
            with patch.object(prep, 'check_remote'), patch.object(prep, 'github', side_effect=[{'object': {'type': 'commit', 'sha': sha}}, base, bad]), patch.object(prep.subprocess, 'run') as command:
                with self.assertRaises(ValueError):
                    prep.upload_release('1.6.5.4', path, sha, publish=True)
                self.assertEqual(command.call_count, 1, 'bad uploaded asset must remain draft, never publish')
            path.write_bytes(b'SYNTHETIC invalid image - NOT FIRMWARE')
            with patch.object(prep, 'release_command') as command:
                with self.assertRaises(ValueError):
                    prep.upload_release('1.6.5.4', path, sha, publish=True)
                command.assert_not_called()


    def test_published_duplicate_is_a_prebuild_noop_not_a_redraft(self):
        import prepare_ota_release as prep
        from unittest.mock import patch
        import inspect
        self.assertIn('skip_published', inspect.signature(prep.check_remote).parameters,
                      'published duplicate prebuild no-op is missing')
        with patch.object(prep, 'github', return_value={'tag_name': 'crosscub-v1.6.5.4', 'draft': False, 'prerelease': False}) as api:
            self.assertFalse(prep.check_remote('1.6.5.4', skip_published=True))
            api.assert_called_once_with('releases/tags/crosscub-v1.6.5.4')
        with tempfile.TemporaryDirectory(prefix='synthetic-duplicate-workflow-') as tmp:
            output = Path(tmp) / 'github-output'
            env = {**os.environ, 'GITHUB_ACTIONS': 'true', 'GITHUB_EVENT_NAME': 'push',
                   'GITHUB_REF_NAME': 'crosscub-v1.6.5.4', 'GITHUB_REPOSITORY': 'tobiasweissert/Crosscub',
                   'GITHUB_SHA': 'a'*40, 'GITHUB_ACTOR': 'tobiasweissert', 'GITHUB_OUTPUT': str(output),
                   'PUBLISH_RELEASE': 'false'}
            with patch.dict(os.environ, env), patch.object(sys, 'argv', ['prepare_ota_release.py', '--ci-check']), \
                    patch.object(prep, 'qualify_source'), patch.object(prep, 'github', return_value={
                        'tag_name': 'crosscub-v1.6.5.4', 'draft': False, 'prerelease': False}), \
                    patch.object(prep, 'release_command') as command:
                prep.main()
                command.assert_not_called()
            self.assertEqual(output.read_text(), 'release_version=1.6.5.4\nskip_release=true\n')
        text = (ROOT / '.github/workflows/release.yml').read_text()
        self.assertIn('skip_release: ${{ steps.version.outputs.skip_release }}', text)
        self.assertIn("if: needs.prepare-release.outputs.skip_release != 'true'", text)

    def test_manual_release_creates_and_verifies_exact_tag_before_draft(self):
        import prepare_ota_release as prep
        from unittest.mock import patch
        self.assertTrue(callable(getattr(prep, 'ensure_remote_tag', None)), 'manual draft needs a verified tag')
        sha = 'a' * 40
        tag = 'crosscub-v1.6.5.4'
        with patch.object(prep, 'github', side_effect=[None, {'object': {'type': 'commit', 'sha': sha}}, {'object': {'type': 'commit', 'sha': sha}}]) as api:
            prep.ensure_remote_tag(tag, sha)
            self.assertEqual(api.call_args_list[1].kwargs['fields'], {'ref': 'refs/tags/' + tag, 'sha': sha})
            self.assertEqual(api.call_count, 3)
        with patch.object(prep, 'github', return_value={'object': {'type': 'commit', 'sha': 'b'*40}}), self.assertRaises(ValueError):
            prep.ensure_remote_tag(tag, sha)


class WorkflowTests(unittest.TestCase):
    def test_workflow_guards_and_validation_are_wired(self):
        text = (ROOT / '.github/workflows/release.yml').read_text()
        for required in ['publish:', 'default: false', 'github.repository == \'tobiasweissert/Crosscub\'',
                         'INPUT_VERSION: ${{ inputs.version }}', '--ci-check', '--ci-upload',
                         'scripts/validate_ota_release.py', 'python3 -m unittest discover -s test/ota_release -v',
                         'cancel-in-progress: false']:
            self.assertIn(required, text)
        self.assertNotIn('VERSION="${{ inputs.version }}"', text)
        self.assertNotIn('softprops/action-gh-release', text)
        self.assertNotIn('sed -i', text)
        self.assertLess(text.index('scripts/validate_ota_release.py'), text.index('uses: actions/upload-artifact'))

    def test_ci_mode_refuses_unowned_context_before_network(self):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/prepare_ota_release.py'), '--ci-check'],
                                env={**os.environ, 'GITHUB_ACTIONS': 'true', 'GITHUB_REPOSITORY': 'Mumfee/CrossSmudge',
                                     'GITHUB_EVENT_NAME': 'workflow_dispatch', 'INPUT_VERSION': '1.6.5.4'},
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('release repository must be tobiasweissert/Crosscub', result.stderr)


class CatalogTests(unittest.TestCase):
    def test_catalog_requires_exact_qualified_owner_image(self):
        with tempfile.TemporaryDirectory(prefix='synthetic-release-catalog-') as tmp:
            cwd = Path(tmp)
            path = cwd / 'crosscub-sticky-v1.6.5.4.bin'
            path.write_bytes(synthetic_image())
            command = [sys.executable, str(ROOT / 'scripts/generate_release_catalog.py'), '--version', '1.6.5.4',
                       '--repo', 'tobiasweissert/Crosscub', '--firmware', str(path), '--output', str(cwd / 'catalog')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            record = json.loads((cwd / 'catalog').read_text())['releases'][0]
            self.assertEqual(record['firmware_sha256'], hashlib.sha256(path.read_bytes()).hexdigest())
            for args in [[], ['--version', '1.6.5.3'], ['--version', 'v1.6.5.4'],
                         ['--firmware-base-url', 'https://unowned.invalid/']]:
                path.write_bytes(synthetic_image(project='CrossSmudge') if not args else synthetic_image())
                result = subprocess.run(command + args, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, result.stdout)


if __name__ == '__main__':
    unittest.main()
