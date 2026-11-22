#!/usr/bin/env python3
"""Complete SettingsActivity/CrossPointSettings header regression (no target SDK).

Run with ARDUINOJSON_INCLUDE_DIR pointing at an existing ArduinoJson src folder.
SETTINGS_STRING_SANITIZE=1 enables ASan/UBSan. CXX selects the native compiler.
UI/HAL services are declaration-only seams; real settings storage and every
production String factory invocation are compiled. No dependency downloads.
"""
from pathlib import Path
import contextlib
import json
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = 'src/activities/settings/SettingsActivity.h'


def fixture(directory):
    """Keep both production headers complete; stub only unavailable services."""
    stubs = {
        'FreeInkApp.h': 'namespace freeink::ui { struct ActionEvent {}; template<int,int> struct FreeInkApp { struct ScreenType {}; }; }',
        'FreeInkUIGfxRenderer.h': 'namespace freeink::ui { struct GfxRendererTarget {}; }',
        'activities/Activity.h': '''class GfxRenderer { public: enum class Orientation { Portrait }; };
class MappedInputManager {}; struct RenderLock {};
class Activity { public: virtual ~Activity()=default; virtual bool allowGlobalHomeSwipeGesture() const {return true;}
virtual void onEnter() {} virtual void onExit() {} virtual void loop() {} virtual void render(RenderLock&&) {} };''',
        'components/OptionPopup.h': 'class OptionPopup {};',
        'util/ButtonNavigator.h': 'class ButtonNavigator {};',
        'Epub/ReaderRenderSpec.h': 'enum class EpubRenderMode { CrossSmudgeDefault }; struct ReaderRenderSpec {};',
        'HalStorage.h': 'class HalFile {}; using FsFile=HalFile;',
        'BlePageTurner.h': 'namespace bleturner { struct Config { bool enabled=false; }; }',
        'BlePageTurnerJson.h': '''#include <ArduinoJson.h>
#include <BlePageTurner.h>
namespace bleturner { inline void writeJson(const Config&,JsonDocument&) {}
inline bool readJson(Config&,JsonVariantConst) {return true;} }''',
    }
    # StrId is an external localization interface, not a settings implementation.
    names = sorted(set(re.findall(r'StrId::(\w+)', (ROOT/HEADER).read_text() +
                                 (ROOT/'src/SettingsList.cpp').read_text() +
                                 (ROOT/'src/activities/settings/SettingsSection.h').read_text())))
    stubs['I18n.h'] = ('enum class StrId {' + ','.join(names) + '};\n'
                       'struct I18n { const char* get(StrId) const {return "label";} }; inline I18n I18N;')
    for name in ['Arduino.h', 'Logging.h']:
        stubs[name] = (ROOT/'test/crosspoint_migration/settings/stubs'/name).read_text()
    for name, body in stubs.items():
        path = directory/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n' + body + '\n')
    # Match balanced parentheses rather than truncating sizeof/SETTINGS expressions.
    source = (ROOT/'src/SettingsList.cpp').read_text()
    calls = []
    for match in re.finditer(r'\bSettingInfo::String\s*\(', source):
        depth = 1
        end = match.end()
        while depth:
            if source[end] == '(':
                depth += 1
            elif source[end] == ')':
                depth -= 1
            end += 1
        calls.append(source[match.start():end])
    if not calls:
        raise AssertionError('No production String callers found')
    fields = []
    for call in calls:
        field = re.search(r'SETTINGS\.(\w+)', call)
        if field is None:
            raise AssertionError('String caller no longer uses direct settings storage: ' + call)
        fields.append(field.group(1))
    expected = ','.join('{SETTINGS.' + field + ',sizeof(SETTINGS.' + field + ')}'
                        for field in fields)
    # Compile the exact real editor write statements, not a reimplementation.
    editor = (ROOT/'src/activities/settings/SettingsActivity.cpp').read_text()
    start = editor.index('char* ptr = reinterpret_cast<char*>(&SETTINGS) + selectedSetting.stringOffset;')
    stop = editor.index("ptr[selectedSetting.stringMaxLen - 1] = '\\0';", start)
    write = editor[start:stop] + "ptr[selectedSetting.stringMaxLen - 1] = '\\0';"
    probe = r'''#include <cassert>
#include <cstring>
#include <cstdio>
#include <type_traits>
#include "activities/settings/SettingsActivity.h"
uint8_t CrossPointSettings::defaultUiScale() {return UI_SCALE_LARGE;}
using Factory=decltype(&SettingInfo::String);
static_assert(std::is_invocable_r_v<SettingInfo,Factory,StrId,char*,size_t,const char*,StrId>);
static_assert(std::is_invocable_r_v<SettingInfo,Factory,StrId,const char*,size_t,const char*,StrId>);
static_assert(std::is_same_v<Factory,SettingInfo(*)(StrId,const char*,size_t,const char*,StrId)>);
// No production getStringPtr exists in this tree. This probe adapter uses the
// unchanged offset expression employed by editor, JSON load and web mutations.
char* getStringPtr(const SettingInfo& s) {return reinterpret_cast<char*>(&SETTINGS)+s.stringOffset;}
static_assert(std::is_same_v<decltype(getStringPtr(std::declval<const SettingInfo&>())),char*>);
void edit(const SettingInfo& selectedSetting,const std::string& value) {
WRITE
}
int main() {
 const CrossPointSettings& view=SETTINGS;
 auto mutableInfo=SettingInfo::String(StrId::STR_NONE_OPT,SETTINGS.deviceName,sizeof(SETTINGS.deviceName));
 auto constInfo=SettingInfo::String(StrId::STR_NONE_OPT,view.deviceName,sizeof(view.deviceName));
 assert(mutableInfo.stringOffset==constInfo.stringOffset);
 assert(getStringPtr(constInfo)==SETTINGS.deviceName);
 // Only the address is consumed, even when maxLen=0: do not dereference input.
 auto zero=SettingInfo::String(StrId::STR_NONE_OPT,view.deviceName,0);
 assert(zero.stringOffset==constInfo.stringOffset && zero.stringMaxLen==0);
 std::vector<SettingInfo> settings={CALLS};
 struct Expected {char* ptr; size_t size;};
 const Expected expected[]={EXPECTED};
 assert(settings.size()==sizeof(expected)/sizeof(expected[0]));
 unsigned cases=0;
 size_t index=0;
 for(const auto& s:settings) {
  assert(s.type==SettingType::STRING && s.stringMaxLen>1 && s.key);
  char* storage=getStringPtr(s);
  assert(storage==expected[index].ptr && s.stringMaxLen==expected[index].size);
  ++index;
  const std::string original(storage);
  auto copy=s;
  assert(getStringPtr(copy)==storage);
  const auto fromConst=SettingInfo::String(s.nameId,static_cast<const char*>(storage),s.stringMaxLen,s.key,s.category);
  assert(fromConst.stringOffset==s.stringOffset && fromConst.stringMaxLen==s.stringMaxLen);
  assert(fromConst.key==s.key && fromConst.category==s.category && fromConst.nameId==s.nameId);
  for(const auto& value:std::vector<std::string>{"", "edit", std::string(s.stringMaxLen-1,'b'),std::string(s.stringMaxLen+17,'x')}) {
   edit(s,value);
   assert(std::string(storage)==value.substr(0,s.stringMaxLen-1));
   assert(storage[s.stringMaxLen-1]=='\0');
   ++cases;
  }
  edit(s,original);
  assert(std::string(storage)==original);
 }
 printf("PASS %zu production String callers; %u editor cases; complete real headers; mutable and const inputs\n",settings.size(),cases);
}
'''.replace('WRITE', write).replace('CALLS', ',\n'.join(calls)).replace('EXPECTED', expected)
    (directory/'probe.cpp').write_text(probe)
    return calls


