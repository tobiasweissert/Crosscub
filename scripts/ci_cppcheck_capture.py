#!/usr/bin/env python3
"""Run PIO check with the owner reporting policy and private input capture."""
import os
from pathlib import Path
from threading import Lock
import sys


PROBE_TIMEOUT = 60
ROOT = Path(__file__).resolve().parents[1]


def load_baseline(path):
    """Reject malformed policy rather than silently broadening debt acceptance."""
    import json
    import re
    document = json.loads(path.read_text())
    if not isinstance(document, dict) or document.get('schema') != 1 or not isinstance(document.get('findings'), list):
        raise ValueError('invalid analyzer baseline')
    rows = document['findings']
    seen = set()
    for row in rows:
        if (not isinstance(row, list) or len(row) != 6
                or not isinstance(row[0], str) or not row[0]
                or Path(row[0]).is_absolute() or '..' in Path(row[0]).parts
                or Path(row[0]).as_posix() != row[0]
                or type(row[1]) is not int or row[1] <= 0
                or not isinstance(row[2], str) or not isinstance(row[3], str)
                or not row[3] or type(row[4]) is not int or row[4] not in (2, 4)
                or not isinstance(row[5], str) or not re.fullmatch('[0-9a-f]{64}', row[5])):
            raise ValueError('invalid analyzer baseline finding')
        key = tuple(row[:5])
        if key in seen:
            raise ValueError('duplicate analyzer baseline finding')
        seen.add(key)
    return rows


def is_baselined(defect, baseline, root=ROOT):
    """Accept only an exact authenticated parser tuple on unchanged reviewed source."""
    import hashlib
    if not (getattr(defect, '_crosscub_raw_verified', False)
            and getattr(defect, '_crosscub_location_verified', False)):
        return False
    path = Path(defect.file)
    if path.is_absolute() or '..' in path.parts or path.as_posix() != defect.file:
        return False
    key = [defect.file, defect.line, defect.category, defect.id, defect.severity]
    candidates = [row for row in baseline if row[:5] == key]
    if not candidates:
        return False
    try:
        source = root/path
        source.resolve().relative_to(root.resolve())
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
    except (OSError, ValueError):
        return False
    return any(row[5] == digest for row in candidates)



def capture_execute(original, tool, command):
    from ci_producer_capture import output_root, retain, save_receipt, bounded_diagnostic
    out = output_root()
    if not (out/'cppcheck-generated-header-receipt.json').exists():
        files = {}
        retain(out, 'cppcheck-I18nKeys.h', Path.cwd()/'lib/I18n/I18nKeys.h', files)
        save_receipt(out, 'cppcheck-generated-header-receipt.json', {'files': files, 'publication_authorized': False})
    from capture_sticky_startup_config import read_regular
    header_path = Path.cwd()/'lib/I18n/I18nKeys.h'
    header_data = None
    header_error = None
    try:
        header_data = read_regular(header_path)
    except (OSError, ValueError) as exc:
        header_error = type(exc).__name__
    saw_syntax = []
    output = tool.on_tool_output
    def observe(line):
        if 'id=syntaxError' in line:
            saw_syntax.append(True)
        return output(line)
    tool.on_tool_output = observe
    try:
        result = original(tool, command)
    finally:
        tool.on_tool_output = output
    if not saw_syntax:
        return result
    index = getattr(tool, '_crosscub_syntax_count', 0) + 1
    tool._crosscub_syntax_count = index
    # Bound extra analyzer work; original strict check runs unchanged for all TUs.
    if index > 3:
        return result
    prefix = 'cppcheck-%04d' % index
    receipt = {'command': command, 'original_exit': result['returncode'], 'files': {}, 'publication_authorized': False}
    if header_data is not None:
        import hashlib
        name = prefix+'-I18nKeys.h'
        with (out/name).open('xb') as stream:
            os.chmod(out/name, 0o600)
            stream.write(header_data)
        receipt['files'][name] = {'source': str(header_path), 'present': True, 'sha256': hashlib.sha256(header_data).hexdigest(), 'boundary': 'before-exact-analyzer-command'}
        try:
            receipt['preprocess_header_matches_input'] = read_regular(header_path) == header_data
        except (OSError, ValueError):
            receipt['preprocess_header_matches_input'] = False
    else:
        receipt['header_error'] = header_error
    for i, arg in enumerate(a for a in command if a.startswith('--includes-file=')):
        retain(out, prefix+'-includes-'+str(i)+'.txt', Path(arg.split('=', 1)[1].strip('"')), receipt['files'])
    # Core 6.1.19 proc.exec_command passes argv directly to subprocess.Popen.
    # Preserve even its quoted template/source tokens exactly; append only -E.
    args = list(command) + ['-E']
    receipt['preprocess_command'] = args
    try:
        receipt['preprocess_exit'] = bounded_diagnostic(args, out/(prefix+'-preprocess.log'), timeout=PROBE_TIMEOUT)
    except Exception as exc:
        # Only diagnostic errors are contained; original strict result survives.
        receipt['preprocess_error'] = type(exc).__name__
    save_receipt(out, prefix+'-receipt.json', receipt)
    return result


def raw_policy_fields(record, delimiter, keys):
    # The template can quote the whole record, never its individual keys.
    if record.startswith('"') and record.endswith('"'):
        record = record[1:-1]
    fields = record.split(delimiter, len(keys))
    if len(fields) != len(keys):
        return None
    for key, field in zip(keys, fields):
        if not field.startswith(key + '='):
            return None
    return fields[0][len('severity='):], fields[-1][len('id='):]


