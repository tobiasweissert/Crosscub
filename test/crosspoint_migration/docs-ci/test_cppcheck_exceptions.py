"""Exact exceptions must authenticate source/revision and never broaden the gate."""
from pathlib import Path
import importlib.util
import json
import os
import runpy
import shutil
import subprocess
from unittest.mock import patch
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


def load():
    spec = importlib.util.spec_from_file_location('exact_cppcheck', ROOT/'scripts/cppcheck_exact.py')
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ExactTests(unittest.TestCase):
    def test_source_line_id_and_revision_are_bound(self):
        helper = load()
        entries = json.loads((ROOT/'scripts/cppcheck-exceptions.json').read_text())
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for entry in entries['exceptions']:
                for relative in (entry['path'], entry['test']):
                    path = root/relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes((ROOT/relative).read_bytes())
            for relative in helper.PATCHES:
                target = root/'freeink-sdk'/relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(subprocess.check_output([
                    'git', '-C', str(ROOT/'freeink-sdk'), 'show', helper.SDK_REVISION+':'+relative]))
            helper.apply_patches(root/'freeink-sdk', verify_revision=False)
            gitdir = subprocess.check_output(['git', '-C', str(ROOT/'freeink-sdk'), 'rev-parse', '--absolute-git-dir'], text=True).strip()
            (root/'freeink-sdk/.git').write_text('gitdir: '+gitdir+'\n')
            args = helper.suppressions(root)
            self.assertEqual(len(args), len(entries['exceptions']))
            self.assertTrue(all(arg.startswith('--suppress=') and '*' not in arg for arg in args))
            for entry in entries['exceptions']:
                for changes in ({'sha256': '0'*64}, {'line': entry['line']+1}, {'id': '*'}, {'path': '../escape'}):
                    with self.subTest(path=entry['path'], line=entry['line'], changes=changes):
                        with self.assertRaises(RuntimeError):
                            helper.validate_entry(root, dict(entry, **changes))
                source = root/entry['path']
                original = source.read_bytes()
                source.write_bytes(original+b'\n')
                with self.assertRaises(RuntimeError):
                    helper.suppressions(root)
                source.write_bytes(original)
            (root/'freeink-sdk/.git').unlink()
            subprocess.run(['git', 'init', '-q', str(root/'freeink-sdk')], check=True)
            with self.assertRaises(RuntimeError):
                helper.suppressions(root)

    def test_sdk_patch_is_exact_idempotent_and_unknown_bytes_fail(self):
        helper = load()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for path in helper.PATCHES:
                target = root/path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(subprocess.check_output(['git', '-C', str(ROOT/'freeink-sdk'), 'show', helper.SDK_REVISION+':'+path]))
            helper.apply_patches(root, verify_revision=False)
            before = {path:(root/path).read_bytes() for path in helper.PATCHES}
            helper.apply_patches(root, verify_revision=False)
            self.assertEqual(before, {path:(root/path).read_bytes() for path in helper.PATCHES})
            first = next(iter(helper.PATCHES))
            (root/first).write_bytes(before[first]+b'\n')
            with self.assertRaises(RuntimeError):
                helper.apply_patches(root, verify_revision=False)


class CombinedHookTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        shutil.copytree(ROOT/'scripts', self.root/'scripts')
        self.sdk = self.root/'freeink-sdk'
        self.sdk.mkdir()
        source = os.environ.get('CROSSCUB_SDK_GIT_DIR')
        if source:
            git = ['git', '--git-dir='+source]
        else:
            git = ['git', '-C', str(ROOT/'freeink-sdk')]
        gitdir = subprocess.check_output(git+['rev-parse', '--absolute-git-dir'], text=True).strip()
        (self.sdk/'.git').write_text('gitdir: '+gitdir+'\n')
        helper = load()
        # Load definitions through the real hook with no SDK present yet.
        overlay = self.run_hook()
        self.paths = list(helper.PATCHES)+list(overlay['SDK_SECURENET_PREIMAGES'])
        for path in self.paths:
            target = self.sdk/path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(subprocess.check_output(git+['show', helper.SDK_REVISION+':'+path]))

    def run_hook(self):
        env = type('Env', (), {'subst': lambda env, value: str(self.root)})()
        with patch.dict(os.environ, {'CROSSCUB_PRIVATE_EVIDENCE': ''}):
            return runpy.run_path(str(self.root/'scripts/patch_wolfssl.py'),
                                  init_globals={'Import': lambda value: None, 'env': env})

    def snapshot(self):
        return {path: (self.sdk/path).read_bytes() for path in self.paths}

    def test_unknown_family_bytes_reject_without_any_sdk_mutation(self):
        original = self.snapshot()
        for path in (self.paths[-1], list(load().PATCHES)[-1]):
            with self.subTest(corrupt=path):
                for relative, data in original.items():
                    (self.sdk/relative).write_bytes(data)
                target = self.sdk/path
                target.write_bytes(target.read_bytes()+b'\n// unknown-byte canary\n')
                before = self.snapshot()
                with self.assertRaisesRegex(RuntimeError, 'Unknown .*bytes|Unknown .*preimage'):
                    self.run_hook()
                self.assertEqual(before, self.snapshot())

    def test_valid_combined_hook_is_idempotent(self):
        original = self.snapshot()
        self.run_hook()
        installed = self.snapshot()
        self.assertEqual(len(installed), len(load().PATCHES) + len(self.run_hook()['SDK_SECURENET_PREIMAGES']))
        self.assertTrue(all(installed[path] != original[path] for path in self.paths))
        self.run_hook()
        self.assertEqual(installed, self.snapshot())


if __name__ == '__main__':
    unittest.main()
