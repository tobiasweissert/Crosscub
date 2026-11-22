#!/usr/bin/env python3
"""Compile exact production mask/snapshot/classifier; no firmware or SDK build."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

R = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--sanitize', action='store_true')
p.add_argument('--cppcheck', type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
inputs = {}


def read(path):
    text = (R / path).read_text()
    inputs[path] = hashlib.sha256(text.encode()).hexdigest()
    return text


def block(text, marker, path, semicolon=False):
    # Selected production blocks have no brace literals. Keep original line identity.
    assert text.count(marker) == 1, marker
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    if semicolon:
        assert text[end] == ';'
        end += 1
    line = text.count('\n', 0, start) + 1
    return f'#line {line} "{path}"\n' + text[start:end] + '\n'


model_path = 'src/activities/reader/EpubReaderMenuModel.h'
source_path = 'src/activities/reader/EpubReaderMenuActivity.cpp'
model = read(model_path)
source = read(source_path)
settings = read('src/CrossPointSettings.h')
# Only the settings member's size is needed by the exact snapshot declaration.
member = re.findall(r'  char sdFontFamilyName\[\d+\] = "";', settings)
assert len(member) == 1
code = '#include <cstdint>\n#include <cstring>\n'
code += 'struct SettingsSizeSeam {\n' + member[0] + '\n} SETTINGS;\n'
code += block(model, 'enum class ReaderSettingsChangeMask :', model_path, True)
code += block(model, 'constexpr ReaderSettingsChangeMask operator|(', model_path)
code += block(model, 'constexpr bool hasReaderSettingsChange(', model_path)
code += block(source, 'struct ReaderLayoutSettingsSnapshot {', source_path, True)
code += block(source, 'ReaderSettingsChangeMask classifyReaderSettingsChange(', source_path)
production = a.output / 'production.cpp'
production.write_text(code)
# Expected numeric mask values are independent of the extracted operators.
probe = '#include "production.cpp"\n#include <cassert>\n#include <iostream>\n'
probe += r'''
int main() {
  ReaderLayoutSettingsSnapshot before{};
  unsigned cases = 0;
  for (unsigned flags = 0; flags < 8; ++flags) {
    auto after = before;
    if (flags & 1) after.textAntiAliasing = 1;
    if (flags & 2) after.orientation = 1;
    if (flags & 4) after.fontFamily = 1;
    const unsigned expected = ((flags & 1) ? 8 : 0) | ((flags & 2) ? 4 : 0) | ((flags & 4) ? 2 : 0);
    auto actual = classifyReaderSettingsChange(before, after);
    assert(static_cast<unsigned>(actual) == expected);
    assert(!hasReaderSettingsChange(actual, ReaderSettingsChangeMask::Preview));
    assert(hasReaderSettingsChange(actual, ReaderSettingsChangeMask::NonLayout) == bool(flags & 1));
    assert(hasReaderSettingsChange(actual, ReaderSettingsChangeMask::Orientation) == bool(flags & 2));
    assert(hasReaderSettingsChange(actual, ReaderSettingsChangeMask::Relayout) == bool(flags & 4));
    assert(classifyReaderSettingsChange(after, before) == actual);
    ++cases;
  }
'''
# Individually exercise every actual scalar snapshot field, not only fontFamily.
snapshot = block(source, 'struct ReaderLayoutSettingsSnapshot {', source_path, True)
fields = re.findall(r'uint8_t (\w+);', snapshot)
assert len(fields) == 21
for field in fields:
    expected = 8 if field == 'textAntiAliasing' else 4 if field == 'orientation' else 2
    probe += f'  {{ auto after = before; after.{field} = 255; assert(static_cast<unsigned>(classifyReaderSettingsChange(before, after)) == {expected}); ++cases; }}\n'
probe += r'''
  for (unsigned i = 0; i < sizeof(before.sdFontFamilyName); ++i) {
    auto after = before;
    // Nonzero prefix reaches even the final byte under the production strncmp.
    std::memset(before.sdFontFamilyName, 'a', sizeof(before.sdFontFamilyName));
    after = before;
    after.sdFontFamilyName[i] = 'b';
    assert(classifyReaderSettingsChange(before, after) == ReaderSettingsChangeMask::Relayout);
    assert(classifyReaderSettingsChange(after, before) == ReaderSettingsChangeMask::Relayout);
    ++cases;
  }
  std::cout << "PASS menu mask: " << cases << " cases (8 flag combinations, 21 scalar fields, 64 font-name bytes)\n";
}
'''
(a.output / 'probe.cpp').write_text(probe)
(a.output / 'inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
rows = []


def run(name, command):
    with (a.output / (name + '.log')).open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=60,
                                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
                                     'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
    rows.append({'name': name, 'command': command, 'exit': result.returncode})
    (a.output / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
    print(name, result.returncode, flush=True)
    return result.returncode


with open('/root/.hermes/cache/scratch/crosscub-review-probes.lock', 'a') as lock:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    command = ['g++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror']
    if a.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    command += [str(a.output / 'probe.cpp'), '-o', str(a.output / 'probe')]
    if run('compile', command):
        raise SystemExit(1)
    behavior_exit = run('behavior', [str((a.output / 'probe').resolve())])
    analyzer_exit = 0
    if a.cppcheck:
        analyzer_exit = run('analyzer', [str(a.cppcheck), '--enable=warning,style,performance,portability',
                                       '--error-exitcode=1', '--std=c++17',
                                       '--template={file}:{line}:{column}: {severity}: {message} [{id}]',
                                       str(production)])
    raise SystemExit(bool(behavior_exit or analyzer_exit))
