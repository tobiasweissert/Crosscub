#!/usr/bin/env python3
"""Home selection production-body probe; host seams, not firmware acceptance."""
from pathlib import Path
import fcntl
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
HOME = ROOT / 'src/activities/home'


def body(text, marker):
    start = text.index('{', text.index(marker))
    depth = 1
    for end in range(start + 1, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if depth == 0:
            return text[start:end + 1]
    raise AssertionError('unterminated production body: ' + marker)


class HomeBookSelection(unittest.TestCase):
    def test_home_helper_does_not_hide_base_selection(self):
        header = (HOME / 'HomeActivity.h').read_text()
        source = (HOME / 'HomeActivity.cpp').read_text()
        self.assertNotRegex(header, r'\bvoid\s+onSelectBook\s*\(')
        self.assertIn('void openHomeBook(const std::string& path);', header)
        self.assertEqual(len(re.findall(r'\bopenHomeBook\s*\(', source)), 3)
        self.assertNotRegex(source, r'\bonSelectBook\s*\(')
        self.assertIn('void onSelectBook(const std::string& path);',
                      (ROOT / 'src/activities/Activity.h').read_text())

    def probe(self, sanitized):
        compiler = shutil.which('g++')
        if compiler is None:
            self.skipTest('host g++ unavailable')
        source = (HOME / 'HomeActivity.cpp').read_text()
        helper = 'openHomeBook' if 'HomeActivity::openHomeBook(' in source else 'onSelectBook'
        selected = body(source, 'auto activateSelectedHomeItem =')
        continuing = body(source, 'void HomeActivity::onContinueReading(')
        selection = body(source, 'void HomeActivity::' + helper + '(')
        base = body((ROOT / 'src/activities/Activity.cpp').read_text(), 'void Activity::onSelectBook(')
        harness = r'''
#include <cassert>
#include <string>
#include <vector>
static std::vector<std::string> events;
static bool locked = false;
struct RenderLock {
  RenderLock() { assert(!locked); locked=true; events.push_back("lock"); }
  ~RenderLock() { assert(locked); locked=false; events.push_back("unlock"); }
};
struct Cache { void invalidate() { assert(locked); events.push_back("invalidate"); } } gCarouselCache;
static constexpr const char* CAROUSEL_CACHE_TMP_PATH = "/.crosspoint/home_carousel_cache.tmp";
struct StorageSeam {
  bool present = true;
  bool exists(const char* p) { assert(!locked); assert(p==std::string(CAROUSEL_CACHE_TMP_PATH)); events.push_back("exists"); return present; }
  void remove(const char* p) { assert(!locked); assert(p==std::string(CAROUSEL_CACHE_TMP_PATH)); events.push_back("remove"); present=false; }
} Storage;
struct Manager { void goToReader(const std::string& p) { assert(!locked); events.push_back("reader:"+p); } } activityManager;
struct CrossPointSettings { enum class UI_THEME { CLASSIC, LYRA_CAROUSEL }; };
struct Settings { CrossPointSettings::UI_THEME uiTheme=CrossPointSettings::UI_THEME::CLASSIC; } SETTINGS;
struct Metrics { bool homeContinueReadingInMenu=false; } metrics;
struct UITheme { static UITheme& getInstance() { static UITheme t; return t; } const Metrics& getMetrics() { return metrics; } };
struct Book { std::string path; };
struct Entry { int action; };
static std::vector<Entry> buildSelectableHomeMenuItems(bool,bool,bool,bool,bool) { return {{7}}; }
static int getHomeMenuSelectionOffset(const std::vector<Book>& b) { return metrics.homeContinueReadingInMenu ? 0 : int(b.size()); }
struct Activity { void onSelectBook(const std::string& path) BASE };
struct HomeActivity : Activity {
  std::vector<Book> recentBooks{{"/one.epub"},{"/two.txt"},{"/three.xtc"}};
  int selectorIndex=1, highlighted=2;
  bool hasOpdsServers=false, hasReadingStats=false, hasBookmarks=false, hasClippings=false;
  int getHighlightedBookIndex() { return highlighted; }
  void freeCarouselFrames() { assert(locked); events.push_back("frames"); }
  void HELPER(const std::string& path) SELECTION
  void onContinueReading() CONTINUING
  void activateSelectedHomeItem() {
    const int visibleBookCount=int(recentBooks.size());
    auto activateHomeMenuAction=[](int a) { events.push_back("menu:"+std::to_string(a)); };
    auto selected=[this, visibleBookCount, &activateHomeMenuAction]() SELECTED;
    selected();
  }
};
static void expect(const std::string& path, bool present) {
  std::vector<std::string> want{"lock","invalidate","frames","unlock","exists"};
  if(present) want.push_back("remove");
  want.push_back("reader:"+path); assert(events==want); events.clear();
}
int main() {
  HomeActivity h;
  for (bool present : {false,true}) {
    Storage.present=present; h.activateSelectedHomeItem(); expect("/two.txt",present);
    Storage.present=present; h.onContinueReading(); expect("/one.epub",present);
    SETTINGS.uiTheme=CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
    Storage.present=present; h.onContinueReading(); expect("/three.xtc",present);
    SETTINGS.uiTheme=CrossPointSettings::UI_THEME::CLASSIC;
  }
  SETTINGS.uiTheme=CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  for(int invalid : {-1,3}) { h.highlighted=invalid; h.onContinueReading(); assert(events.empty()); }
  metrics.homeContinueReadingInMenu=true; h.selectorIndex=0; h.activateSelectedHomeItem();
  assert(events==std::vector<std::string>{"menu:7"}); events.clear();
  h.selectorIndex=99; h.activateSelectedHomeItem(); assert(events.empty());
  h.recentBooks.clear(); h.onContinueReading(); assert(events.empty());
  Activity a; a.onSelectBook("/base.epub"); assert(events==std::vector<std::string>{"reader:/base.epub"}); events.clear();
  static_cast<Activity&>(h).onSelectBook("/inherited.epub");
  assert(events==std::vector<std::string>{"reader:/inherited.epub"});
}
'''
        for token, value in [('BASE', base), ('SELECTION', selection), ('CONTINUING', continuing),
                             ('SELECTED', selected), ('HELPER', helper)]:
            harness = harness.replace(token, value)
        scratch = os.environ.get('TMPDIR')
        with tempfile.TemporaryDirectory(prefix='home-selection-', dir=scratch) as temp:
            cpp = Path(temp) / 'probe.cpp'
            exe = Path(temp) / 'probe'
            cpp.write_text(harness)
            args = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-g', str(cpp), '-o', str(exe)]
            if sanitized:
                args += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
            host_lock = Path('/root/.hermes/cache/scratch/crosscub-review-probes.lock')
            lock_path = (host_lock if os.access(host_lock.parent, os.W_OK)
                         else Path(tempfile.gettempdir()) / 'crosscub-review-probes.lock')
            with open(lock_path, 'a') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                compiled = subprocess.run(args, capture_output=True, text=True, timeout=60)
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                env = os.environ.copy()
                if sanitized:
                    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
                run = subprocess.run([str(exe)], capture_output=True, text=True, env=env, timeout=20)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_production_selection_and_both_callers(self):
        self.probe(False)

    def test_production_selection_and_both_callers_asan_ubsan(self):
        self.probe(True)


if __name__ == '__main__':
    unittest.main()
