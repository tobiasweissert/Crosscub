#!/usr/bin/env python3
"""Bounded production-body member separation probe, not a full reader/target build."""
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
p.add_argument('--sanitize', action='store_true')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
D = R / 'src/activities/reader'
inputs = {}


def read(name):
    text = (D / name).read_text()
    inputs[name] = hashlib.sha256(text.encode()).hexdigest()
    return text


def block(text, marker):
    """Extract a brace-balanced production block (these selected blocks have no brace literals)."""
    start = text.index(marker)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


base_h = read('ReaderActivity.h')
base_cpp = read('ReaderActivity.cpp')
headers = {n: read(n + 'ReaderActivity.h') for n in ('Epub', 'Txt', 'Xtc')}
cpps = {n: read(n + 'ReaderActivity.cpp') for n in ('Epub', 'Txt', 'Xtc')}
# Rename contract fails on the original source before compiling behavior slices.
for name, header in headers.items():
    assert 'int formatPagesUntilFullRefresh = 0;' in header, name + ': derived countdown still shadows base'
    assert not re.search(r'\bpagesUntilFullRefresh\b', re.sub(r'//[^\n]*', '', header + cpps[name])), name
    if name != 'Txt':
        assert 'std::unique_ptr<EndOfBookOptions> formatEndOfBookOptions;' in header, name
        assert not re.search(r'\bendOfBookOptions\b', header + cpps[name]), name
assert 'int pagesUntilFullRefresh = 0;' in base_h
assert 'std::unique_ptr<EndOfBookOptions> endOfBookOptions;' in base_h

code = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#define LOG_ERR(...) ((void)0)
struct Settings {int frequency=0;int getRefreshFrequency(){return frequency;}} SETTINGS;
struct GfxRenderer {int clears=0,displays=0;void clearScreen(){++clears;}void displayBuffer(){++displays;}};
struct MappedInputManager {};
struct BookReaderSettingsData {};
struct Book {std::string getPath(){return "book";}unsigned getPageCount(){return 3;}int getSpineItemsCount(){return 3;}};
using Epub=Book;using Txt=Book;using Xtc=Book;
struct EndOfBookOptions {int loads=0,renders=0;explicit EndOfBookOptions(GfxRenderer&){}void loadOnce(std::string){++loads;}void render(GfxRenderer&,MappedInputManager&){++renders;}bool menuActive(){return true;}};
bool failAllocation=false;
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... args){return failAllocation?nullptr:std::make_unique<T>(std::forward<A>(args)...);}
struct Activity {GfxRenderer& renderer;MappedInputManager& mappedInput;Activity(const char*,GfxRenderer& r,MappedInputManager& m):renderer(r),mappedInput(m){}};
struct RenderLock {template<class T>explicit RenderLock(T&) {}};
struct ReaderActivity:Activity {
 int pagesUntilFullRefresh=0;std::unique_ptr<EndOfBookOptions> endOfBookOptions;
 std::atomic<bool> endOfBookOptionsReady{false};std::string bookPath;bool recentUpdateOnRender;bool atEnd=false;
 ReaderActivity(const char*,GfxRenderer&,MappedInputManager&,std::string,bool);
 void disableFastInitialRefresh();
 bool isAtEndOfBook()const{return atEnd;}
 void clearEndOfBookOptionsIfNeeded();bool endOfBookMenuActive()const;
 void exitCleanup(){
'''
# Preserve the exact base onExit cleanup statements without emulating unrelated lifecycle work.
start = base_cpp.index('  endOfBookOptions.reset();', base_cpp.index('void ReaderActivity::onExit()'))
code += base_cpp[start:base_cpp.index('\n}', start)] + '\n}\n};\n'
code += block(base_cpp, 'ReaderActivity::ReaderActivity(') + '\n'
for signature in ('void ReaderActivity::disableFastInitialRefresh(',
                  'void ReaderActivity::clearEndOfBookOptionsIfNeeded(',
                  'bool ReaderActivity::endOfBookMenuActive('):
    code += block(base_cpp, signature) + '\n'
for name, header in headers.items():
    cls = name + 'ReaderActivity'
    # Exact production constructor definitions, including both initializer lists.
    first = header.index('  explicit ' + cls)
    second = header.index('  explicit ' + cls, first + 1)
    end = header.index(' {}', second) + 3
    constructors = header[first:end]
    code += f'struct {cls}:ReaderActivity {{\n'
    code += f' std::unique_ptr<{name}> {name.lower()};\n'
    code += ' BookReaderSettingsData initialBookReaderSettings;\n'
    declaration = re.search(r'  int formatPagesUntilFullRefresh = 0;', header)
    assert declaration is not None
    code += declaration[0] + '\n'
    code += ' bool cleanImageBasePending=false,skipRecentBookUpdateOnEntry=false;\n'
    code += constructors + '\n'
    code += block(header, 'bool prepareManualRefresh()').replace(' override', '') + '\n'
    if name != 'Txt':
        code += ' std::unique_ptr<EndOfBookOptions> formatEndOfBookOptions;\n'
        code += ' int currentSpineIndex=3;unsigned currentPage=3;bool automaticPageTurnActive=true;void showPendingSyncSaveError(){}\n'
        marker = ('  if (currentSpineIndex == epub->getSpineItemsCount()) {' if name == 'Epub'
                  else '  if (pageToRender >= xtc->getPageCount()) {')
        code += ' void endScreen(){const unsigned pageToRender=currentPage;(void)pageToRender;\n' + block(cpps[name], marker) + '\n}\n'
        marker = ('  if (!atEndOfBook && ' if name == 'Epub' else '    if (currentPage < xtc->getPageCount() && ')
        code += ' void leaveEnd(){const bool atEndOfBook=false;(void)atEndOfBook;\n' + block(cpps[name], marker) + '\n}\n'
    code += '};\n'
code += r'''
template<class T>void countdown(GfxRenderer& r,MappedInputManager& m){
 for(int freq:{0,1,2,7})for(bool fast:{false,true}){
  SETTINGS.frequency=freq;T h(r,m,std::string("book"),fast);
  assert(h.pagesUntilFullRefresh==(fast?std::max(2,freq):0));
  assert(h.formatPagesUntilFullRefresh==(fast?std::max(2,freq):0));
  h.pagesUntilFullRefresh=99;assert(h.prepareManualRefresh());
  assert(h.formatPagesUntilFullRefresh==-1 && h.pagesUntilFullRefresh==99);
  h.disableFastInitialRefresh();assert(h.pagesUntilFullRefresh==0 && h.formatPagesUntilFullRefresh==-1);
 }
}
template<class T>void endState(T& h){
 assert(!h.endOfBookOptions && !h.formatEndOfBookOptions && !h.endOfBookOptionsReady);
 failAllocation=true;h.endScreen();assert(!h.formatEndOfBookOptions);failAllocation=false;
 h.endScreen();assert(h.formatEndOfBookOptions && !h.endOfBookOptions && !h.endOfBookOptionsReady);
 auto* format=h.formatEndOfBookOptions.get();h.endScreen();assert(h.formatEndOfBookOptions.get()==format && format->renders==2);
 h.atEnd=true;assert(!h.endOfBookMenuActive());
 h.endOfBookOptions=std::make_unique<EndOfBookOptions>(h.renderer);h.endOfBookOptionsReady=true;
 assert(h.endOfBookMenuActive());h.exitCleanup();
 assert(!h.endOfBookOptions && !h.endOfBookOptionsReady && h.formatEndOfBookOptions.get()==format);
 h.endOfBookOptions=std::make_unique<EndOfBookOptions>(h.renderer);h.endOfBookOptionsReady=true;h.atEnd=false;
 h.clearEndOfBookOptionsIfNeeded();assert(!h.endOfBookOptions && !h.endOfBookOptionsReady && h.formatEndOfBookOptions.get()==format);
 h.currentPage=0;h.leaveEnd();assert(!h.formatEndOfBookOptions);
}
int main(){GfxRenderer r;MappedInputManager m;
 countdown<EpubReaderActivity>(r,m);countdown<TxtReaderActivity>(r,m);countdown<XtcReaderActivity>(r,m);
 for(int initial:{-1,0,1,9}){
  SETTINGS.frequency=7;
  EpubReaderActivity e(r,m,std::make_unique<Epub>(),BookReaderSettingsData{},initial,true,true);
  TxtReaderActivity t(r,m,std::make_unique<Txt>(),initial,true);
  XtcReaderActivity x(r,m,std::make_unique<Xtc>(),initial,true);
  assert(e.formatPagesUntilFullRefresh==initial && t.formatPagesUntilFullRefresh==initial && x.formatPagesUntilFullRefresh==initial);
  assert(e.pagesUntilFullRefresh==(initial>0?7:0) && t.pagesUntilFullRefresh==e.pagesUntilFullRefresh && x.pagesUntilFullRefresh==e.pagesUntilFullRefresh);
  endState(e);endState(x);
 }
 std::cout<<"PASS reader member separation: 24 path constructors, 12 owned-book constructors, manual/base refresh, 8 end-screen lifecycle cases including allocation failure\n";
}
'''
(a.output / 'slice.cpp').write_text(code)
(a.output / 'inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
with open('/root/.hermes/cache/scratch/crosscub-review-probes.lock', 'a') as lock:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    cmd = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1']
    if a.sanitize:
        cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    exe = a.output / 'slice'
    cmd += [str(a.output / 'slice.cpp'), '-o', str(exe)]
    print('COMPILE', json.dumps(cmd), flush=True)
    subprocess.run(cmd, check=True, timeout=45)
    result = subprocess.run([str(exe)], timeout=10, env={**os.environ,
        'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
    exe.unlink()
    raise SystemExit(result.returncode)