def checked_command(original, tool, command):
    # Core feeds one shared parser from two pipe-reader threads. A callback
    # exception must fail the command without terminating either reader.
    output = tool.on_tool_output
    lock = Lock()
    valid = not tool._buffer
    before = len(tool.get_defects())
    delimiter = tool._field_delimiter

    def observe(line):
        nonlocal valid
        with lock:
            admitted = len(tool.get_defects())
            pending = tool._buffer
            start = line.lstrip().lstrip('"').startswith('severity=')
            if start and (pending or delimiter not in line):
                valid = False
            try:
                templated = delimiter in line
                raw = (raw_policy_fields(pending + line, delimiter, tool.defect_fields)
                       if templated else None)
                output(line)
                if templated and not tool._buffer:
                    if len(tool.get_defects()) != admitted + 1:
                        valid = False
                        return
                    defect = tool.get_defects()[-1]
                    if raw == (defect.category, defect.id):
                        defect._crosscub_raw_verified = True
                        record = (pending + line).strip().strip(chr(34))
                        fields = dict(field.split('=', 1) for field in record.split(delimiter))
                        raw_path = Path(fields['file'])
                        parsed_path = Path(defect.file)
                        defect._crosscub_location_verified = (
                            raw_path.resolve() == parsed_path.resolve() and
                            fields['line'] == str(defect.line))
                    else:
                        valid = False
            except BaseException:
                # Keep draining; never join a later line to failed parser state.
                valid = False
                tool._buffer = ''

    tool.on_tool_output = observe
    try:
        result = original(tool, command)
    finally:
        tool.on_tool_output = output
    rc = result['returncode']
    if not (valid and not tool._buffer and
            (rc == 0 or (rc == 3 and len(tool.get_defects()) > before))):
        tool._crosscub_complete = False
    return result


def run_core(totals):
    baseline = load_baseline(ROOT/'scripts/cppcheck-baseline.json')
    from importlib import import_module
    from unittest.mock import patch
    from platformio.check.defect import DefectItem
    from platformio.check.tools.cppcheck import CppcheckCheckTool
    from platformio.__main__ import main as pio_main
    original = CppcheckCheckTool.execute_check_cmd
    def execute(tool, command):
        if hasattr(tool, '_crosscub_complete'):
            def observed(tool, cmd):
                return checked_command(original, tool, cmd)
            return capture_execute(observed, tool, command)
        return capture_execute(original, tool, command)
    # Core 6.1.19 folds check()'s bad-input flag and --fail-on-defect into
    # one boolean *before* collecting stats. Keep the independent tool status
    # bound to its exact defect list; never reinterpret a CLI/process exit.
    cli = import_module('platformio.check.cli')
    original_check = CppcheckCheckTool.check
    original_stats = cli.collect_component_stats
    pending = None

    def check(tool, *args, **kwargs):
        nonlocal pending
        tool._crosscub_complete = True
        status = original_check(tool, *args, **kwargs)
        pending = (tool.get_defects(), status is False and tool._crosscub_complete)
        return status

    def stats(result):
        nonlocal pending
        component_stats = original_stats(result)
        if result['tool'] != 'cppcheck':
            return component_stats
        defects = result['defects']
        if pending is None or defects is not pending[0]:
            raise RuntimeError('unmatched cppcheck result')
        completed = pending[1]
        pending = None
        notices = {'checkersReport': 0, 'normalCheckLevelMaxBranches': 0, 'baselined': 0}
        blocking = 0
        for defect in defects:
            if (getattr(defect, '_crosscub_raw_verified', False)
                    and defect.category == 'information' and defect.severity == DefectItem.SEVERITY_LOW
                    and defect.id in notices):
                notices[defect.id] += 1
            elif is_baselined(defect, baseline):
                notices['baselined'] += 1
            else:
                blocking += 1
        if blocking or not completed:
            result['succeeded'] = False
        elif defects and result['succeeded'] is False:
            result['succeeded'] = True

        totals.append((blocking, notices, completed))
        return component_stats

    with patch.object(CppcheckCheckTool, 'execute_check_cmd', execute), \
            patch.object(CppcheckCheckTool, 'check', check), \
            patch.object(cli, 'collect_component_stats', stats):
        return pio_main()


def main():
    totals = []
    exit_code = 1  # uncaught exception; never serialize its type or value
    try:
        result = run_core(totals)
        exit_code = process_status(result)
        return result
    except SystemExit as exc:
        exit_code = process_status(exc.code)
        raise
    except KeyboardInterrupt:
        exit_code = 130
        raise
    finally:
        if summary := os.environ.get('GITHUB_STEP_SUMMARY'):
            counts = 'counts=unavailable'
            if totals:
                blocking = sum(b for b, _, _ in totals)
                counts = (f'blocking={blocking}; report-only information: ' + ', '.join(
                    f'{key}={sum(n[key] for _, n, _ in totals)}'
                    for key in ('checkersReport', 'normalCheckLevelMaxBranches')) +
                    f'; baselined={sum(n["baselined"] for _, n, _ in totals)}' +
                    f'; tool_completed={all(done for _, _, done in totals)}')
            try:
                with open(summary, 'a', encoding='utf-8') as stream:
                    print(f'Cppcheck: {counts}; command_exit={exit_code}', file=stream)
            except OSError:
                # A reporting failure must not replace Core's return/exception.
                print('Cppcheck: summary unavailable', file=sys.stderr)


def process_status(value):
    if value is None:
        return 0
    return int(value) % 256 if isinstance(value, int) else 1


if __name__ == '__main__':
    sys.exit(main())
