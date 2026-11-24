"""SYNTHETIC command producers; source/wiring evidence, never hosted CI proof."""
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
EXPERIMENT = 'sticky-static-builtins-experimental'


def workflow():
    return (ROOT/'.github/workflows/ci.yml').read_text()


def block(name):
    text = workflow().split('      - name: '+name, 1)[1].split('\n      - ', 1)[0]
    body = text.split('        run: |\n', 1)[1]
    return '\n'.join(line[10:] for line in body.splitlines() if line.startswith('          '))+'\n'


def diagnostic():
    text = block('Stage exact Sticky BIN ELF and provenance')
    source = text.split("python3 - <<'PY'\n", 1)[1].split('\nPY', 1)[0]
    tree = ast.parse(source)
    fn = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'capture_merged_initializer']
    if len(fn) != 1:
        raise AssertionError('bounded initializer diagnostic missing')
    scope = {}
    exec('import hashlib, json, os, pathlib, subprocess', scope)
    exec(compile(ast.Module(body=fn, type_ignores=[]), '<actual-private-diagnostic>', 'exec'), scope)
    return scope['capture_merged_initializer']


class ExperimentalRouting(unittest.TestCase):
    def test_experimental_actual_provenance_python_synthetic(self):
        import test_packaging
        test_packaging.Packaging().run_stage(environment=EXPERIMENT)

    def test_manual_choice_single_build_and_no_shell_expression_injection(self):
        text = workflow()
        self.assertIn('options: [sticky, sticky-static-builtins-experimental]', text)
        self.assertIn('default: sticky', text)
        self.assertIn("github.event_name == 'workflow_dispatch' && inputs.environment || 'sticky'", text)
        self.assertIn('PIO_ENV: ${{ matrix.environment }}', text)
        for run in re.findall(r'(?m)^        run: \|\n((?:          .*\n|\n)*)', text):
            self.assertNotIn('${{ inputs.', run)
            self.assertNotIn('${{ matrix.environment }}', run)
        self.assertIn('publication_authorized=False', text)
        self.assertNotIn('action-gh-release', text)
        self.assertNotIn('contents: write', text)

    def test_actual_cli_two_environments_and_invalid_rejected(self):
        with tempfile.TemporaryDirectory(dir=tempfile.gettempdir()) as tmp:
            root = Path(tmp)
            tools = root/'tools'; tools.mkdir()
            producer = '#!/usr/bin/env python3\nimport json,os,sys\nwith open(os.environ["RECORD"],"a") as f:f.write(json.dumps([os.path.basename(sys.argv[0]),*sys.argv[1:]])+"\\n")\n'
            for name in ['pio', 'git']:
                path = tools/name; path.write_text(producer); path.chmod(0o700)
            (root/'scripts').mkdir()
            (root/'sticky-evidence').mkdir()
            (root/'scripts/capture_sticky_startup_config.py').write_text(producer)
            # CLI-routing fixture only; real producer overlay/body has its own tests.
            (root/'scripts/ci_producer_capture.py').write_text(producer)
            for name in ['sticky', EXPERIMENT]:
                record = root/'record'; record.write_text('')
                env = {**os.environ, 'PATH': str(tools)+':'+os.environ['PATH'], 'PIO_ENV': name, 'RECORD': str(record), 'GITHUB_WORKSPACE': str(root), 'COLD_QUALIFICATION': 'false', 'BUILD_JOBS': '1'}
                subprocess.run(['bash', '-c', 'set -euo pipefail\n'+block('Validate selected build environment')], cwd=root, env=env, check=True)
                build = block('Build ${{ matrix.environment }} firmware')
                subprocess.run(['bash', '-c', build], cwd=root, env=env, check=True)
                pin = workflow().split('  build:', 1)[1].split('      - name: Pin nested core and verify source toolchain pins', 1)[1]
                command = re.search(r'(?m)^          pio pkg install .+$', pin)[0].strip()
                subprocess.run(['bash', '-c', command], cwd=root, env=env, check=True)
                folder = root/'.pio/build'/name; folder.mkdir(parents=True)
                (folder/'firmware.bin').write_bytes(b'SYNTHETIC PRIMARY '+name.encode())
                prefix = block('Stage exact Sticky BIN ELF and provenance').split("python3 - <<'PY'", 1)[0]
                subprocess.run(['bash', '-c', prefix], cwd=root, env=env, check=True)
                commands = [json.loads(line) for line in record.read_text().splitlines()]
                self.assertIn(['pio', 'run', '--verbose', '-j1', '-e', name], commands)
                self.assertIn(['pio', 'pkg', 'install', '-e', name], commands)
                self.assertIn(['pio', 'pkg', 'list', '-e', name], commands)
                self.assertIn(['capture_sticky_startup_config.py', '--require-producer', '--project', '.', '--env', name, '--memory-type', 'qio_opi', '--out', 'sticky-evidence'], commands)
                self.assertEqual((root/'sticky-evidence/firmware.bin').read_bytes(), (folder/'firmware.bin').read_bytes())
            for name in ['', 'sticky; touch INJECTED', '../sticky', 'sticky-debug']:
                result = subprocess.run(['bash', '-c', block('Validate selected build environment')], env={**os.environ, 'PIO_ENV': name}, cwd=root, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
            self.assertFalse((root/'INJECTED').exists())

    def test_ordinary_cli_fixture_under_ambient_cold_mode(self):
        from unittest.mock import patch
        # The hosted cold job still exercises ordinary routing as a fixture.
        # Production cold-environment rejection is covered independently.
        with patch.dict(os.environ, {'COLD_QUALIFICATION': 'true'}):
            self.test_actual_cli_two_environments_and_invalid_rejected()
            self.assertEqual(os.environ['COLD_QUALIFICATION'], 'true')

    def run_isolated_diagnostic(self, producer, watchdog=28, build_lines=None, invoked=True):
        """Actual extracted function; SYNTHETIC producers, not native SDK proof.

        The watchdog owns the whole process group even when an assertion fails.
        """
        import ctypes
        import signal
        import sys
        import time
        # Linux CI: adopt/reap orphaned producers if the watchdog kills the caller.
        libc = ctypes.CDLL(None, use_errno=True)
        previous_subreaper = ctypes.c_int()
        self.assertEqual(libc.prctl(37, ctypes.byref(previous_subreaper), 0, 0, 0), 0)
        self.assertEqual(libc.prctl(36, 1, 0, 0, 0), 0)
        self.addCleanup(lambda: libc.prctl(36, previous_subreaper.value, 0, 0, 0))
        with tempfile.TemporaryDirectory(dir=tempfile.gettempdir()) as tmp:
            root = Path(tmp); out = root/'out'; out.mkdir()
            elf = root/'firmware.elf'; elf.write_bytes(b'SYNTHETIC ELF NOT TARGET')
            compiler = root/'xtensa-esp-elf-g++'; compiler.write_text('SYNTHETIC COMPILER')
            tool = root/'xtensa-esp-elf-objdump'
            tool.write_text('#!/usr/bin/env python3\nimport os,sys,time\n'
                            'from pathlib import Path\n'
                            'Path(__file__).with_suffix(".pid").write_text(str(os.getpid()))\n'+producer)
            tool.chmod(0o700)
            (out/'build.log').write_text('\n'.join(line.format(compiler=compiler) for line in (build_lines or ['{compiler} -o .pio/build/'+EXPERIMENT+'/firmware.elf'])))
            script = ('import sys,json,hashlib; from pathlib import Path; '
                      'from test_experimental_routing import diagnostic; '
                      'root=Path(sys.argv[1]); compiler=root/"xtensa-esp-elf-g++"; '
                      'diagnostic()({str(compiler): {"sha256": hashlib.sha256(compiler.read_bytes()).hexdigest()}}, '
                      'root/"out", root/"firmware.elf"); '
                      '(root/"hash-stage").write_text("reached")')
            started = time.monotonic()
            proc = subprocess.Popen([sys.executable, '-c', script, str(root)],
                                    cwd=Path(__file__).parent, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, start_new_session=True,
                                    env={**os.environ, 'PIO_ENV': EXPERIMENT})
            try:
                try:
                    public_out, public_err = proc.communicate(timeout=watchdog)
                except subprocess.TimeoutExpired:
                    self.fail('actual diagnostic exceeded lifecycle watchdog')
                elapsed = time.monotonic()-started
                self.assertEqual(proc.returncode, 0, public_err.decode(errors='replace'))
                self.assertTrue((root/'hash-stage').exists())
                if invoked:
                    self.assertTrue(tool.with_suffix('.pid').exists(), 'expected final-link tool invocation')
                    pid = int(tool.with_suffix('.pid').read_text())
                    with self.assertRaises(ProcessLookupError):
                        os.kill(pid, 0)
                else:
                    self.assertFalse(tool.with_suffix('.pid').exists())
                receipt = json.loads((out/'merged-initializer-receipt.json').read_text())
                data = (out/'merged-initializer.txt').read_bytes()
                self.assertEqual(receipt['elf_sha256'], hashlib.sha256(elf.read_bytes()).hexdigest())
                if invoked:
                    self.assertEqual(receipt['objdump_sha256'], hashlib.sha256(tool.read_bytes()).hexdigest())
                self.assertEqual(receipt['output_sha256'], hashlib.sha256(data).hexdigest())
                self.assertLessEqual(len(data), 131072)
                print('SYNTHETIC diagnostic lifecycle elapsed=%.3fs watchdog=%ss' % (elapsed, watchdog))
                return receipt, data, public_out, public_err
            finally:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                proc.communicate(timeout=5)
                assert proc.stdout is not None and proc.stderr is not None
                proc.stdout.close(); proc.stderr.close()
                cleanup_deadline = time.monotonic()+5
                while True:
                    try:
                        child, _ = os.waitpid(-proc.pid, os.WNOHANG)
                    except ChildProcessError:
                        break
                    if child == 0:
                        self.assertLess(time.monotonic(), cleanup_deadline, 'group reap deadline')
                        time.sleep(0.01)

    def test_initializer_eof_alive_is_bounded_and_reaped(self):
        receipt, data, stdout, stderr = self.run_isolated_diagnostic(
            'if "--version" in sys.argv: print("SYNTHETIC version");sys.exit(0)\n'
            'os.close(1);os.close(2);time.sleep(120)\n')
        self.assertFalse(receipt['complete'])
        self.assertTrue(receipt['timed_out'])
        self.assertNotEqual(receipt['exit_code'], 0)
        self.assertEqual((stdout, stderr), (b'', b''))

    def test_initializer_open_pipe_timeout_is_bounded_and_reaped(self):
        receipt, data, stdout, stderr = self.run_isolated_diagnostic(
            'if "--version" in sys.argv: print("SYNTHETIC version");sys.exit(0)\n'
            'print("SYNTHETIC partial",flush=True);time.sleep(120)\n')
        self.assertFalse(receipt['complete'])
        self.assertTrue(receipt['timed_out'])
        self.assertIn(b'SYNTHETIC partial', data)
        self.assertEqual((stdout, stderr), (b'', b''))

    def test_objdump_version_stderr_is_private_in_success_and_failure(self):
        sentinel = '/PRIVATE/SYNTHETIC/version-stderr-sentinel'
        for code in [0, 7]:
            with self.subTest(exit_code=code):
                receipt, data, stdout, stderr = self.run_isolated_diagnostic(
                    'if "--version" in sys.argv:\n'
                    ' print("SYNTHETIC version");print('+repr(sentinel)+',file=sys.stderr);sys.exit('+str(code)+')\n'
                    'print("00000000 <_sub_I_65535_0$0>:\\n SYNTHETIC function")\n')
                self.assertNotIn(sentinel.encode(), stdout+stderr)
                self.assertIn(sentinel, receipt['objdump_version'])
                self.assertEqual(receipt['complete'], code == 0)
                self.assertEqual(receipt['version_status']['exit_code'], code)

    def test_objdump_version_timeout_is_private_bounded_and_reaped(self):
        receipt, data, stdout, stderr = self.run_isolated_diagnostic(
            'print("/PRIVATE/SYNTHETIC/version-timeout",file=sys.stderr,flush=True)\n'
            'time.sleep(120)\n', watchdog=18)
        self.assertFalse(receipt['complete'])
        self.assertTrue(receipt['version_status']['timed_out'])
        self.assertIn('/PRIVATE/SYNTHETIC/version-timeout', receipt['objdump_version'])
        self.assertEqual((stdout, stderr), (b'', b''))

    def test_objdump_version_overflow_is_private_bounded_and_reaped(self):
        receipt, data, stdout, stderr = self.run_isolated_diagnostic(
            'sys.stderr.write("/PRIVATE/SYNTHETIC/overflow\\n"+"v"*140000)\n')
        self.assertFalse(receipt['complete'])
        self.assertTrue(receipt['version_status']['overflow'])
        self.assertLessEqual(len(receipt['objdump_version'].encode()), 131072)
        self.assertIn('/PRIVATE/SYNTHETIC/overflow', receipt['objdump_version'])
        self.assertEqual((stdout, stderr), (b'', b''))

    def test_quiet_main_final_elf_link_selects_manifest_toolchain(self):
        receipt, data, stdout, stderr = self.run_isolated_diagnostic(
            'if "--version" in sys.argv: print("SYNTHETIC version");sys.exit(0)\n'
            'print("00000000 <_sub_I_65535_0$0>:\\n SYNTHETIC function")\n',
            build_lines=['{compiler} -o .pio/build/sticky/sdk/preliminary.elf',
                         '{compiler} -o .pio/build/sticky/firmware.elf',
                         '{compiler} -o .pio/build/'+EXPERIMENT+'/firmware.elf'])
        self.assertTrue(receipt['complete'])
        self.assertEqual(receipt['compiler_selection'], 'captured final ELF link driver')
        self.assertEqual(receipt['linked_output'], '.pio/build/'+EXPERIMENT+'/firmware.elf')
        self.assertEqual((stdout, stderr), (b'', b''))

    def test_unrelated_or_ambiguous_final_elf_links_fail_closed(self):
        final = '.pio/build/'+EXPERIMENT+'/firmware.elf'
        for lines in [
            ['{compiler} -o .pio/build/sticky/firmware.elf'],
            ['{compiler} -o .pio/build/'+EXPERIMENT+'/sdk/preliminary.elf'],
            ['{compiler} -c src/main.cpp'],
            ['{compiler} -o '+final, '{compiler} -o '+final],
            ['{compiler} -o '+final+' -o foreign.elf'],
            ['{compiler} -c src/main.cpp -o '+final],
            ['/unrecorded/xtensa-esp-elf-g++ -o '+final],
        ]:
            with self.subTest(lines=lines):
                receipt, data, stdout, stderr = self.run_isolated_diagnostic(
                    'raise AssertionError("must not invoke")\n', build_lines=lines, invoked=False)
                self.assertFalse(receipt['complete'])
                self.assertEqual(receipt['command'], [])
                self.assertEqual((stdout, stderr), (b'', b''))

    def test_bounded_initializer_diagnostic_recorded_producers(self):
        from unittest.mock import patch
        capture = diagnostic()
        with patch.dict(os.environ, {'PIO_ENV': EXPERIMENT}), tempfile.TemporaryDirectory(dir=tempfile.gettempdir()) as tmp:
            root = Path(tmp); out = root/'out'; out.mkdir()
            elf = root/'firmware.elf'; elf.write_bytes(b'SYNTHETIC ELF NOT TARGET')
            compiler = root/'xtensa-esp-elf-g++'; compiler.write_text('SYNTHETIC COMPILER')
            tool = root/'xtensa-esp-elf-objdump'
            compilers = {str(compiler): {'sha256': hashlib.sha256(compiler.read_bytes()).hexdigest(), 'version': 'SYNTHETIC'}}
            (out/'build.log').write_text(str(compiler)+' -o .pio/build/'+EXPERIMENT+'/firmware.elf\n')
            for mode in ['success', 'failed', 'missing', 'overflow']:
                tool.write_text('#!/usr/bin/env python3\nimport sys\nif "--version" in sys.argv: print("SYNTHETIC objdump");sys.exit(0)\n'+{
                    'success': 'print("00000000 <_sub_I_65535_0$0>:\\n SYNTHETIC bounded function")\n',
                    'failed': 'print("SYNTHETIC error");sys.exit(7)\n',
                    'missing': 'print("SYNTHETIC no matching symbol")\n',
                    'overflow': 'sys.stdout.write("x"*140000)\n',
                }[mode]); tool.chmod(0o700)
                capture(compilers, out, elf)
                receipt = json.loads((out/'merged-initializer-receipt.json').read_text())
                self.assertEqual(receipt['complete'], mode == 'success')
                self.assertEqual(receipt['elf_sha256'], hashlib.sha256(elf.read_bytes()).hexdigest())
                self.assertLessEqual((out/'merged-initializer.txt').stat().st_size, 131072)
                self.assertIn('--disassemble=_sub_I_65535_0$0', receipt['command'])
                self.assertEqual(receipt['objdump_sha256'], hashlib.sha256(tool.read_bytes()).hexdigest())
            tool.unlink()
            capture(compilers, out, elf)
            self.assertFalse(json.loads((out/'merged-initializer-receipt.json').read_text())['complete'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
