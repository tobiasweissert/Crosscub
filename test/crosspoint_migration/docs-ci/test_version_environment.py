"""Actual SCons hook execution with isolated INI and synthetic Git responses."""
import contextlib
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
EXPERIMENT = 'sticky-static-builtins-experimental'
VERSION_KEYS = ('CROSSSMUDGE_RELEASE_VERSION', 'CROSSINK_RELEASE_VERSION',
                'CROSSSMUDGE_RC_HASH', 'CROSSINK_RC_HASH')


class VersionEnvironment(unittest.TestCase):
    def execute(self, name, values=None):
        with tempfile.TemporaryDirectory(dir=tempfile.gettempdir()) as tmp:
            root = Path(tmp)
            (root/'platformio.ini').write_text('[crosssmudge]\nversion = 9.8.7\n[crossink]\nversion = 9.8.7\n')
            class Env(dict):
                def Append(self, **kwargs):
                    self.setdefault('defines', []).extend(kwargs['CPPDEFINES'])
            env = Env(PIOENV=name, PROJECT_DIR=str(root))
            def git(argv, **kwargs):
                self.assertEqual(Path(kwargs['cwd']), root)
                if argv == ['git', 'rev-parse', '--short', 'HEAD']:
                    return 'abcde12345\n'
                if argv == ['git', 'rev-parse', '--abbrev-ref', 'HEAD']:
                    return 'feature/review\n'
                if argv == ['git', 'status', '--porcelain', '--untracked-files=no']:
                    return ''
                if argv == ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard']:
                    return b'platformio.ini\0'
                raise AssertionError('unexpected external command')
            scope = {'env': env, 'Import': lambda *_: None}
            settings = dict.fromkeys(VERSION_KEYS, '')
            settings.update(values or {})
            with patch.dict(os.environ, settings), patch('subprocess.check_output', side_effect=git), contextlib.redirect_stdout(io.StringIO()):
                exec(compile((ROOT/'scripts/git_branch.py').read_text(), '<actual-version-hook>', 'exec'), scope)
            self.assertNotIn('__file__', scope)
            return env, scope

    @staticmethod
    def definitions(env):
        return dict(item for item in env['defines'] if isinstance(item, tuple))

    def test_experimental_matches_sticky_without_file(self):
        ordinary, _ = self.execute('sticky')
        experiment, _ = self.execute(EXPERIMENT)
        self.assertEqual(experiment['defines'], ordinary['defines'])
        values = self.definitions(experiment)
        self.assertEqual(values['CROSSINK_VERSION'], values['CROSSSMUDGE_VERSION'])
        self.assertIn('9.8.7-sticky', values['CROSSINK_VERSION'])
        self.assertEqual(values['CROSSINK_GIT_SHA'], self.definitions(ordinary)['CROSSINK_GIT_SHA'])

    def test_release_and_rc_precedence_preserved(self):
        for settings, expected in [
            ({'CROSSSMUDGE_RELEASE_VERSION': 'v2.3.4'}, '2.3.4-sticky'),
            ({'CROSSINK_RELEASE_VERSION': 'v3.4.5'}, '3.4.5-sticky'),
            ({'CROSSSMUDGE_RELEASE_VERSION': 'v2.3.4', 'CROSSINK_RELEASE_VERSION': 'v3.4.5'}, '2.3.4-sticky'),
            ({'CROSSSMUDGE_RC_HASH': 'rc-hash'}, '9.8.7-rc-hash-RC'),
            ({'CROSSINK_RC_HASH': 'legacy-hash'}, '9.8.7-legacy-hash-RC'),
            ({'CROSSSMUDGE_RC_HASH': 'first', 'CROSSINK_RC_HASH': 'second', 'CROSSSMUDGE_RELEASE_VERSION': 'v2.3.4'}, '9.8.7-first-RC'),
        ]:
            with self.subTest(settings=settings):
                ordinary, _ = self.execute('sticky', settings)
                experiment, _ = self.execute(EXPERIMENT, settings)
                self.assertEqual(experiment['defines'], ordinary['defines'])
                self.assertIn(expected, self.definitions(experiment)['CROSSINK_VERSION'])

    def test_recognized_controls_keep_versions(self):
        for name in ['default', 'sticky', 'sticky-owner-test', 'debug', 'sticky-debug', 'test', 'gh_release_rc']:
            with self.subTest(name=name):
                env, _ = self.execute(name)
                defs = self.definitions(env)
                self.assertEqual(defs['CROSSINK_VERSION'], defs['CROSSSMUDGE_VERSION'])
                self.assertIn('CROSSINK_GIT_SHA', defs)
                self.assertIn('CROSSINK_GIT_DIRTY', defs)

    def test_simulators_and_prefix_lookalikes_remain_unhandled(self):
        for name in ['simulator', 'simulator-debug', 'other-device', EXPERIMENT+'-other']:
            with self.subTest(name=name):
                env, scope = self.execute(name)
                defs = self.definitions(env)
                self.assertNotIn('CROSSINK_VERSION', defs)
                self.assertNotIn('CROSSSMUDGE_VERSION', defs)
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(ValueError):
                    scope['get_hardware_version'](env['PROJECT_DIR'], name)


if __name__ == '__main__':
    unittest.main(verbosity=2)
