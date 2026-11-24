"""Offline synthetic state only: no resolver, compiler or firmware proof."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / 'scripts/check_sticky_cold_state.py'


class ColdState(unittest.TestCase):
    def test_fresh_and_reused_packages(self):
        self.assertTrue(HELPER.is_file(), 'read-only cold admission helper missing')
        spec = importlib.util.spec_from_file_location('cold', HELPER)
        cold = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cold)
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / 'home'
            project = Path(tmp) / 'project'
            home.mkdir()
            project.mkdir()
            (project / 'platformio.ini').write_text('[platformio]\nextra_configs = platformio.local.ini\n[env:sticky]\n')
            with patch.dict(os.environ, {'HOME': str(home)}, clear=True), patch('os.getcwd', return_value=str(project)):
                cold.admit(project, home, 'fresh')
                packages = home / '.platformio/packages'
                packages.mkdir(parents=True)
                cold.admit(project, home, 'fresh')
                (packages / 'reused').write_text('synthetic')
                with self.assertRaises(cold.Rejected):
                    cold.admit(project, home, 'fresh')


class ColdMatrix(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.home = self.root / 'home'
        self.project = self.root / 'project'
        self.home.mkdir()
        self.project.mkdir()
        self.config = self.project / 'platformio.ini'
        self.config.write_text('[platformio]\nextra_configs = platformio.local.ini\n[env:sticky]\n')
        spec = importlib.util.spec_from_file_location('cold', HELPER)
        self.cold = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.cold)
        self.addCleanup(patch.stopall)
        patch.dict(os.environ, {'HOME': str(self.home)}, clear=True).start()
        self.oldcwd = Path.cwd()
        os.chdir(self.project)
        self.addCleanup(os.chdir, self.oldcwd)

    def inventory(self):
        import hashlib
        import stat
        result = {}
        for path in self.root.rglob('*'):
            mode = path.lstat().st_mode
            result[str(path.relative_to(self.root))] = (mode, os.readlink(path) if stat.S_ISLNK(mode) else hashlib.sha256(path.read_bytes()).hexdigest() if stat.S_ISREG(mode) else None)
        return result

    def reject_unchanged(self):
        before = self.inventory()
        with self.assertRaises(self.cold.Rejected):
            self.cold.admit(self.project, self.home, 'fresh')
        self.assertEqual(self.inventory(), before)

    def test_every_payload_root_and_type(self):
        for name in ('packages', 'platforms', '.cache', 'lib', 'penv'):
            for kind in ('nonempty', 'file', 'symlink', 'dangling', 'fifo'):
                with self.subTest(root=name, kind=kind):
                    root = self.home / '.platformio' / name
                    root.parent.mkdir(exist_ok=True)
                    if kind == 'nonempty':
                        root.mkdir()
                        (root / 'canary').write_bytes(b'synthetic private canary')
                    elif kind == 'file':
                        root.write_bytes(b'synthetic')
                    elif kind in ('symlink', 'dangling'):
                        root.symlink_to(self.project if kind == 'symlink' else self.root / 'absent')
                    else:
                        os.mkfifo(root)
                    self.reject_unchanged()
                    if kind == 'nonempty':
                        (root / 'canary').unlink()
                        root.rmdir()
                    else:
                        root.unlink()

    def test_each_prior_project_state(self):
        for name in ('.pio', 'platformio.local.ini', 'sdkconfig.sticky', 'sdkconfig.defaults'):
            for kind in ('file', 'directory', 'dangling'):
                with self.subTest(name=name, kind=kind):
                    path = self.project / name
                    if kind == 'file':
                        path.write_text('synthetic')
                    elif kind == 'directory':
                        path.mkdir()
                    else:
                        path.symlink_to(self.root / 'missing')
                    self.reject_unchanged()
                    path.rmdir() if kind == 'directory' else path.unlink()

    def test_symlinked_home_core_and_project_ancestors(self):
        for original in (self.home, self.project):
            alias = self.root / ('alias-' + original.name)
            alias.symlink_to(original, target_is_directory=True)
            with self.assertRaises(self.cold.Rejected):
                self.cold.admit(alias if original == self.project else self.project, alias if original == self.home else self.home, 'fresh')
        core = self.home / '.platformio'
        core.symlink_to(self.project)
        self.reject_unchanged()

    def test_finite_override_closure_independently_from_core_metadata(self):
        from platformio.project.options import ProjectOptions
        controls = {name for option in ProjectOptions.values() if option.sysenvvar
                    for name in [option.sysenvvar] + ['PLATFORMIO_' + old.upper() for old in (option.oldnames or [])]}
        self.assertEqual(controls, {'PLATFORMIO_' + name for name in self.cold.OVERRIDES})
        for name in sorted(controls):
            for value in ('', 'synthetic-secret-redirect'):
                with self.subTest(name=name, empty=not value), patch.dict(os.environ, {name: value}):
                    self.reject_unchanged()

    def test_each_config_directory_and_alias_and_extra_config(self):
        from platformio.project.options import ProjectOptions
        for option in ProjectOptions.values():
            if option.group != 'directory':
                continue
            for name in [option.name] + (option.oldnames or []):
                with self.subTest(name=name):
                    self.config.write_text('[platformio]\nextra_configs = platformio.local.ini\n' + name + ' = /synthetic\n[env:sticky]\n')
                    self.reject_unchanged()
        for extra in ('another.ini', '${sysenv.INJECT}', 'platformio.local.ini, other.ini', ''):
            self.config.write_text('[platformio]\nextra_configs = ' + extra + '\n[env:sticky]\n')
            self.reject_unchanged()

    def test_alternate_in_process_config_rejects(self):
        from platformio import app
        app.set_session_var('custom_project_conf', 'synthetic.ini')
        try:
            self.reject_unchanged()
        finally:
            app.set_session_var('custom_project_conf', None)

    def test_real_nested_core_api_and_coherence(self):
        import json
        import subprocess
        import sys
        import sysconfig
        import venv
        penv = self.home / '.platformio/penv'
        venv.EnvBuilder(with_pip=False).create(penv)
        # Synthetic interpreter fixture using the installed, real pinned Core.
        sites = list(penv.glob('lib/python*/site-packages'))
        (sites[0] / 'fixture-runtime.pth').write_text(sysconfig.get_path('purelib') + '\n')
        code = ('import importlib.util,json,pathlib; s=importlib.util.spec_from_file_location("cold",' + repr(str(HELPER)) + '); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); print(json.dumps(m.snapshot(pathlib.Path.cwd(), pathlib.Path.home())))')
        result = subprocess.run([str(penv / 'bin/python'), '-B', '-c', code], capture_output=True, text=True, check=True)
        nested = json.loads(result.stdout)
        outer = self.cold.admit(self.project, self.home, 'coherent')
        self.cold.coherent(outer, nested, self.home)
        for side in ('outer', 'nested'):
            for root in ('core', 'packages', 'platforms'):
                bad = json.loads(json.dumps(outer if side == 'outer' else nested))
                bad['roots'][root] += '-different'
                with self.subTest(side=side, root=root), self.assertRaises(self.cold.Rejected):
                    self.cold.coherent(bad if side == 'outer' else outer, bad if side == 'nested' else nested, self.home)
        nested['prefix'] += '-different'
        with self.assertRaises(self.cold.Rejected):
            self.cold.coherent(outer, nested, self.home)
        nested['version'] = '6.1.18'
        with self.assertRaises(self.cold.Rejected):
            self.cold.coherent(outer, nested, self.home)

    def test_cli_rejects_home_override_without_leak(self):
        import subprocess
        import sys
        result = subprocess.run([sys.executable, '-B', str(HELPER), 'fresh'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertNotIn(str(self.home), result.stdout + result.stderr)
        self.assertIn('no state repaired or removed', result.stderr)


class Workflow(unittest.TestCase):
    def blocks(self):
        import re
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        return dict(re.findall(r'^      - name: ([^\n]+)\n(.*?)(?=^      - |^  [a-z].*:\n|\Z)', text, re.M | re.S))

    def script(self, name):
        import textwrap
        return textwrap.dedent(self.blocks()[name].split('        run: |\n', 1)[1])

    def test_typed_expression_truth_table(self):
        import re
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        expr = re.search(r'COLD_QUALIFICATION: \$\{\{ (.*?) \}\}', text)[1]
        # Evaluate this bounded boolean expression only; not an Actions emulator.
        for event in ('workflow_dispatch', 'pull_request', 'push', 'workflow_call'):
            for value in (True, False):
                actual = expr.replace('github.event_name', repr(event)).replace('inputs.cold_qualification', repr(value)).replace('&&', ' and ').replace('true', 'True')
                self.assertEqual(eval(actual, {'__builtins__': {}}), event == 'workflow_dispatch' and value)
        self.assertIn('type: boolean\n        default: false', text)

    def test_cache_order_and_policy_preservation(self):
        import hashlib
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        build = text.split('  build:\n', 1)[1].split('  required-tests:\n', 1)[0]
        self.assertEqual(build.count('uses: actions/cache@'), 1)
        self.assertIn("if: ${{ !(github.event_name == 'workflow_dispatch' && inputs.cold_qualification == true) }}", self.blocks()['Cache PlatformIO packages'])
        self.assertLess(build.index('scripts/check_sticky_cold_state.py fresh'), build.index('pio pkg install'))
        self.assertLess(build.index('pio pkg install'), build.index('scripts/check_sticky_cold_state.py coherent'))
        self.assertLess(build.index('scripts/check_sticky_cold_state.py coherent'), build.index('pio run --verbose'))
        self.assertLess(build.index('seal_ci_artifacts.py validate-cert'), build.index('pio run --verbose'))
        frozen = {
            'Build ${{ matrix.environment }} firmware': 'bdb1c3aadcbfd630351661ceff4d317880e0349d5d0b5a841b3017058a80db0e',
            'Stage exact Sticky BIN ELF and provenance': '25c88ced542ad2a90e7adeb9d7d7aff202ac78c651de5e8830072a5d53139e76',
            'Seal exact Sticky evidence including failed-build diagnostics': '008fdbf9069325a6dadebe9232b78a07ddb1cb06c8c4fda2cdf59d7ec13784c7',
            'Retain encrypted Sticky evidence': '495609724997fc2eec46146902835a4d740b5b94d80e6aee8d7dee859b31b47a',
            'Require recipient-encrypted artifact retention': '7e5b077917b0ec672ee31a8c81329c12d4c00d609276d645cc6f2f2f9adadbc0'}
        for name, expected in frozen.items():
            self.assertEqual(hashlib.sha256(self.blocks()[name].encode()).hexdigest(), expected, name)
        cppcheck = text.split('  cppcheck:\n')[1].split('  portable-policy:\n')[0]
        self.assertEqual(hashlib.sha256(cppcheck.encode()).hexdigest(), '26fc272a63a5ae2b6ce29f2abc4c7a58118aa7f752643c496d61db32b370b19c')

    def test_actual_fresh_coherent_shell_blocks_with_synthetic_home(self):
        import subprocess
        import sys
        import sysconfig
        import venv
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            home = root / 'home'
            home.mkdir()
            project = root / 'project'
            project.mkdir()
            (project / 'platformio.ini').write_text('[platformio]\nextra_configs = platformio.local.ini\n[env:sticky]\n')
            (project / 'scripts').symlink_to(ROOT / 'scripts')
            tools = root / 'tools'
            tools.mkdir()
            import shlex
            (tools / 'python3').write_text('#!/bin/sh\nexec ' + shlex.quote(sys.executable) + ' "$@"\n')
            (tools / 'python3').chmod(0o700)
            # Explicit passwd fixture seam; no real home/package tree touched.
            site = root / 'site'
            site.mkdir()
            (site / 'sitecustomize.py').write_text('import pwd,types\npwd.getpwuid=lambda uid: types.SimpleNamespace(pw_dir=' + repr(str(home)) + ')\n')
            env = {**os.environ, 'HOME': str(home), 'PYTHONPATH': str(site), 'PATH': str(tools) + os.pathsep + os.environ['PATH'], 'PYTHONDONTWRITEBYTECODE': '1'}
            fresh = self.script('Admit fresh cold Sticky state before resolver')
            result = subprocess.run(['bash', '-c', fresh], cwd=project, env=env, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            config = project / 'platformio.ini'
            original = config.read_bytes()
            config.write_bytes(b'PRIVATE_SYNTHETIC_BAD_CONFIG_CANARY\n')
            result = subprocess.run(['bash', '-c', fresh], cwd=project, env=env, capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertNotIn(b'PRIVATE_SYNTHETIC_BAD_CONFIG_CANARY', result.stdout + result.stderr)
            self.assertNotIn(b'Traceback', result.stdout + result.stderr)
            config.write_bytes(original)
            penv = home / '.platformio/penv'
            venv.EnvBuilder(with_pip=False).create(penv)
            (next(penv.glob('lib/python*/site-packages')) / 'fixture.pth').write_text(sysconfig.get_path('purelib') + '\n')
            coherent = self.script('Admit coherent outer and nested cold Core roots')
            result = subprocess.run(['bash', '-c', coherent], cwd=project, env=env, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            receipt = (project / 'sticky-evidence/cold-coherent.json').read_text()
            self.assertNotIn('PYTHONPATH', receipt)
            self.assertNotIn('PATH', receipt)
            reused = home / '.platformio/packages'
            reused.mkdir()
            (reused / 'canary').write_bytes(b'synthetic reused state')
            result = subprocess.run(['bash', '-c', fresh], cwd=project, env=env, capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual((reused / 'canary').read_bytes(), b'synthetic reused state')

    def test_actual_ci_failed_build_capture_staging_and_sealing(self):
        import shlex
        import subprocess
        import sys
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'scripts').symlink_to(ROOT / 'scripts')
            (root / 'platformio.ini').write_bytes((ROOT / 'platformio.ini').read_bytes())
            (root / 'sticky-evidence').mkdir()
            tools = root / 'tools'
            tools.mkdir()
            # Synthetic producer command boundary, never an SDK fixture/edit.
            (tools / 'python3').write_text('#!/bin/bash\nif [[ "$1" == scripts/ci_producer_capture.py ]]; then echo SYNTHETIC_PRODUCER; exit "${SYNTHETIC_CAPTURE_EXIT:-0}"; fi\nexec ' + shlex.quote(sys.executable) + ' "$@"\n')
            (tools / 'pio').write_text('#!/bin/bash\nif [[ "$1" == run ]]; then echo PRIVATE_SYNTHETIC_BUILD_CANARY; exit 23; fi\necho SYNTHETIC_PACKAGE_QUERY\n')
            (tools / 'git').write_text('#!/bin/sh\nexit 0\n')
            for path in tools.iterdir():
                path.chmod(0o700)
            env = {**os.environ, 'PATH': str(tools) + os.pathsep + os.environ['PATH'], 'HOME': str(root), 'PIO_ENV': 'sticky', 'BUILD_JOBS': '1', 'GITHUB_WORKSPACE': str(root), 'RUNNER_TEMP': str(root), 'GITHUB_REPOSITORY': 'tobiasweissert/Crosscub', 'GITHUB_SHA': 'a' * 40, 'GITHUB_RUN_ID': '1', 'GITHUB_RUN_ATTEMPT': '1'}
            def run(name, **extra):
                return subprocess.run(['bash', '-c', self.script(name)], cwd=root, env={**env, **extra}, capture_output=True)
            for capture, expected in [('37', 37), ('0', 23)]:
                result = run('Build ${{ matrix.environment }} firmware', SYNTHETIC_CAPTURE_EXIT=capture)
                self.assertEqual(result.returncode, expected)
                self.assertNotIn(b'PRIVATE_SYNTHETIC_BUILD_CANARY', result.stdout + result.stderr)
            measurements = (root/'sticky-evidence/build-resources.txt').read_text()
            self.assertIn('workers=1', measurements)
            self.assertIn('elapsed_seconds=', measurements)
            self.assertIn('max_process_rss_kib=', measurements)
            self.assertIn('exit_code=23', measurements)
            build = root / '.pio/build/sticky'
            build.mkdir(parents=True)
            for name in ('firmware.elf', 'firmware.bin', 'firmware.map'):
                (build / name).write_bytes(b'SYNTHETIC_NOT_FIRMWARE_' + name.encode())
            result = run('Stage exact Sticky BIN ELF and provenance')
            self.assertEqual(result.returncode, 1)  # Real missing-config collector/invalid-image failure.
            for name in ('firmware.elf', 'firmware.bin', 'firmware.map'):
                self.assertEqual((root / 'sticky-evidence' / name).read_bytes(), (build / name).read_bytes())
            self.assertIn(b'PRIVATE_SYNTHETIC_BUILD_CANARY', (root / 'sticky-evidence/build.log').read_bytes())
            generated = subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:3072', '-nodes', '-keyout', str(root / 'synthetic.key'), '-out', str(root / 'synthetic.pem'), '-days', '32', '-subj', '/CN=SYNTHETIC-NOT-PRODUCTION'], capture_output=True)
            self.assertEqual(generated.returncode, 0)
            result = run('Require recipient-encrypted artifact retention', RECIPIENT_CERT=(root / 'synthetic.pem').read_text())
            self.assertEqual(result.returncode, 0, result.stderr)
            result = run('Seal exact Sticky evidence including failed-build diagnostics')
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/seal_ci_artifacts.py'), 'unseal', '--envelope', str(root / 'sticky-sealed/sealed.envelope'), '--receipt', str(root / 'sticky-sealed/receipt.json'), '--cert', str(root / 'synthetic.pem'), '--key', str(root / 'synthetic.key'), '--output', str(root / 'recovered')], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / 'recovered/build.log').read_bytes(), (root / 'sticky-evidence/build.log').read_bytes())

    def test_cold_gate_and_actual_environment_block(self):
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertIn('cold_qualification:', text, 'typed cold input missing')
        self.assertIn("COLD_QUALIFICATION: ${{ github.event_name == 'workflow_dispatch' && inputs.cold_qualification == true }}", text)
        import subprocess
        import textwrap
        block = text.split('- name: Validate selected build environment\n', 1)[1].split('run: |\n', 1)[1].split('\n      - ', 1)[0]
        block = textwrap.dedent(block)
        for flag, environment, expected in [('true', 'sticky', 0), ('true', 'sticky-static-builtins-experimental', 1), ('true', 'invalid', 1), ('false', 'sticky', 0), ('false', 'sticky-static-builtins-experimental', 0), ('false', 'invalid', 1), (None, 'sticky', 0), (None, 'sticky-static-builtins-experimental', 0)]:
            with self.subTest(flag=flag, environment=environment):
                env = {**os.environ, 'PIO_ENV': environment, 'BUILD_JOBS': '1'}
                env.pop('COLD_QUALIFICATION', None)
                if flag is not None:
                    env['COLD_QUALIFICATION'] = flag
                result = subprocess.run(['bash', '-c', block], env=env, capture_output=True)
                self.assertEqual(result.returncode, expected)
        for cold, jobs, expected in [('true', '2', 1), ('false', '2', 0),
                                     ('false', '3', 1), ('false', '', 1), ('false', '1;echo BAD', 1)]:
            result = subprocess.run(['bash', '-c', block], env={**os.environ,
                'PIO_ENV': 'sticky', 'COLD_QUALIFICATION': cold, 'BUILD_JOBS': jobs}, capture_output=True)
            self.assertEqual(result.returncode, expected, (cold, jobs, result.stderr))


if __name__ == '__main__':
    unittest.main()
