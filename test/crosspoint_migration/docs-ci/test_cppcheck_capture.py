"""Test the real project diagnostic wrapper without launching any analyzer."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
HELPER = ROOT/'scripts/ci_cppcheck_capture.py'

# Offline excerpts of PlatformIO Core 6.1.19 (wheel SHA256
# 4f93b00cf2759035d76ebece4840a3274096f7343ee5bba56cbc0bc3ce2eb6f5).
# Copyright (c) 2014-present PlatformIO <contact@platformio.org>
# Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0).
# Only transport, configuration, file inventory and CLI display are controlled.
from types import SimpleNamespace
click = SimpleNamespace(echo=print)
get_project_dir = os.getcwd

class DefectItem:
    SEVERITY_HIGH = 1
    SEVERITY_MEDIUM = 2
    SEVERITY_LOW = 4
    SEVERITY_LABELS = {4: "low", 2: "medium", 1: "high"}
    def __init__(  # pylint: disable=too-many-positional-arguments
        self,
        severity,
        category,
        message,
        file=None,
        line=0,
        column=0,
        id=None,
        callstack=None,
        cwe=None,
    ):
        assert severity in (self.SEVERITY_HIGH, self.SEVERITY_MEDIUM, self.SEVERITY_LOW)
        self.severity = severity
        self.category = category
        self.message = message
        self.line = int(line)
        self.column = int(column)
        self.callstack = callstack
        self.cwe = cwe
        self.id = id
        self.file = file or "unknown"
        if file.lower().startswith(get_project_dir().lower()):
            self.file = os.path.relpath(file, get_project_dir())


class CheckToolBase:  # pylint: disable=too-many-instance-attributes
    def get_defects(self):
        return self._defects
    def on_tool_output(self, line):
        line = self.tool_output_filter(line)
        if not line:
            return

        defect = self.parse_defect(line)

        if not isinstance(defect, DefectItem):
            if self.options.get("verbose"):
                click.echo(line)
            return

        if defect.severity not in self.options["severity"]:
            return

        self._defects.append(defect)
        if self._on_defect_callback:
            self._on_defect_callback(defect)
    def execute_check_cmd(self, cmd):
        result = proc.exec_command(
            cmd,
            stdout=proc.LineBufferedAsyncPipe(self.on_tool_output),
            stderr=proc.LineBufferedAsyncPipe(self.on_tool_output),
        )

        if not self.is_check_successful(result):
            click.echo(
                "\nError: Failed to execute check command! Exited with code %d."
                % result["returncode"]
            )
            if self.options.get("verbose"):
                click.echo(result["out"])
                click.echo(result["err"])
            self._bad_input = True

        return result


class CppcheckCheckTool(CheckToolBase):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._field_delimiter = "<&PIO&>"
        self._buffer = ""
        self.defect_fields = [
            "severity",
            "message",
            "file",
            "line",
            "column",
            "callstack",
            "cwe",
            "id",
        ]
    def tool_output_filter(self, line):  # pylint: disable=arguments-differ
        if (
            not self.options.get("verbose")
            and "--suppress=unmatchedSuppression:" in line
        ):
            return ""

        if any(
            msg in line
            for msg in (
                "No C or C++ source files found",
                "unrecognized command line option",
                "there was an internal error",
            )
        ):
            self._bad_input = True

        return line
    def parse_defect(self, raw_line):  # pylint: disable=arguments-differ
        if self._field_delimiter not in raw_line:
            return None

        self._buffer += raw_line
        if any(f not in self._buffer for f in self.defect_fields):
            return None

        args = {}
        for field in self._buffer.split(self._field_delimiter):
            field = field.strip().replace('"', "")
            name, value = field.split("=", 1)
            args[name] = value

        args["category"] = args["severity"]
        if args["severity"] == "error":
            args["severity"] = DefectItem.SEVERITY_HIGH
        elif args["severity"] == "warning":
            args["severity"] = DefectItem.SEVERITY_MEDIUM
        else:
            args["severity"] = DefectItem.SEVERITY_LOW

        # Skip defects found in third-party software, but keep in mind that such defects
        # might break checking process so defects from project files are not reported
        breaking_defect_ids = ("preprocessorErrorDirective", "syntaxError")
        if (
            args.get("file", "")
            .lower()
            .startswith(self.config.get("platformio", "packages_dir").lower())
        ):
            if args["id"] in breaking_defect_ids:
                if self.options.get("verbose"):
                    click.echo(
                        "Error: Found a breaking defect '%s' in %s:%s\n"
                        "Please note: check results might not be valid!\n"
                        "Try adding --skip-packages"
                        % (args.get("message"), args.get("file"), args.get("line"))
                    )
                    click.echo()
                self._bad_input = True
            self._buffer = ""
            return None

        self._buffer = ""
        return DefectItem(**args)
    @staticmethod
    def is_check_successful(cmd_result):
        # Cppcheck is configured to return '3' if a defect is found
        return cmd_result["returncode"] in (0, 3)
    def check(self, on_defect_callback=None):
        self._on_defect_callback = on_defect_callback

        project_files = self.get_project_target_files(
            self.project_dir, self.options["src_filters"]
        )
        src_files_scope = ("c", "c++")
        if not any(project_files[t] for t in src_files_scope):
            click.echo("Error: Nothing to check.")
            return True

        for scope, files in project_files.items():
            if scope not in src_files_scope:
                continue
            for src_file in files:
                cmd = self.configure_command(scope, src_file)
                if not cmd:
                    self._bad_input = True
                    continue
                if self.options.get("verbose"):
                    click.echo(" ".join(cmd))

                self.execute_check_cmd(cmd)

        self.clean_up()

        return self._bad_input


def run_policy(mode):
    import sys
    import types
    from unittest.mock import patch
    sys.path.insert(0, str(ROOT/'scripts'))
    import ci_cppcheck_capture as wrapper

    def record(category, identity):
        return '<&PIO&>'.join((f'severity={category}', 'message=PRIVATE_VALUE',
            'file=freeink-sdk/PRIVATE_PATH', 'line=1', 'column=1', 'callstack=',
            'cwe=', f'id={identity}'))

    notice = record('information', 'checkersReport')
    coverage = record('information', 'normalCheckLevelMaxBranches')
    partial = 'severity=error<&PIO&>message=PRIVATE_VALUE'
    second = {
        'partial': ([partial], 3), 'empty': ([], 3), 'partial-zero': ([partial], 0),
        'overlap': ([partial, coverage], 3),
        'ignored': ([coverage, record('error', 'other').replace('freeink-sdk', '/packages')], 3),
        'multiline': ([coverage.replace('PRIVATE_VALUE', 'PRIVATE_VALUE\nsecond line').split('\n')[0],
                       'second line' + coverage.split('PRIVATE_VALUE')[1]], 3),
        'malformed': ([coverage.replace('column=1', 'column=PRIVATE_VALUE')], 3),
        'continued': ([coverage.replace('column=1', 'column=unknown'), coverage], 0),
        'quoted': ([f'"{coverage}"'], 3),
        'id-syntax': ([record('information', 'normalCheckLevelMax"Branches')], 3),
        'severity-syntax': ([record('infor"mation', 'normalCheckLevelMaxBranches')], 3),
        'mixed': ([record(c, i) for c, i in [('information', 'other'),
            ('warning', 'checkersReport'), ('error', 'normalCheckLevelMaxBranches'),
            ('style', 'checkersReport')]], 3),
        'error': ([coverage], 17), 'bad-input': (['there was an internal error', coverage], 3),
    }.get(mode, ([coverage], 3))
    commands = [([notice], 3), second]
    def exec_command(cmd, stdout, stderr):
        lines, rc = commands[int(cmd[0])]
        for line in lines:
            stderr(line)
        return dict(returncode=rc, out='', err='')

    class Tool(CppcheckCheckTool):
        configure_command = lambda self, scope, src: [src]
        clean_up = lambda self: None
        get_project_target_files = lambda self, *args: {'c': [], 'c++': ['0', '1']}

    def core_main():
        if mode == 'early':
            print('PRIVATE_VALUE', file=sys.stderr)
            return 17
        tool = Tool()
        tool.options = dict(verbose=True, severity=[1, 2, 4], src_filters=[])
        tool.config = SimpleNamespace(get=lambda *args: '/packages')
        tool.project_dir, tool._defects, tool._bad_input = '.', [], False
        rc = tool.check()
        defects = tool.get_defects()
        result = dict(tool='cppcheck', defects=defects, succeeded=rc == 0 and not defects)
        cli.collect_component_stats(result)
        for d in defects:
            print(d.file, d.message, d.category, d.id)
        if mode == 'exception':
            raise RuntimeError('PRIVATE_VALUE')
        return 17 if mode == 'late' else int(not result['succeeded'])

    cli = types.ModuleType('platformio.check.cli')
    cli.collect_component_stats = lambda result: {}
    modules = {'platformio.check.cli': cli}
    for name, attr, value in [('check.tools.cppcheck', 'CppcheckCheckTool', Tool),
                              ('check.defect', 'DefectItem', DefectItem),
                              ('__main__', 'main', core_main)]:
        module = types.ModuleType('platformio.' + name)
        setattr(module, attr, value)
        modules[module.__name__] = module
    transport = SimpleNamespace(exec_command=exec_command, LineBufferedAsyncPipe=lambda cb: cb)
    with patch.dict(sys.modules, modules), patch.dict(globals(), proc=transport):
        return wrapper.main()


class AnalyzerCaptureTests(unittest.TestCase):
    def run_entrypoint(self, root, behavior, capture='none'):
        # Only PlatformIO is controlled: execute the actual production __main__
        # in a fresh OS process, including its real diagnostic capture callback.
        import subprocess
        import sys
        bootstrap = '''
import json, pathlib, runpy, sys, types
scripts, behavior, capture = sys.argv[1:]
sys.path.insert(0, scripts)
for name in ('platformio', 'platformio.check', 'platformio.check.tools',
             'platformio.check.tools.cppcheck', 'platformio.check.cli',
             'platformio.check.defect', 'platformio.__main__'):
    sys.modules[name] = types.ModuleType(name)
command = [sys.executable, '-c',
           'import sys; sys.stderr.write("private-diagnostic-canary\\\\n"); sys.exit(7)']
if capture == 'error':
    command = ['/nonexistent/private-diagnostic-canary']
strict_result = {'returncode': 3, 'stdout': 'unchanged strict result',
                 'stderr': 'unchanged strict error'}
class Tool:
    def check(self, on_defect_callback=None):
        return self.status
    def get_defects(self):
        return self.defects
    def on_tool_output(self, line):
        pass
    def execute_check_cmd(self, args):
        assert args == command
        if capture != 'plain':
            self.on_tool_output('id=syntaxError')
        return strict_result
sys.modules['platformio.check.tools.cppcheck'].CppcheckCheckTool = Tool
cli = sys.modules['platformio.check.cli']
cli.collect_component_stats = lambda result: tuple(result['defects'])
sys.modules['platformio.check.defect'].DefectItem = types.SimpleNamespace(SEVERITY_LOW=4)
def main():
    assert sys.argv[1:] == ['check', '--verbose', '--fail-on-defect', 'low',
                            '--fail-on-defect', 'medium', '--fail-on-defect', 'high']
    if capture != 'none':
        tool = Tool()
        output = tool.on_tool_output
        result = tool.execute_check_cmd(command)
        assert result is strict_result, 'capture replaced the strict result'
        assert tool.on_tool_output == output, 'capture did not restore output'
        pathlib.Path('observed-result.json').write_text(json.dumps(result))
    if behavior == 'raise3':
        raise SystemExit(3)
    if behavior == 'raise0':
        raise SystemExit(0)
    return None if behavior == 'None' else int(behavior)
sys.modules['platformio.__main__'].main = main
sys.argv = [str(pathlib.Path(scripts)/'ci_cppcheck_capture.py'), 'check',
            '--verbose', '--fail-on-defect', 'low', '--fail-on-defect',
            'medium', '--fail-on-defect', 'high']
runpy.run_path(sys.argv[0], run_name='__main__')
'''
        env = {'PATH': os.defpath, 'CROSSCUB_PRIVATE_EVIDENCE': str(root/'evidence'),
               'GITHUB_STEP_SUMMARY': str(root/'summary')}
        return subprocess.run([sys.executable, '-I', '-B', '-c', bootstrap,
                               str(ROOT/'scripts'), behavior, capture],
                              cwd=root, env=env, capture_output=True, timeout=10)

    def test_pinned_core_policy_under_workflow_redirection(self):
        import subprocess
        import sys
        for mode, rc, blocking, coverage, complete in [
            ('only', 0, 0, 1, True), ('mixed', 1, 4, 0, True),
            ('partial', 1, 0, 0, False), ('partial-zero', 1, 0, 0, False),
            ('empty', 1, 0, 0, False), ('overlap', 1, 1, 0, False),
            ('malformed', 1, 0, 0, False), ('error', 1, 0, 1, False),
            ('continued', 1, 0, 1, False), ('quoted', 0, 0, 1, True),
            ('id-syntax', 1, 1, 0, False), ('severity-syntax', 1, 1, 0, False),
            ('ignored', 1, 0, 1, False), ('multiline', 0, 0, 1, True),
            ('bad-input', 1, 0, 1, False), ('early', 17, None, None, None),
            ('late', 17, 0, 1, True), ('exception', 1, 0, 1, True),
        ]:
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                env = dict(PATH=os.defpath, GITHUB_STEP_SUMMARY=str(root/'summary'),
                           CROSSCUB_PRIVATE_EVIDENCE=str(root/'evidence'))
                with (root/'private.log').open('wb') as log:
                    result = subprocess.run([sys.executable, '-IB', __file__, '--policy', mode],
                        cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=10)
                raw = (root/'private.log').read_text()
                self.assertIn('PRIVATE_VALUE', raw)
                self.assertEqual(result.returncode, rc, raw)
                counts = ('counts=unavailable' if blocking is None else
                    f'blocking={blocking}; report-only information: checkersReport=1, '
                    f'normalCheckLevelMaxBranches={coverage}; baselined=0; tool_completed={complete}')
                self.assertEqual((root/'summary').read_text(),
                                 f'Cppcheck: {counts}; command_exit={rc}\n')
                if mode == 'exception':
                    self.assertIn('RuntimeError: PRIVATE_VALUE', raw)

    def test_actual_entrypoint_propagates_core_nonzero_return(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
            result = self.run_entrypoint(Path(temp), '3')
        self.assertEqual(result.returncode, 3, 'Core returned 3 but wrapper reported OS success')
        self.assertEqual(result.stdout, b'')
        self.assertEqual(result.stderr, b'')

    def test_actual_entrypoint_preserves_success_and_system_exit(self):
        for behavior, expected in (('0', 0), ('None', 0), ('raise0', 0), ('raise3', 3)):
            with self.subTest(behavior=behavior), tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
                result = self.run_entrypoint(Path(temp), behavior)
                self.assertEqual((Path(temp)/'summary').read_text(),
                    f'Cppcheck: counts=unavailable; command_exit={expected}\n')
                self.assertEqual(result.returncode, expected)
                self.assertEqual(result.stdout, b'')
                self.assertEqual(result.stderr, b'')

    def test_actual_entrypoint_preserves_capture_results_and_private_errors(self):
        import json
        import stat
        for capture in ('plain', 'syntax', 'error'):
            for behavior in ('3', 'raise3'):
                with self.subTest(capture=capture, behavior=behavior), tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
                    root = Path(temp)
                    (root/'lib/I18n').mkdir(parents=True)
                    (root/'lib/I18n/I18nKeys.h').write_bytes(b'private header fixture\n')
                    result = self.run_entrypoint(root, behavior, capture)
                    self.assertEqual(result.returncode, 3)
                    self.assertEqual(result.stdout, b'')
                    self.assertEqual(result.stderr, b'')
                    self.assertEqual(json.loads((root/'observed-result.json').read_text()),
                                     {'returncode': 3, 'stdout': 'unchanged strict result',
                                      'stderr': 'unchanged strict error'})
                    evidence = root/'evidence'
                    self.assertEqual(stat.S_IMODE(evidence.stat().st_mode), 0o700)
                    for file in evidence.iterdir():
                        self.assertEqual(stat.S_IMODE(file.stat().st_mode), 0o600)
                    receipt_path = evidence/'cppcheck-0001-receipt.json'
                    if capture == 'plain':
                        self.assertFalse(receipt_path.exists())
                        continue
                    receipt = json.loads(receipt_path.read_text())
                    self.assertEqual(receipt['original_exit'], 3)
                    self.assertFalse(receipt['publication_authorized'])
                    if capture == 'syntax':
                        self.assertEqual(receipt['preprocess_exit'], 7)
                        self.assertNotIn('preprocess_error', receipt)
                        self.assertEqual((evidence/'cppcheck-0001-preprocess.log').read_bytes(),
                                         b'private-diagnostic-canary\n')
                    else:
                        self.assertEqual(receipt['preprocess_error'], 'FileNotFoundError')
                        self.assertNotIn('preprocess_exit', receipt)

    def test_exact_command_include_list_header_and_preprocessor_retained(self):
        self.assertTrue(HELPER.exists(), 'missing cppcheck command-boundary capture')
        import sys
        sys.path.insert(0, str(ROOT/'scripts'))
        spec = importlib.util.spec_from_file_location('cpp_capture', HELPER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
            root = Path(temp)
            (root/'lib/I18n').mkdir(parents=True)
            header = root/'lib/I18n/I18nKeys.h'
            header.write_bytes(b'exact generated fixture\n')
            includes = root/'temporary-includes'
            includes.write_text('/actual/includes\n')
            driver = root/'fake analyzer.py'
            driver.write_text('import sys; assert sys.argv[-1] == "-E"; print("preprocessed diagnostic fixture"); sys.exit(1)')
            command = [sys.executable, str(driver), '--language=c++', '--std=c++2b', '--includes-file='+str(includes), 'src/main.cpp']
            class Tool:
                def on_tool_output(self, line): pass
            tool = Tool()
            def execute(tool, args):
                self.assertEqual(args, command)
                self.assertTrue((root/'evidence/cppcheck-I18nKeys.h').is_file())
                header.write_bytes(b'changed after analyzer input was read\n')
                tool.on_tool_output('id=syntaxError')
                return {'returncode': 3}
            with patch.dict(os.environ, CROSSCUB_PRIVATE_EVIDENCE=str(root/'evidence')):
                old = Path.cwd()
                try:
                    os.chdir(root)
                    result = module.capture_execute(execute, tool, command)
                finally:
                    os.chdir(old)
            self.assertEqual(result, {'returncode': 3}, 'strict analyzer exit must survive diagnostics')
            self.assertEqual((root/'evidence/cppcheck-I18nKeys.h').read_bytes(), b'exact generated fixture\n')
            self.assertEqual((root/'evidence/cppcheck-0001-I18nKeys.h').read_bytes(), b'exact generated fixture\n')
            import json
            receipt = json.loads((root/'evidence/cppcheck-0001-receipt.json').read_text())
            self.assertFalse(receipt['preprocess_header_matches_input'])
            self.assertEqual((root/'evidence/cppcheck-0001-includes-0.txt').read_bytes(), includes.read_bytes())
            self.assertEqual((root/'evidence/cppcheck-0001-preprocess.log').read_bytes(), b'preprocessed diagnostic fixture\n')

    def test_descendants_cannot_write_after_capture(self):
        import json
        import sys
        import time
        sys.path.insert(0, str(ROOT/'scripts'))
        import ci_cppcheck_capture as module
        for mode in ('timeout', 'normal', 'closed-pipes', 'overflow', 'exception'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
                root = Path(temp)
                sentinel, pidfile = root/'late-write', root/'pid'
                driver = root/'fake analyzer.py'
                child = 'import pathlib,time;time.sleep(0.65);pathlib.Path('+repr(str(sentinel))+').write_text("late")'
                body = 'import os,sys,subprocess,pathlib,time\np=subprocess.Popen([sys.executable,"-c",'+repr(child)+'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)\npathlib.Path('+repr(str(pidfile))+').write_text(str(p.pid))\n'
                if mode == 'closed-pipes': body += 'os.close(1);os.close(2)\n'
                if mode == 'overflow': body += 'os.write(1,b"x"*300000)\n'
                if mode != 'normal': body += 'time.sleep(5)\n'
                driver.write_text(body)
                import selectors
                select = selectors.DefaultSelector.select
                def fault(selector, timeout=None):
                    if mode == 'exception' and pidfile.exists():
                        raise OSError('synthetic-monitor-failure')
                    return select(selector, timeout)
                class Tool:
                    def on_tool_output(self, line): pass
                def original(tool, command):
                    tool.on_tool_output('id=syntaxError')
                    return {'returncode': 3}
                with patch.dict(os.environ, CROSSCUB_PRIVATE_EVIDENCE=str(root/'evidence')), patch.object(module, 'PROBE_TIMEOUT', 0.25, create=True), patch.object(selectors.DefaultSelector, 'select', fault):
                    result = module.capture_execute(original, Tool(), [sys.executable, str(driver)])
                self.assertEqual(result, {'returncode': 3})
                receipt = json.loads((root/'evidence/cppcheck-0001-receipt.json').read_text())
                expected_error = {'timeout': 'TimeoutExpired', 'closed-pipes': 'TimeoutExpired',
                                  'overflow': 'OutputLimitExceeded', 'exception': 'OSError'}
                if mode in expected_error:
                    self.assertEqual(receipt.get('preprocess_error'), expected_error[mode])
                status = Path('/proc')/pidfile.read_text()/'stat'
                self.assertTrue(not status.exists() or status.read_text().split()[2] == 'Z', 'live analyzer descendant at callback return')
                time.sleep(0.8)
                self.assertFalse(sentinel.exists(), 'analyzer descendant wrote after callback')
                self.assertFalse(status.exists(), 'analyzer descendant not reaped by host init')

if __name__ == '__main__':
    import sys
    if sys.argv[1:2] == ['--policy']:
        sys.exit(run_policy(sys.argv[2]))
    unittest.main()
