#!/usr/bin/env python3
"""Capture bounded SDK diagnostics into the existing recipient-sealed evidence.

No build, install, config/cache mutation, or public raw output. The board/memory
candidate is only a cross-check: retained main-object Make prerequisites must
identify one actual generated SDK header. Missing/conflicting evidence exits 1
with a private diagnostic receipt; it never infers a baseline from defaults.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import configparser
import os
import stat

# Only fixed collector-authored codes may cross the public log boundary.
# Never print exception text, configuration values, command lines or raw receipts.
PUBLIC_CONFIGURATION_ERRORS = frozenset({
    'producer-phase-incomplete', 'producer-command-conflict',
    'producer-projection-type', 'producer-projection-incomplete', 'producer-projection-conflict',
    'duplicate-config', 'duplicate-header', 'unsupported-header-grammar',
    'unsupported-header-comment-or-string', 'unsupported-header-guard',
    'header-alias-cycle-or-undefined', 'header-nonliteral', 'header-alias-depth-limit',
    'dependency-escape', 'dependency-variable', 'dependency-rule',
    'dependency-duplicate-target', 'dependency-foreign-target', 'dependency-target-missing',
    'dependency-path-traversal', 'foreign-sdk-header', 'dependency-header-missing',
    'dependency-header-ambiguous', 'producer-proof-type', 'producer-proof-incomplete',
    'producer-receipt-missing', 'dependency-header-unresolved', 'producer-header-type',
    'producer-header-conflict', 'required-config-missing', 'project-package-conflict',
    'producer-active-macro-conflict', 'ignore-notfound-enabled', 'selected-value-invalid',
    'header-config-conflict', 'selected-values-unresolved', 'actual-header-candidate-disagreement',
})


def public_configuration_error(exc):
    if (type(exc) is ValueError and len(exc.args) == 1 and
            type(exc.args[0]) is str and exc.args[0] in PUBLIC_CONFIGURATION_ERRORS):
        return exc.args[0]
    return 'configuration-incomplete'


MAX_FILE = 262144
MAX_TOTAL = 2097152
KEYS = (
    'CONFIG_SPIRAM', 'CONFIG_SPIRAM_BOOT_HW_INIT', 'CONFIG_SPIRAM_BOOT_INIT',
    'CONFIG_SPIRAM_PRE_CONFIGURE_MEMORY_PROTECTION', 'CONFIG_SPIRAM_IGNORE_NOTFOUND',
    'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY', 'CONFIG_ESP_ALLOW_BSS_SEG_EXTERNAL_MEMORY',
    'CONFIG_SPIRAM_MEMTEST', 'CONFIG_SPIRAM_USE_MALLOC', 'CONFIG_SPIRAM_USE_CAPS_ALLOC',
    'CONFIG_SPIRAM_USE_MEMMAP', 'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL',
    'CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL', 'CONFIG_SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY',
    'CONFIG_SPIRAM_FETCH_INSTRUCTIONS', 'CONFIG_SPIRAM_RODATA',
    'CONFIG_SPIRAM_MODE_OCT', 'CONFIG_SPIRAM_SPEED',
    'CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP', 'CONFIG_BT_ENABLED',
)

def validate_producer_phases(receipt, argv, target, dep, macros):
    from ci_producer_capture import MAX_MACRO_STREAM, preprocessing_base
    base = preprocessing_base(argv)
    if ('probe_error' in receipt or not re.fullmatch(r'[a-f0-9]{64}', receipt.get('compile_env_sha256', '')) or
            any(type(receipt.get(k)) is not int or receipt[k] != 0 for k in ('dependency_exit', 'macro_exit'))):
        raise ValueError('producer-phase-incomplete')
    dep_cmd = receipt.get('dependency_argv', [])
    macro_cmd = receipt.get('probe_argv', [])
    if (dep_cmd not in [base + ['-M', '-MF', str(dep), '-MT', t] for t in target] or
            not isinstance(macro_cmd, list) or macro_cmd[:-1] != base + ['-E', '-dM', '-o'] or
            not re.fullmatch(r'/dev/fd/[0-9]+', macro_cmd[-1])):
        raise ValueError('producer-command-conflict')
    projection = receipt['macro_projection']
    if not isinstance(projection, dict):
        raise ValueError('producer-projection-type')
    if (not macros.endswith('\n') or not all(line.startswith('#define CONFIG_') for line in macros.splitlines()) or
            any(type(projection.get(k)) is not int for k in ('raw_bytes', 'records', 'config_records')) or
            not 0 < macros.count('\n') == projection['config_records'] <= projection['records'] or
            not len(macros.encode()) <= projection['raw_bytes'] <= MAX_MACRO_STREAM or
            not re.fullmatch(r'[0-9a-f]{64}', projection.get('raw_sha256', ''))):
        raise ValueError('producer-projection-incomplete')
    retained = macros.encode()
    extra_records = projection['records'] - projection['config_records']
    extra_bytes = projection['raw_bytes'] - len(retained)
    # Each omitted record includes at least '#define X\n'. A mixed stream's
    # digest is producer-attested; only an all-selected digest is reproducible.
    if (not len(b'#define X\n') * extra_records <= extra_bytes <= MAX_FILE * extra_records or
            (not extra_records and projection['raw_sha256'] != hashlib.sha256(retained).hexdigest())):
        raise ValueError('producer-projection-conflict')


def parse_config(text):
    result = {}
    for line in text.splitlines():
        m = re.fullmatch(r'(CONFIG_[A-Za-z0-9_]+)=(.*)', line)
        if m:
            if m[1] in result:
                raise ValueError('duplicate-config')
            result[m[1]] = m[2]
        m = re.fullmatch(r'# (CONFIG_[A-Za-z0-9_]+) is not set', line)
        if m:
            if m[1] in result:
                raise ValueError('duplicate-config')
            result[m[1]] = 'n'
    return result


def parse_generated_header(text):
    """Only flat generated CONFIG defines, comments, pragma once, outer guard.

    This is not a preprocessor: other directives, continuations, valueless
    CONFIG defines and conditional bodies are unsupported, never guessed.
    A non-CONFIG outer guard is accepted as the conventional fresh-header form.
    """
    # Preserve quoted literals while removing comments; reject unterminated ones
    # and continuations rather than interpreting C translation phases.
    if '\\' in text:
        raise ValueError('unsupported-header-grammar')
    text = re.sub(r'"[^"\n]*"|/\*.*?\*/|//[^\n]*',
                  lambda m: m[0] if m[0].startswith('"') else ' ', text, flags=re.S)
    unquoted = re.sub(r'"[^"\n]*"', '', text)
    if '/*' in unquoted or '*/' in unquoted or '"' in unquoted:
        raise ValueError('unsupported-header-comment-or-string')
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    if lines and lines[0] == '#pragma once':
        lines.pop(0)
    if lines and lines[0].startswith('#ifndef '):
        guard = lines.pop(0)[len('#ifndef '):]
        if (not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', guard) or guard.startswith('CONFIG_') or
                len(lines) < 2 or lines.pop(0) != '#define ' + guard or lines.pop() != '#endif'):
            raise ValueError('unsupported-header-guard')
    definitions = {}
    for line in lines:
        match = re.fullmatch(r'#define[ \t]+(CONFIG_[A-Za-z0-9_]+)[ \t]+(.+)', line)
        if not match:
            raise ValueError('unsupported-header-grammar')
        if match[1] in definitions:
            raise ValueError('duplicate-header')
        definitions[match[1]] = match[2].strip()
    return definitions


def resolve_header_value(definitions, key):
    """At most 32 CONFIG identifier edges to a literal; no C evaluation."""
    seen = set()
    if key not in definitions:
        return None
    for _ in range(33):
        if key in seen or key not in definitions:
            raise ValueError('header-alias-cycle-or-undefined')
        seen.add(key)
        value = definitions[key]
        if re.fullmatch(r'CONFIG_[A-Za-z0-9_]+', value):
            key = value
        elif re.fullmatch(r'-?[0-9]+|0[xX][0-9A-Fa-f]+|"[^"\n]*"', value):
            return value
        else:
            raise ValueError('header-nonliteral')
    raise ValueError('header-alias-depth-limit')


def qualify_invisible_ignore(text, values, definitions, source):
    """One known Kconfig dependency, not a Kconfig evaluator or default guess."""
    key = 'CONFIG_SPIRAM_IGNORE_NOTFOUND'
    if key in values or key in definitions or text is None:
        return {}
    dependency = ('SPIRAM_BOOT_HW_INIT && !SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY '
                  '&& !SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY')
    # Require the actual top-level definition and its entire property grammar.
    # Help prose is non-semantic; additional properties/definitions fail closed.
    if len(re.findall(r'^\s*(?:menuconfig|config) SPIRAM_IGNORE_NOTFOUND\s*$', text, re.M)) != 1:
        return {}
    match = re.search(r'^config SPIRAM_IGNORE_NOTFOUND\n(.*?)(?=^\S|\Z)', text, re.M | re.S)
    if not match:
        return {}
    body = match[1].splitlines()
    expected = ['    bool "Ignore PSRAM when not found"', '    default "n"',
                '    depends on ' + dependency, '    help']
    if body[:4] != expected or any(line.strip() and not line.startswith('        ') for line in body[4:]):
        return {}
    terms = {'CONFIG_SPIRAM_BOOT_HW_INIT': 'n',
             'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY': 'y',
             'CONFIG_SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY': 'y'}
    # A false conjunct must itself be explicit and header-consistent. All other
    # selected values still undergo the ordinary strict validation below.
    false_terms = {k: v for k, v in terms.items() if values.get(k) == v and
                   definitions.get(k) == (None if v == 'n' else '1')}
    if not false_terms:
        return {}
    return {key: {'raw_config_state': 'ABSENT_UNRESOLVED', 'raw_header_state': 'UNDEFINED',
                  'certainty': 'source-proven-dependency-inactive',
                  'source': source['source'], 'source_sha256': source['sha256'],
                  'definition': 'config SPIRAM_IGNORE_NOTFOUND\n' + '\n'.join(expected),
                  'dependency': dependency, 'false_dependency_terms': false_terms}}


def parse_dependencies(text, target):
    """Read GCC Make prerequisites, not shell syntax; never expand Make variables."""
    text = text.replace('\\\r\n', ' ').replace('\\\n', ' ')
    result = None
    for line in text.splitlines():
        words, word, sides = [], '', []
        i = 0
        while i < len(line):
            c = line[i]
            if c == '\\':
                i += 1
                if i == len(line):
                    raise ValueError('dependency-escape')
                word += line[i]
            elif c == '$':
                i += 1
                if i == len(line) or line[i] != '$':
                    raise ValueError('dependency-variable')
                word += '$'
            elif c == '#':
                break
            elif c == ':' or c.isspace():
                if word:
                    words.append(word)
                    word = ''
                if c == ':':
                    sides.append(words)
                    words = []
            else:
                word += c
            i += 1
        if word:
            words.append(word)
        if not sides and not words:
            continue
        if len(sides) != 1:
            raise ValueError('dependency-rule')
        if target in sides[0]:
            if result is not None:
                raise ValueError('dependency-duplicate-target')
            result = words
        elif words:
            raise ValueError('dependency-foreign-target')
    if result is None:
        raise ValueError('dependency-target-missing')
    return result


def open_directory(path):
    """No symlinks in any ancestor; keep a directory handle for exclusive writes."""
    fd = os.open('/', os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in Path(os.path.abspath(path)).parts[1:]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            os.close(fd)
            fd = child
        return fd
    except BaseException:
        os.close(fd)
        raise


def read_regular(path):
    fd = open_directory(path.parent)
    try:
        child = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=fd)
        with os.fdopen(child, 'rb') as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_FILE:
                raise ValueError('file-limit-or-type')
            data = stream.read(MAX_FILE + 1)
            if len(data) > MAX_FILE:
                raise ValueError('file-limit')
            return data
    finally:
        os.close(fd)


def package_libs():
    return Path.home() / '.platformio/packages/framework-arduinoespressif32-libs/esp32s3'


def select_dependency_header(paths, project, build, libs):
    selected = set()
    for path in map(Path, paths):
        if path.name != 'sdkconfig.h':
            continue
        if '..' in path.parts:
            raise ValueError('dependency-path-traversal')
        header = project / path
        if header != build / 'config/sdkconfig.h':
            relative = header.relative_to(libs) if header.is_relative_to(libs) else None
            if (relative is None or len(relative.parts) != 3 or
                    not re.fullmatch(r'[A-Za-z0-9_-]+', relative.parts[0]) or
                    relative.parts[1:] != ('include', 'sdkconfig.h')):
                raise ValueError('foreign-sdk-header')
        selected.add(header)
    if not selected:
        raise ValueError('dependency-header-missing')
    if len(selected) != 1:
        raise ValueError('dependency-header-ambiguous')
    return selected.pop()


def capture(project, environment, memory_type, out, *, require_producer=True):
    project = Path(os.path.abspath(project))
    out = Path(os.path.abspath(out))
    libs = package_libs()
    platform = Path.home() / '.platformio/platforms/espressif32'
    idf = Path.home() / '.platformio/packages/framework-espidf'
    build = project / '.pio/build' / environment
    report = {'environment': environment, 'memory_type_candidate': memory_type, 'files': {},
              'diagnostics': [], 'actual_resolved_config_captured': False, 'publication_authorized': False}
    total = 0
    parent_fd = open_directory(out.parent)
    try:
        try:
            os.mkdir(out.name, mode=0o700, dir_fd=parent_fd)
        except FileExistsError:
            pass
        fd = os.open(out.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent_fd)
    finally:
        os.close(parent_fd)
    try:
        def write(name, data):
            nonlocal total
            if len(data) > MAX_FILE or total + len(data) > MAX_TOTAL:
                raise ValueError('output-limit')
            handle = os.open('startup-' + name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=fd)
            with os.fdopen(handle, 'wb') as stream:
                stream.write(data)
            if read_regular(out / ('startup-' + name)) != data:
                raise ValueError('hash-verification')
            total += len(data)

        def take(name, path):
            entry = {'source': str(path), 'present': False}
            report['files'][name] = entry
            try:
                data = read_regular(path)
                # Reserve room for a bounded diagnostic receipt.
                if total + len(data) > MAX_TOTAL - MAX_FILE:
                    raise ValueError('total-limit')
                write(name, data)
                entry.update(present=True, size=len(data), sha256=hashlib.sha256(data).hexdigest())
                return data.decode('utf-8')
            except (OSError, ValueError, UnicodeError) as exc:
                entry['error'] = type(exc).__name__
                if not isinstance(exc, FileNotFoundError):
                    report['diagnostics'].append('file-rejected:' + name)
                return None

        project_config = take('project-resolved.sdkconfig', project / ('sdkconfig.' + environment))
        package_config = take('package-resolved.sdkconfig', libs / 'sdkconfig')
        ini = take('platformio.ini', project / 'platformio.ini')
        installed = take('platform.json', platform / 'platform.json')
        for name, path in {
            'project-defaults.sdkconfig': project / 'sdkconfig.defaults',
            'package-original.sdkconfig': libs / 'sdkconfig.orig',
            'package-request.txt': libs / 'sdkconfig.crosspoint',
            'package-memory.ld': libs / 'ld/memory.ld', 'package-sections.ld': libs / 'ld/sections.ld',
            'build-sdkconfig.h': build / 'config/sdkconfig.h',
            'build-sdkconfig.json': build / 'config/sdkconfig.json',
            'build-memory.ld': build / 'memory.ld', 'build-sections.ld': build / 'sections.ld',
        }.items():
            take(name, path)
        kconfig_common = None
        for source in (
            'components/esp_psram/Kconfig.spiram.common', 'components/esp_psram/esp32s3/Kconfig.spiram',
            'components/esp_system/port/cpu_start.c', 'components/esp_psram/system_layer/esp_psram.c',
            'components/esp_system/startup.c', 'components/lwip/linker.lf', 'components/esp_wifi/linker.lf',
            'components/bt/linker_common.lf', 'components/bt/controller/linker_rw_bt_controller.lf',
            'components/bt/controller/linker_esp_ble_controller.lf',
        ):
            captured = take('idf-' + source.replace('/', '__'), idf / source)
            if source == 'components/esp_psram/Kconfig.spiram.common':
                kconfig_common = captured
        for source in ('espidf.py', 'arduino.py', 'component_manager.py'):
            take('builder-' + source, platform / 'builder/frameworks' / source)
        try:
            values = parse_config(project_config or '')
        except ValueError:
            values = {}
            report['diagnostics'].append('duplicate-config')
        report['selected_resolved_values'] = {key: values.get(key, 'ABSENT_UNRESOLVED') for key in KEYS}
        report['all_spiram_resolved_values'] = {k: v for k, v in values.items() if k.startswith('CONFIG_SPIRAM')}
        # Raw compiler evidence collection is independent of candidate metadata.
        dependencies = {name: take(name, build / 'src' / name)
                        for name in ('main.cpp.d', 'main.cpp.o.d')}
        try:
            config = configparser.ConfigParser(interpolation=None)
            config.read_string(ini or '')
            # This baseline workflow has no local overlay. Never silently ignore
            # one or expand arbitrary globs/paths for metadata collection.
            extras = config.get('platformio', 'extra_configs', fallback='').strip()
            if extras:
                if extras != 'platformio.local.ini':
                    raise ValueError('extra-config-unresolved')
                overlay = project / extras
                if overlay.exists() or overlay.is_symlink():
                    take('platformio.local.ini', overlay)
                    raise ValueError('extra-config-unresolved')

            def option(section, key, seen=()):
                if section in seen:
                    report['diagnostics'].append('inheritance-cycle')
                    raise ValueError('inheritance-cycle')
                # Generated baseline metadata needs only shallow inheritance;
                # unsupported depth is diagnostic, not recursive interpretation.
                if len(seen) >= 64:
                    report['diagnostics'].append('inheritance-depth-limit')
                    raise ValueError('inheritance-depth-limit')
                if config.has_option(section, key):
                    return config[section][key].strip()
                for parent in config.get(section, 'extends', fallback='').split(','):
                    if parent.strip():
                        value = option(parent.strip(), key, seen + (section,))
                        if value is not None:
                            return value
                return None

            board = option('env:' + environment, 'board')
            if not board or not re.fullmatch(r'[A-Za-z0-9_-]+', board):
                raise ValueError('board-unresolved')
            board_text = take('board.json', platform / 'boards' / (board + '.json'))
            board_memory = json.loads(board_text)['build']['arduino']['memory_type']
            report.update(board=board, board_memory_type_candidate=board_memory)
            if board_memory != memory_type:
                raise ValueError('board-candidate-disagreement')
            pin = 'https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip'
            if option('env:' + environment, 'platform') != pin:
                raise ValueError('platform-unresolved')
            installed_manifest = json.loads(installed or '{}')
            version = installed_manifest.get('version') if isinstance(installed_manifest, dict) else None
            if not isinstance(version, str) or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
                raise ValueError('platform-version-unresolved')
            if tuple(map(int, version.split('.'))) != (55, 3, 311):
                raise ValueError('platform-version-disagreement')
        except (ValueError, TypeError, KeyError, configparser.Error):
            report['diagnostics'].append('candidate-metadata-incomplete')
        producer = None
        try:
            producer_path = out / 'producer-main-receipt.json'
            if producer_path.exists() or producer_path.is_symlink():
                from ci_producer_capture import MAX_DIAGNOSTIC
                producer = json.loads(take('producer-main-receipt.json', producer_path) or '{}')
                if not isinstance(producer, dict):
                    raise ValueError('producer-proof-type')
                proof = take('producer-main.d', out / 'producer-main.d')
                macros = take('producer-main.macros', out / 'producer-main.macros')
                frozen = take('producer-application-sdkconfig.h', out / 'producer-application-sdkconfig.h')
                argv = producer.get('compile_argv', [])
                if (producer.get('boundary') != 'inner-application-main' or producer.get('environment') != environment or
                        type(producer.get('probe_exit')) is not int or producer['probe_exit'] != 0 or
                        not isinstance(argv, list) or not all(isinstance(a, str) for a in argv) or '-c' not in argv or
                        not any(Path(os.path.abspath(project / a)) == project / 'src/main.cpp' for a in argv if not a.startswith('-')) or
                        proof is None or macros is None or frozen is None or
                        report['files']['producer-main.d']['size'] >= MAX_DIAGNOSTIC or
                        report['files']['producer-main.macros']['size'] >= MAX_DIAGNOSTIC or
                        hashlib.sha256(proof.encode()).hexdigest() != producer.get('producer-main.d_sha256') or
                        hashlib.sha256(macros.encode()).hexdigest() != producer.get('producer-main.macros_sha256')):
                    raise ValueError('producer-proof-incomplete')
                object_path = build / 'src/main.cpp.o'
                validate_producer_phases(producer, argv, (str(object_path), str(object_path.relative_to(project))),
                                         out/'producer-main.d', macros)
                dependencies = {'producer-main.d': proof}
                report['dependency_selection'] = 'inner-main-exact-command-system-inclusive-preprocessing'
            elif require_producer:
                raise ValueError('producer-receipt-missing')
            headers = set()
            count = 0
            for name, dependency in dependencies.items():
                if dependency is None:
                    continue
                count += 1
                target = str((build / 'src/main.cpp.o').relative_to(project))
                try:
                    paths = parse_dependencies(dependency, target)
                except ValueError:
                    paths = parse_dependencies(dependency, str(build / 'src/main.cpp.o'))
                headers.add(select_dependency_header(paths, project, build, libs))
            if not count or len(headers) != 1:
                raise ValueError('dependency-header-unresolved')
            header = headers.pop()
            relative = header.relative_to(libs) if header.is_relative_to(libs) else None
            report['actual_header'] = str(header)
            header_text = take('application-sdkconfig.h', header)
            if producer is not None:
                selected_header = producer['files']['producer-application-sdkconfig.h']
                if not isinstance(selected_header, dict):
                    raise ValueError('producer-header-type')
                if (header_text != frozen or not selected_header.get('present') or
                        selected_header.get('source') != str(header) or
                        selected_header.get('sha256') != hashlib.sha256(frozen.encode()).hexdigest()):
                    raise ValueError('producer-header-conflict')
            if header_text is None or project_config is None or package_config is None:
                raise ValueError('required-config-missing')
            report['project_package_byte_equal'] = project_config == package_config
            if project_config != package_config:
                raise ValueError('project-package-conflict')
            raw_definitions = parse_generated_header(header_text)
            definitions = {key: resolve_header_value(raw_definitions, key) for key in raw_definitions}
            if producer is not None:
                macro_definitions = parse_generated_header(macros)
                active = {key: resolve_header_value(macro_definitions, key) for key in macro_definitions}
                keys = {key for key in definitions.keys() | active.keys() if key.startswith('CONFIG_SPIRAM') or key in KEYS}
                if any(active.get(key) != definitions.get(key) for key in keys):
                    raise ValueError('producer-active-macro-conflict')
            report['header_identifier_aliases'] = {k: v for k, v in raw_definitions.items()
                                                   if re.fullmatch(r'CONFIG_[A-Za-z0-9_]+', v)}
            qualified = qualify_invisible_ignore(
                kconfig_common, values, definitions,
                report['files']['idf-components__esp_psram__Kconfig.spiram.common'])
            report['qualified_dependency_invisible'] = qualified
            if values.get('CONFIG_SPIRAM_IGNORE_NOTFOUND') == 'y':
                raise ValueError('ignore-notfound-enabled')
            mismatches = []
            relevant = ({k for k in values.keys() | definitions.keys()
                         if k.startswith('CONFIG_SPIRAM') or k in KEYS} |
                        set(report['header_identifier_aliases']) |
                        set(report['header_identifier_aliases'].values()))
            numeric = {'CONFIG_SPIRAM_SPEED', 'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL',
                       'CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL'}
            for key in KEYS:
                if key in qualified:
                    continue
                value = values.get(key, '')
                if (key in numeric and not re.fullmatch(r'[0-9]+', value)) or (key not in numeric and value not in ('y', 'n')):
                    raise ValueError('selected-value-invalid')
            for key in sorted(relevant):
                value = values.get(key)
                expected = None if value == 'n' else '1' if value == 'y' else value
                if value is None or definitions.get(key) != expected:
                    mismatches.append(key)
            report['header_mismatches'] = mismatches
            if mismatches:
                raise ValueError('header-config-conflict')
            if any(key not in values and key not in qualified for key in KEYS):
                raise ValueError('selected-values-unresolved')
            if relative is not None and relative.parts[0] != memory_type:
                raise ValueError('actual-header-candidate-disagreement')
            report['actual_resolved_config_captured'] = not report['diagnostics']
        except (ValueError, TypeError, KeyError, configparser.Error) as exc:
            # Preserve only a closed collector-authored reason, never exception text.
            report['configuration_error'] = public_configuration_error(exc)
            report['diagnostics'].append('configuration-incomplete')
        report['captured_bytes'] = total
        # Count-bound derived detail before serialization; a small receipt must
        # survive even when valid bounded raw configs expand greatly as JSON.
        receipt = None
        if len(report['all_spiram_resolved_values']) <= 1024:
            candidate = (json.dumps(report, indent=2) + '\n').encode()
            if len(candidate) <= MAX_FILE:
                receipt = candidate
        if receipt is None:
            report = {
                'actual_resolved_config_captured': False, 'publication_authorized': False,
                'diagnostics': ['receipt-detail-limit'], 'captured_bytes': total,
                'raw_config_references': ['startup-project-resolved.sdkconfig',
                                          'startup-package-resolved.sdkconfig'],
                'files': {name: {k: entry[k] for k in ('present', 'size', 'sha256')}
                          for name, entry in report['files'].items() if entry['present']},
            }
            receipt = (json.dumps(report, separators=(',', ':')) + '\n').encode()
        write('config-manifest.json', receipt)
        return report
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--env', required=True)
    parser.add_argument('--memory-type', required=True)
    parser.add_argument('--out', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--require-producer', dest='require_producer', action='store_true', default=True)
    mode.add_argument('--legacy-dependencies', dest='require_producer', action='store_false',
                      help='allow ordinary dependency evidence only when no producer receipt exists')
    args = parser.parse_args()
    if not all(re.fullmatch(r'[A-Za-z0-9_-]+', v) for v in (args.env, args.memory_type)):
        parser.error('invalid environment or memory type')
    try:
        report = capture(args.project, args.env, args.memory_type, args.out, require_producer=args.require_producer)
    except (OSError, ValueError):
        print(json.dumps({'actual_resolved_config_captured': False, 'capture_error': True}))
        return 1
    status = {'actual_resolved_config_captured': report['actual_resolved_config_captured'],
              'captured_bytes': report['captured_bytes'],
              'captured_files': sum(f['present'] for f in report['files'].values())}
    if not report['actual_resolved_config_captured']:
        status['configuration_error'] = public_configuration_error(
            ValueError(report.get('configuration_error')))
    print(json.dumps(status))
    return 0 if report['actual_resolved_config_captured'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