class SettingsStringPointerConst(unittest.TestCase):
    def test_complete_headers_and_production_callers(self):
        compiler = shlex.split(os.environ.get('CXX', 'g++'))
        if not shutil.which(compiler[0]):
            self.skipTest('native C++ compiler unavailable')
        candidates = [Path(os.environ.get('ARDUINOJSON_INCLUDE_DIR', '/nonexistent')),
                      ROOT/'.pio/libdeps/sticky/ArduinoJson/src']
        dependency = next((p for p in candidates if (p/'ArduinoJson.h').is_file()), None)
        if dependency is None:
            self.skipTest('supply existing ArduinoJson src via ARDUINOJSON_INCLUDE_DIR')
        output = os.environ.get('SETTINGS_STRING_EVIDENCE')
        manager = (contextlib.nullcontext(output) if output else
                   tempfile.TemporaryDirectory(prefix='settings-string-'))
        with manager as name:
            directory = Path(name)
            directory.mkdir(parents=True, exist_ok=True)
            calls = fixture(directory)
            command = [*compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                       '-fno-exceptions', '-fno-rtti', '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0',
                       '-I'+str(directory), '-I'+str(dependency),
                       '-I'+str(ROOT/'lib/Serialization'), '-I'+str(ROOT/'src'),
                       str(directory/'probe.cpp'), '-o', str(directory/'probe')]
            if os.environ.get('SETTINGS_STRING_SANITIZE') == '1':
                command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
            result = subprocess.run(command, capture_output=True, timeout=90)
            (directory/'compile.log').write_bytes(result.stdout+result.stderr)
            receipt = {'command': command, 'compile_exit': result.returncode, 'callers': calls}
            (directory/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            result = subprocess.run([str(directory/'probe')], capture_output=True, timeout=30,
                                    env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
                                         'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
            (directory/'run.log').write_bytes(result.stdout+result.stderr)
            receipt.update(run_exit=result.returncode)
            (directory/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
            self.assertEqual(result.returncode, 0, (result.stdout+result.stderr).decode())


if __name__ == '__main__':
    unittest.main()
