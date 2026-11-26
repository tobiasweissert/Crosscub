// Production geometry methods are extracted unchanged by run.py. The deterministic
// font/metrics adapter below is NOT real-font, SDK renderer or physical-panel proof.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
constexpr int UI_12_FONT_ID = 12;
enum class EpdFontFamily { REGULAR };
struct Rect {
  int x, y, width, height;
};
struct Metrics {
  int sideButtonHintsWidth = 0, keyboardTextFieldWidthPercent = 90, verticalSpacing = 4, topPadding = 0,
      keyboardVerticalOffset = 0, keyboardKeySpacing = 4, keyboardKeyHeight = 40, keyboardWidthPercent = 95,
      buttonHintsHeight = 24;
  bool keyboardCenteredText = false;
};
struct UITheme {
  Metrics metrics;
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  const Metrics& getMetrics() { return metrics; }
};
struct FakeRenderer {
  int width = 480, height = 800;
  struct Inversion {
    int x, y, w, h;
  };
  std::vector<Inversion> inverted;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  int getLineHeight(int) const { return 20; }
  int getTextAdvanceX(int, const char* s, EpdFontFamily) const {
    int width = 0;
    for (; *s; ++s)
      if ((uint8_t(*s) & 0xc0) != 0x80) width += 8;
    return width;
  }
  int getTextWidth(int font, const char* s) const { return getTextAdvanceX(font, s, EpdFontFamily::REGULAR); }
  void invertRect(int x, int y, int w, int h) { inverted.push_back({x, y, w, h}); }
};
namespace BidiUtils {
inline int detectParagraphLevel(const char*, int, int) { return 0; }
}  // namespace BidiUtils
int gpio = 0;
bool deviceUsesSideButtonHintGutters(int) { return false; }
struct Input {};
struct TouchHeaderBackButton {
  static int height(const Metrics&, const Input&) { return 64; }
  struct Layout {
    Rect touchRect;
  };
  static Layout layout(Rect) { return {{0, 0, 48, 64}}; }
};
namespace freeink::ui {
using Rect = ::Rect;
}
namespace fui = freeink::ui;
namespace keyboard_layouts {
constexpr int COUNT = 1;
struct Item {
  int id;
};
Item ALL[] = {{0}};
int startingLayout() { return 0; }
unsigned enabled() { return 1; }
}  // namespace keyboard_layouts
enum class InputType { Text, Password, Url };
struct KeyboardEntryActivity {
  FakeRenderer renderer;
  Input mappedInput;
  std::string text;
  size_t cursorPos = 0, snapshotSelection = std::string::npos;
  bool snapshotMasked = false, passwordVisible = false, cursorMode = false, symbols = false, showLangKey = false;
  InputType inputType = InputType::Text;
  int snapshotTop = 144, layoutId = 0;
  struct Layout {
    int rowCount = 5;
  };
  Layout currentLayout() const { return {}; }
  enum class InputFieldTouchTarget { None, Cursor, PasswordToggle };
  static size_t utf8Prev(const std::string&, size_t);
  static size_t utf8Next(const std::string&, size_t);
  std::string displayTextForCurrentState() const;
  int measureRange(std::string&, int, int) const;
  bool rangeIsRtl(std::string&, int, int) const;
  int lineBreakEnd(std::string&, int, int) const;
  int inputStartY() const;
  int inputViewportStart(std::string&, int) const;
  fui::Rect keyboardRect() const;
  InputFieldTouchTarget inputFieldTouchTargetFromPoint(int, int, size_t&) const;
  int snapshotTouch(size_t, bool, int, int, int = -1, bool = false, int = 0);
  int snapshotVertical(size_t, bool, int, int = -1, bool = false, int = 0);
  void selectionLine(std::string displayText, int lineStartIdx, int lineEndIdx, int inputTopY, int inputHeight) {
    const int lineHeight = 20, lineStartX = 24, textWidth = measureRange(displayText, lineStartIdx, lineEndIdx);
    const bool isRtl = false;
#include "selection-production.inc"
  }
};
#include "layout-production.inc"
int main() {
  for (auto size : {std::pair<int, int>{480, 800}, {800, 480}}) {
    KeyboardEntryActivity k;
    k.renderer.width = size.first;
    k.renderer.height = size.second;
    for (bool centered : {false, true}) {
      UITheme::getInstance().metrics.keyboardCenteredText = centered;
      k.snapshotSelection = 0;
      k.text.clear();
      assert(k.snapshotVertical(0, false, -1) == 0);
      assert(k.snapshotVertical(0, false, 1) == 0);
      k.text = "abc";
      assert(k.snapshotVertical(k.text.size(), false, -1) == 3);
      assert(k.snapshotVertical(k.text.size(), false, 1) == 3);
      const int columns = (size.first * 90 / 100) / 8;
      k.text = std::string(columns + 3, 'a');
      assert(k.snapshotVertical(2, false, -1) == 2);                       // first wrapped line: Up stays put
      assert(k.snapshotVertical(columns + 2, false, 1) == columns + 2);    // last wrapped line
      assert(k.snapshotVertical(k.text.size(), false, 1) == columns + 3);  // final caret
      if (!centered) {
        assert(k.snapshotVertical(2, false, 1) == columns + 2);
        assert(k.snapshotVertical(columns + 2, false, -1) == 2);
      }
      k.text = "abc\nxyz";
      assert(k.snapshotVertical(3, false, 1) == 7);  // newline caret belongs to preceding line
      assert(k.snapshotVertical(4, false, -1) == 0);
      assert(k.snapshotVertical(7, false, 1) == 7);
      k.text = "abc\n";
      assert(k.snapshotVertical(3, false, 1) == 4);  // trailing newline has an empty last line
      assert(k.snapshotVertical(4, false, 1) == 4);
      if (!centered) assert(k.snapshotVertical(4, false, -1) == 0);
      k.text = "ü🙂\nö🙂";
      assert(k.snapshotVertical(k.text.size(), false, 1) == 13);
      assert(k.snapshotVertical(k.text.size(), false, -1) == 6);
      assert(k.snapshotVertical(7, false, -1) == 0);
      assert(k.snapshotVertical(6, false, 1) == 13);
      assert(k.snapshotSelection == 0);  // navigation leaves the selection anchor alone
    }
    UITheme::getInstance().metrics.keyboardCenteredText = false;
    k.text = "Aü🙂\n\nZ\n";
    auto s = k.text;
    assert(k.lineBreakEnd(s, 0, 400) == 7);
    assert(k.lineBreakEnd(s, 8, 400) == 8);
    assert(k.snapshotVertical(0, false, 1, -1, false, 144) == 8);
    assert(k.snapshotVertical(8, false, 1, -1, false, 144) == 9);
    assert(k.snapshotVertical(k.text.size(), false, -1, -1, false, 144) == 9);
    k.text = std::string(3072, 'a');
    s = k.text;
    const int width = size.first * 90 / 100;
    for (size_t cursor : {size_t(0), size_t(1536), k.text.size()}) {
      k.cursorPos = cursor;
      int first = k.inputViewportStart(s, width);
      assert(first >= 0 && static_cast<size_t>(first) <= cursor);
      size_t position = 9999;
      assert(k.inputFieldTouchTargetFromPoint(size.first / 2, 154, position) ==
             KeyboardEntryActivity::InputFieldTouchTarget::Cursor);
      assert(position <= k.text.size());
      const int next = k.snapshotVertical(cursor, false, 1, -1, false, 144);
      assert(next >= 0 && static_cast<size_t>(next) <= k.text.size());
    }
    k.text = "ü🙂Z";
    k.cursorPos = 3;
    assert(k.snapshotTouch(1, false, size.first / 2, 154, -1, false, 144) >= 0);
    assert(k.cursorPos == 0);  // mid-code-point request snapped back
    k.snapshotMasked = true;
    assert(k.displayTextForCurrentState() == std::string(k.text.size(), '*'));
    k.snapshotMasked = false;
    k.snapshotSelection = 0;
    k.cursorPos = 6;
    k.renderer.inverted.clear();
    k.selectionLine(k.text, 0, 6, 144, 0);
    assert(k.renderer.inverted.size() == 1 && k.renderer.inverted[0].w == 16 && k.renderer.inverted[0].y == 144);
    k.snapshotSelection = 6;
    k.cursorPos = 0;
    k.renderer.inverted.clear();
    k.selectionLine(k.text, 0, 6, 144, 0);
    assert(k.renderer.inverted.size() == 1 && k.renderer.inverted[0].w == 16);
    k.text = "\n";
    k.cursorPos = 1;
    k.snapshotSelection = 0;
    k.renderer.inverted.clear();
    k.selectionLine(k.text, 0, 0, 144, 0);
    assert(k.renderer.inverted.size() == 1 && k.renderer.inverted[0].w == 6);
  }
  puts(
      "PASS actual production keyboard UTF-8/wrap/newline/viewport/touch/vertical/selection-region methods; "
      "deterministic font adapter only");
}
