#!/usr/bin/env python3
"""Complete real parser declaration/inline constructors; host dependency seams only.

No parser implementation, rendering, expat, storage, target or device qualification.
Private access is exposed only in the generated probe; production is unchanged.
"""
from pathlib import Path
import argparse
import fcntl
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'lib/Epub/Epub/parsers/ChapterHtmlSlimParser.h'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--sanitize', action='store_true')
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    header = HEADER.read_text()
    declaration = header[header.index('class ChapterHtmlSlimParser {'):]
    declaration = declaration.replace('class ChapterHtmlSlimParser {', 'class ChapterHtmlSlimParser {\n public:', 1)
    declaration = declaration.replace('\n private:', '\n public:')
    preamble = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "Epub/EpubRenderMode.h"
#include "Epub/FootnoteEntry.h"
#include "Epub/css/CssStyle.h"
#define MAX_WORD_SIZE 200
#define XMLCALL
using XML_Char = char;
using XML_Parser = void*;
struct HalFile { explicit operator bool() const { return false; } size_t position() { return 0; } size_t size() { return 0; } };
struct EpdFontFamily { enum Style { REGULAR }; };
struct GfxRenderer { int marker; };
struct Epub {};
struct Page {};
struct ParsedText {};
struct ImageBlock {};
struct TextBlock {};
struct CssParser {};
struct BlockStyle {};
struct CssAncestor {};
struct CompactTableLayout {};
'''
    checks = r'''
// Destructor/storage parsing is outside this constructor probe.
ChapterHtmlSlimParser::~ChapterHtmlSlimParser() = default;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)
int main() {
  GfxRenderer first{17}, second{29};
  std::string path = "chapter.xhtml", base = "OPS", images = "cache";
  CssParser css;
  int completed = 0, ownerCompleted = 0, popups = 0;
  auto book = std::make_shared<Epub>();
  std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)> callback =
    [&](auto page, auto a, auto b, auto c) { CHECK(page && a == 1 && b == 2 && c == 3); ++completed; };
  std::function<void()> popup = [&] { ++popups; };
  std::vector<std::string> anchors{"a", "b"};
  ChapterHtmlSlimParser direct(book, path, first, 8, 1.25f, true, 3, 480, 800, true, false,
    callback, true, base, images, 2, anchors, popup, &css);
  CHECK(&direct.renderer == &first && &direct.filepath == &path && direct.epub == book);
  CHECK(direct.fontId == 8 && direct.lineCompression == 1.25f && direct.extraParagraphSpacing);
  CHECK(direct.paragraphAlignment == 3 && direct.viewportWidth == 480 && direct.viewportHeight == 800);
  CHECK(direct.hyphenationEnabled && !direct.focusReadingEnabled && direct.cssParser == &css);
  CHECK(direct.embeddedStyle && direct.imageRendering == 2 && direct.contentBase == base && direct.imageBasePath == images);
  CHECK(direct.tocAnchors == anchors && !direct.ownerCompletePageFn && !direct.guideReadingEnabled);
  direct.completePageFn(std::make_unique<Page>(), 1, 2, 3); direct.popupFn();
  ChapterHtmlSlimParser defaults(book, path, second, 4, 1.f, false, 0, 800, 480, false, true,
    {}, false, base, images);
  CHECK(&defaults.renderer == &second && !defaults.popupFn && !defaults.cssParser && defaults.tocAnchors.empty());
  CHECK(defaults.imageRendering == 0 && defaults.paragraphIndentSpaces == 2);
  Epub borrowed;
  std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t, uint32_t)> owner =
    [&](auto page, auto a, auto b, auto c, auto d) { CHECK(page && a == 4 && b == 5 && c == 6 && d == 7); ++ownerCompleted; };
  for (bool indents : {false, true}) for (uint8_t spacing : {0, 2, 4, 255}) {
    ChapterHtmlSlimParser delegated(borrowed, path, second, 9, 0.75f, false, indents, 1, 800, 480,
      false, true, true, spacing, owner, false, base, images, 1, anchors, popup, &css,
      EpubRenderMode::Light, "preview", 3, true);
    CHECK(&delegated.renderer == &second && &delegated.filepath == &path);
    CHECK(delegated.epub.get() == &borrowed && delegated.epub.use_count() == 0);
    CHECK(delegated.fontId == 9 && delegated.lineCompression == .75f && !delegated.extraParagraphSpacing);
    CHECK(delegated.viewportWidth == 800 && delegated.viewportHeight == 480 && delegated.paragraphAlignment == 1);
    CHECK(!delegated.hyphenationEnabled && delegated.focusReadingEnabled && delegated.guideReadingEnabled);
    CHECK(delegated.forceParagraphIndents == indents && delegated.paragraphIndentSpaces == (indents ? 2 : 0));
    CHECK(delegated.legacyWordSpacing == (spacing > 4 ? 4 : spacing));
    CHECK(delegated.renderMode == EpubRenderMode::Light && delegated.previewAnchor == "preview");
    CHECK(delegated.previewMaxPages == 3 && delegated.trackReferenceCharacters && delegated.isScanningForPreviewAnchor());
    CHECK(!delegated.completePageFn && delegated.cssParser == &css && delegated.tocAnchors == anchors);
    delegated.ownerCompletePageFn(std::make_unique<Page>(), 4, 5, 6, 7); delegated.popupFn();
  }
  CHECK(completed == 1 && ownerCompleted == 8 && popups == 9 && book.use_count() == 3);
  CHECK(first.marker == 17 && second.marker == 29);
}
'''
    source = out / 'probe.cpp'
    source.write_text(preamble + declaration + checks)
    dependencies = [HEADER, Path(__file__)] + [ROOT / ('lib/Epub/Epub/' + name) for name in ['EpubRenderMode.h', 'FootnoteEntry.h', 'css/CssStyle.h']]
    (out / 'inputs.json').write_text(json.dumps({str(x.relative_to(ROOT)): hashlib.sha256(x.read_bytes()).hexdigest() for x in dependencies}, indent=2) + '\n')
    command = ['g++', '-std=c++20', '-g', '-I' + str(ROOT / 'lib/Epub'), str(source), '-o', str(out / 'probe')]
    if a.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    rows = []
    with open('/root/.hermes/cache/scratch/crosscub-review-probes.lock', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        for cmd in [command, [str(out / 'probe')]]:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=60, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
            log = out / ('compile.log' if cmd == command else 'run.log')
            log.write_text(r.stdout + r.stderr)
            rows.append({'command': cmd, 'exit': r.returncode, 'log': str(log)})
            (out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
            if r.returncode:
                print(log.read_text())
                return r.returncode
    print('PASS: real direct/default/delegating constructors; supplied renderer identity, callbacks, borrowed book, settings')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
