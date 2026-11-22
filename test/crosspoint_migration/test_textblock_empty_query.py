#!/usr/bin/env python3
"""Focused complete-production TextBlock TU probe; existing HAL/renderer doubles.

Run directly with --evidence PATH [--sanitize]. Not registered in the shared runner.
Reader qualification is the exact getBlock condition only, not its activity TU.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

R = Path(__file__).resolve().parents[2]
P = argparse.ArgumentParser(description=__doc__)
P.add_argument('--evidence', required=True, type=Path)
P.add_argument('--sanitize', action='store_true')
A = P.parse_args()
O = A.evidence.resolve()
O.mkdir(parents=True, exist_ok=True)
reader = (R / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
condition = next(line.strip() for line in reader.splitlines()
                 if 'if (firstLine->getBlock() && !firstLine->getBlock()->' in line)
probe = r'''
#include "Epub/Page.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <type_traits>
// Link seam only: fail production nothrow array allocation twice (initial + retry).
static unsigned failArrays = 0;
extern "C" void* __real__ZnamRKSt9nothrow_t(std::size_t, const std::nothrow_t&) noexcept;
extern "C" void* __wrap__ZnamRKSt9nothrow_t(std::size_t size, const std::nothrow_t& tag) noexcept {
  if (failArrays) { --failArrays; return nullptr; }
  return __real__ZnamRKSt9nothrow_t(size, tag);
}
static_assert(std::is_same_v<decltype(&TextBlock::empty), bool (TextBlock::*)() const>);
static void check(TextBlock& block, bool empty, bool valid) {
  const TextBlock& view = block;
  Block& base = block;
  bool (Block::*dispatch)() = &Block::isEmpty;
  for (int i = 0; i < 100; ++i) {
    assert(block.isEmpty() == empty);
    assert(view.empty() == empty);
    assert(block.empty() == empty);
    assert((base.*dispatch)() == empty);
    assert(base.getType() == TEXT_BLOCK);
    assert(view.valid() == valid);
    assert((view.wordCount() == 0) == empty);
  }
}
static std::unique_ptr<TextBlock> make(std::vector<std::string> words,
                                     std::vector<int16_t> positions,
                                     std::vector<EpdFontFamily::Style> styles) {
  return std::make_unique<TextBlock>(words, positions, styles,
                                   std::vector<uint8_t>{}, std::vector<uint16_t>{});
}
static std::string firstWord(const PageLine* firstLine) {
  std::string nextPageFirstWord;
  READER_CONDITION
    nextPageFirstWord = firstLine->getBlock()->wordText(0);
  }
  return nextPageFirstWord;
}
int main() {
  auto empty = make({}, {}, {});
  check(*empty, true, true);
  auto text = make({"hello", "world"}, {0, 40}, {EpdFontFamily::REGULAR, EpdFontFamily::REGULAR});
  check(*text, false, true);
  assert(std::strcmp(text->wordText(1), "world") == 0);
  auto blankWord = make({""}, {0}, {EpdFontFamily::REGULAR});
  check(*blankWord, false, true);  // emptiness counts words, not text bytes
  auto mismatch = make({"hello"}, {}, {EpdFontFamily::REGULAR});
  check(*mismatch, true, false);
  auto oversized = make({std::string(65535, 'x')}, {0}, {EpdFontFamily::REGULAR});
  check(*oversized, true, false);
  auto tooMany = make(std::vector<std::string>(513, "x"),
                      std::vector<int16_t>(513),
                      std::vector<EpdFontFamily::Style>(513, EpdFontFamily::REGULAR));
  check(*tooMany, true, false);
  failArrays = 2;
  auto oom = make({"hello"}, {0}, {EpdFontFamily::REGULAR});
  assert(failArrays == 0);  // initial allocation and retry both reached
  check(*oom, true, false);
  auto recovered = make({"hello"}, {0}, {EpdFontFamily::REGULAR});
  check(*recovered, false, true);
  PageLine nullLine(nullptr, 0, 0);
  PageLine emptyLine(std::move(empty), 0, 0);
  PageLine textLine(std::move(text), 0, 0);
  PageLine invalidLine(std::move(mismatch), 0, 0);
  assert(firstWord(&nullLine).empty());
  assert(firstWord(&emptyLine).empty());
  assert(firstWord(&invalidLine).empty());
  assert(firstWord(&textLine) == "hello");
  std::cout << "PASS: 8 block states; mutable/const/virtual dispatch; 4 reader conditions\n";
}
'''.replace('READER_CONDITION', condition)
(O / 'probe.cpp').write_text(probe)
incs = [R / p for p in [
    'test/crosspoint_migration/epub-layout-final/adapters',
    'test/chapter_html_slim_parser/stubs',
    'test/crosspoint_migration/epub-finish/stubs',
    'test/crosspoint_migration/font-finish/renderer-stubs',
    'freeink-sdk/libs/book/ContentProtection/include', 'lib/uzlib/src',
    'lib/miniz/src', 'src', 'lib', 'lib/Epub', 'lib/Epub/Epub']]
incs += [R / 'lib' / name for name in [
    'InflateReader', 'MemoryBudget', 'AsciiText', 'EpdFont', 'Utf8', 'Memory',
    'Serialization', 'MiniBidi', 'FsHelpers', 'XmlParserUtils', 'GfxRenderer',
    'FontCache', 'Bitmap', 'Hash', 'ZipFile', 'InflateStream', 'JsonParser',
    'Txt', 'PngToBmpConverter', 'JpegToBmpConverter', 'GifToBmpConverter']]
flags = ['-std=c++20', '-O0', '-g0', '-ffunction-sections', '-fdata-sections',
         '-include', str(R / 'lib/MemoryBudget/PoolBudget.h')]
if A.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
cmd = ['g++', *flags, *['-I' + str(p) for p in incs], str(O / 'probe.cpp'),
       str(R / 'lib/Epub/Epub/blocks/TextBlock.cpp'), str(R / 'lib/Epub/Epub/Page.cpp'),
       str(R / 'lib/Utf8/Utf8.cpp'), str(R / 'lib/MiniBidi/BidiUtils.cpp'),
       str(R / 'lib/Epub/Epub/CjkBreaks.cpp'),
       str(R / 'lib/Epub/Epub/hyphenation/HyphenationCommon.cpp'), str(O / 'minibidi.o'),
       '-Wl,--gc-sections', '-Wl,--wrap=_ZnamRKSt9nothrow_t', '-o', str(O / 'probe')]
env = dict(os.environ, TMPDIR='/root/.hermes/cache/scratch',
           ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
receipts = []
with open('/root/.hermes/cache/scratch/crosscub-review-probes.lock', 'a') as lock:
    fcntl.flock(lock, fcntl.LOCK_EX)
    ccmd = ['gcc', '-O0', '-c', str(R / 'lib/MiniBidi/minibidi.c'), '-o', str(O / 'minibidi.o')]
    if A.sanitize:
        ccmd += ['-fsanitize=address,undefined', '-fno-pie']
    for name, argv in [('bidi-compile', ccmd), ('compile', cmd), ('run', [str(O / 'probe')])]:
        with (O / (name + '.log')).open('w') as log:
            result = subprocess.run(argv, cwd=R, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=180)
        receipts.append(dict(name=name, argv=argv, exit=result.returncode))
        (O / 'receipts.json').write_text(json.dumps(receipts, indent=2))
        print(name, result.returncode, flush=True)
        if result.returncode:
            print((O / (name + '.log')).read_text()[-12000:])
            sys.exit(result.returncode)
# Freeze all directly included production and double headers via compiler depfile.
depcmd = cmd[:cmd.index(str(O / 'probe.cpp'))] + ['-MM'] + [
    p for p in cmd[cmd.index(str(O / 'probe.cpp')):] if p.endswith('.cpp')]
with open('/root/.hermes/cache/scratch/crosscub-review-probes.lock', 'a') as lock:
    fcntl.flock(lock, fcntl.LOCK_EX)
    deps = subprocess.check_output(depcmd, cwd=R, env=env, text=True)
(O / 'dependencies.d').write_text(deps)
inputs = {Path(__file__).resolve(), R / 'src/activities/reader/EpubReaderActivity.cpp',
          R / 'lib/MiniBidi/minibidi.c', R / 'lib/MiniBidi/minibidi.h'}
for token in deps.replace('\\\n', ' ').split():
    if not token.endswith(':') and Path(token).is_file():
        inputs.add(Path(token).resolve())
(O / 'inputs.json').write_text(json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                         for p in sorted(inputs)}, indent=2))
print((O / 'run.log').read_text(), end='')
