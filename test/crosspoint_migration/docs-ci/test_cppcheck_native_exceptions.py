"""Compile the actual pinned public value API and execute the mutable-body guard.

Host qualification, not a firmware or physical-device test.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT/'freeink-sdk/libs/ui/FreeInkUI'

API = r'''
#include <FreeInkApp.h>
#include <type_traits>
using namespace freeink::ui;
using Emit = void (*)(const char*, Rect);
static_assert(std::is_same_v<decltype(&Paint::bitmapFill), Paint (*)(BitmapFill)>);
static_assert(std::is_same_v<decltype(&measureWrappedText), Size (*)(const DrawTarget&, const char*, TextStyle, int16_t)>);
static_assert(std::is_same_v<decltype(&drawBorderEdges), void (*)(DrawTarget&, Rect, Paint, uint8_t, uint8_t, uint8_t, uint8_t)>);
static_assert(std::is_same_v<decltype(&layoutText<Emit>), void (*)(const DrawTarget&, Rect, const char*, TextStyle, Emit&&)>);
static_assert(std::is_same_v<decltype(&FreeInkApp<>::setDevice), void (FreeInkApp<>::*)(DeviceContext)>);
static_assert(std::is_same_v<decltype(&Screen<32>::navHeader), void (Screen<32>::*)(const char*, ActionId, BitmapRef, const char*, uint8_t, const char*, ActionId, bool, LayoutAnchor)>);
// These are precisely the virtual signatures required by GfxRendererTarget.
struct Contract : DrawTarget {
  Size measureText(FontId, const char*, TextStyle) const override { return {}; }
  Rect inkBounds(FontId, const char*, TextStyle) const override { return {}; }
  void fill(Rect, Paint, uint8_t, uint8_t) override {}
  void stroke(Rect, Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(Point, Point, uint8_t, Paint) override {}
  void triangle(Point, Point, Point, Paint) override {}
  void text(Rect, const char*, TextStyle) override {}
  void bitmap(Rect, BitmapRef, BitmapMode, Paint, Rotation) override {}
};
int main() {}
'''

GUARD = r'''
#define main sdk_baseline_main
#include "HOST_TEST"
#undef main
class CountTarget : public FakeDrawTarget {
 public:
  mutable int availabilityMeasures = 0;
  int16_t lineHeight(FontId font) const override {
    if (font == 42) ++availabilityMeasures;
    return 12;
  }
};
int main() {
  CountTarget draw;
  DeviceContext device = makeDevice(120, 48);
  InputSnapshot input;
  InteractionBuffer<16> hits;
  Frame<16> frame(draw, device, input, hits);
  PublicationPageProps props;
  props.padding = Insets{};
  props.actionHeight = 48;
  props.actionGap = 0;
  props.primary.label = "Borrow";
  props.primary.action = 91;
  props.availability.status = "Available";
  props.availability.headingText.font = 42;
  publicationPage(frame, device.screen(), props);
  return hits.count() == 1 && draw.availabilityMeasures == 0 ? 0 : 1;
}
'''


class NativeExceptionTests(unittest.TestCase):
    def setUp(self):
        if not (SDK/'include/FreeInkUICore.h').is_file():
            self.skipTest('pinned SDK submodule required for native exception controls')
        self.assertIsNotNone(shutil.which('c++'))
        # Exercise installed overlay bytes even on a fresh, unmodified SDK;
        # never patch the checkout merely by importing/running a regression.
        import importlib.util
        spec = importlib.util.spec_from_file_location('native_exact', ROOT/'scripts/cppcheck_exact.py')
        assert spec is not None and spec.loader is not None
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        sdk_root = Path(scratch.name)
        self.sdk = sdk_root/'libs/ui/FreeInkUI'
        shutil.copytree(SDK, self.sdk)
        helper.apply_patches(sdk_root, verify_revision=False)

    def compile(self, directory, source, include=None):
        source_path = directory/'probe.cpp'
        source_path.write_text(source)
        result = subprocess.run(['c++', '-std=c++17', '-pthread', '-Wall', '-Werror',
                                 '-I'+str(include or self.sdk/'include'), str(source_path),
                                 str(self.sdk/'src/FreeInkUI.cpp'), '-o', str(directory/'probe')],
                                capture_output=True, text=True, timeout=90)
        return result

    def test_public_byvalue_api_and_reference_break_controls(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            positive = self.compile(directory, API)
            self.assertEqual(positive.returncode, 0, positive.stderr)
            public_contracts = [line for line in API.splitlines() if line.startswith('static_assert')]
            mutations = []
            for line in public_contracts:
                for token in ('BitmapFill', 'TextStyle', 'DeviceContext', 'BitmapRef', 'Paint'):
                    changed = line.replace(token+',', 'const '+token+'&,').replace('('+token+')', '(const '+token+'&)')
                    if changed != line:
                        mutations.append((line, changed))
                        break
            for method, token in (
                ('measureText', 'TextStyle'), ('inkBounds', 'TextStyle'),
                ('fill', 'Paint'), ('stroke', 'Paint'), ('line', 'Paint'),
                ('triangle', 'Paint'), ('text', 'TextStyle'),
                ('bitmap', 'BitmapRef'), ('bitmap', 'Paint'),
            ):
                line = next(line for line in API.splitlines() if ' '+method+'(' in line)
                mutations.append((line, line.replace(token, 'const '+token+'&', 1)))
            self.assertEqual(len(mutations), 15)
            for old, new in mutations:
                negative = self.compile(directory, API.replace(old, new))
                self.assertNotEqual(negative.returncode, 0, old)
                self.assertTrue('static assertion failed' in negative.stderr or 'does not override' in negative.stderr, negative.stderr)

    def test_publication_guard_is_required_after_lambda_consumes_body(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            source = GUARD.replace('HOST_TEST', str(self.sdk/'test/host/test_freeinkui.cpp'))
            positive = self.compile(directory, source)
            self.assertEqual(positive.returncode, 0, positive.stderr)
            self.assertEqual(subprocess.run([str(directory/'probe')], timeout=10).returncode, 0)
            copied = directory/'include'
            shutil.copytree(self.sdk/'include', copied)
            header = copied/'components/media/publication-page.h'
            text = header.read_text()
            old = '  action(props.more, false);\n  if (body.empty()) return;'
            self.assertEqual(text.count(old), 1)
            header.write_text(text.replace(old, '  action(props.more, false);'))
            mutant = self.compile(directory, source, copied)
            self.assertEqual(mutant.returncode, 0, mutant.stderr)
            self.assertEqual(subprocess.run([str(directory/'probe')], timeout=10).returncode, 1)

    def test_static_recommendations_preserve_instance_and_platform_contracts(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            for path, name, member in (
                ('freeink-sdk/libs/hardware/InputManager/include/InputManager.h', 'InputManager', 'hasToggleSwitch'),
                ('lib/hal/HalGPIO.h', 'HalGPIO', 'isXteinkDevice'),
            ):
                line = next(line.strip() for line in (ROOT/path).read_text().splitlines()
                            if 'bool '+member+'() const {' in line)
                source = ('#include <type_traits>\n'
                          'struct BoardConfig { inline static struct { struct { int toggleSwitch; } input; } ACTIVE = {{-1}}; };\n'
                          'class '+name+' { public: '+line+'\n};\n'
                          'static_assert(std::is_same_v<decltype(&'+name+'::'+member+'), bool ('+name+'::*)() const>);\nint main() {}\n')
                positive = self.compile(directory, source)
                self.assertEqual(positive.returncode, 0, positive.stderr)
                negative = self.compile(directory, source.replace(line, line.replace('bool ', 'static bool ', 1).replace('() const', '()')))
                self.assertNotEqual(negative.returncode, 0)
            text = (ROOT/'freeink-sdk/libs/display/FreeInkDisplay/src/bus/EpdBus.h').read_text()
            start = text.index('#if defined(ARDUINO)', text.index('class EpdBus'))
            end = text.index('#endif', start)+len('#endif')
            # Compile the actual conditional guard definitions (only surrounding
            # Arduino/SPI transport declarations are omitted).
            definitions = text[start:end]
            source = ('#include <type_traits>\n#include <cstddef>\n'
                      'using esp_pm_lock_handle_t = int*; constexpr int ESP_OK = 0;\n'
                      'int acquired = 0, released = 0;\n'
                      'int esp_pm_lock_acquire(int*) { ++acquired; return ESP_OK; }\n'
                      'int esp_pm_lock_release(int*) { ++released; return ESP_OK; }\n'
                      'class EpdBus { public:\n'+definitions+'\n};\n'
                      'using G = EpdBus::SpiApbLockGuard;\n'
                      'static_assert(std::is_same_v<decltype(&G::release), void (G::*)()>);\n'
                      'int main() { EpdBus bus;\n'
                      '#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE\n'
                      'int lock; bus._spiApbLock = &lock;\n'
                      '#endif\n'
                      'G guard(bus); guard.release(); guard.release();\n'
                      '#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE\n'
                      'return acquired == 1 && released == 1 ? 0 : 1;\n'
                      '#else\nreturn acquired == 0 && released == 0 ? 0 : 1;\n#endif\n}\n')
            for defines in ('', '#define ARDUINO 1\n#define CONFIG_PM_ENABLE 1\n'):
                result = self.compile(directory, defines+source)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(subprocess.run([str(directory/'probe')], timeout=10).returncode, 0)
            result = self.compile(directory, source.replace('void release() {}', 'static void release() {}'))
            self.assertNotEqual(result.returncode, 0)

    def test_ordered_accumulation_preserves_original_results_and_calls(self):
        import json
        patch = json.loads((ROOT/'scripts/sdk-analyzer-patches.json').read_text())
        old, new = patch['libs/ui/FreeInkUI/include/components/media/publication-page.h']['edits'][1]
        import os
        import runpy
        from unittest.mock import patch as environment_patch
        with tempfile.TemporaryDirectory() as empty:
            env = type('Env', (), {'subst': lambda self, value: empty})()
            with environment_patch.dict(os.environ, {'CROSSCUB_PRIVATE_EVIDENCE': ''}):
                overlay = runpy.run_path(str(ROOT/'scripts/patch_wolfssl.py'),
                                        init_globals={'Import': lambda value: None, 'env': env})
            http = overlay['sdk_secure_net_outputs'](ROOT/'freeink-sdk')[
                'libs/network/SecureNet/include/SecureHttpClient.h'].decode()
        start = http.index('    size = std::accumulate(_headers.begin()')
        end = http.index('\n', http.index('});', start))
        http_new = http[start:end]
        source = r'''
#include <numeric>
#include <iterator>
#include <string>
#include <vector>
#include <cstdint>
#include <limits>
#include <cassert>
struct Target { int calls = 0; int16_t lineHeight(int) { ++calls; return 32760; } };
struct Frame { Target value; Target& target() { return value; } };
struct Availability { struct { int font = 0; } detailText; int16_t gap = 22; };
int16_t nonnegative(int16_t value) { return value < 0 ? 0 : value; }
bool present(const char* value) { return value && value[0]; }
int main() {
  for (int seed : {0, 10, 32760, -32760}) {
    Frame frame; Availability av; const char* details[] = {nullptr, "", "a", "b"};
    int16_t availabilityH = seed;
    ORIGINAL_PUBLICATION
    const int16_t expected = availabilityH;
    const int calls = frame.value.calls;
    availabilityH = seed; frame.value.calls = 0;
    NEW_PUBLICATION
    assert(availabilityH == expected && frame.value.calls == calls && calls == 2);
  }
  for (size_t seed : {size_t(0), size_t(256), std::numeric_limits<size_t>::max()-3}) {
    std::vector<std::string> _headers = {"", "Host: test", "Cookie: example"};
    size_t size = seed;
    for (const std::string& h : _headers) size += h.size();
    const size_t expected = size;
    size = seed;
    NEW_HTTP
    assert(size == expected);
  }
}
'''.replace('ORIGINAL_PUBLICATION', old).replace('NEW_PUBLICATION', new).replace('NEW_HTTP', http_new)
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            result = self.compile(directory, source)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(subprocess.run([str(directory/'probe')], timeout=10).returncode, 0)

    def test_sdk_full_host_suite_after_local_repairs(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            result = self.compile(directory, '#include "'+str(self.sdk/'test/host/test_freeinkui.cpp')+'"\n')
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(directory/'probe')], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)


if __name__ == '__main__':
    unittest.main()
