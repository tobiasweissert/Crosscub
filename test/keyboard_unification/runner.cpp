// Host renderer/input fixture. Production KeyboardEntryActivity is compiled whole.
// Activity stack and render-task mutex are isolated below, not firmware navigation.
#include <EpdFontFamily.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <SDL.h>
#include <builtinFonts/inter_10_bold.h>
#include <builtinFonts/inter_10_regular.h>
#include <builtinFonts/inter_12_bold.h>
#include <builtinFonts/inter_12_regular.h>
#include <builtinFonts/inter_8_regular.h>
#include <builtinFonts/ui_symbols_10.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "activities/apps/lua/BrowserWork.h"
#include "activities/apps/lua/LuaRuntime.h"
#include "activities/reader/EpubReaderPercentSelectionActivity.h"
#include "activities/settings/FrontlightTimePickerActivity.h"
#include "activities/settings/KeyboardLayoutsActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "snapshot_lifetime.inc"
GfxRenderer renderer(display);
MappedInputManager mappedInput(gpio, renderer);
static bool finished = false;
void Activity::onEnter() {}
void Activity::onExit() {}
void Activity::requestUpdate(bool) {}
RequestUpdateResult Activity::requestUpdateAndWait() { return RequestUpdateResult::Rendered; }
void Activity::setResult(ActivityResult&& value) { result = std::move(value); }
void Activity::finish() { finished = true; }
static unsigned renderLockConstructions = 0;
RenderLock::RenderLock(Mode) { ++renderLockConstructions; }
RenderLock::RenderLock(Activity&) {}
RenderLock::~RenderLock() {}
void RenderLock::unlock() {}
bool RenderLock::peek() { return false; }
EpdFont normal10(&inter_10_regular), bold10(&inter_10_bold), normal12(&inter_12_regular), bold12(&inter_12_bold),
    small(&inter_8_regular), symbols(&ui_symbols_10);
EpdFontFamily family10(&normal10, &bold10, nullptr, nullptr, &symbols), family12(&normal12, &bold12),
    smallFamily(&small);
// Generated from exact production LuaRunner callback bodies. See run.py bindings.
#include "lua_adapter.inc"
static void code(lua_State* L, const char* s) {
  if (luaL_dostring(L, s) != LUA_OK) {
    fprintf(stderr, "Lua: %s\n", lua_tostring(L, -1));
    exit(2);
  }
}
static int fixtureCapture(lua_State* L) {
  const std::string id = luaL_checkstring(L, 1);
  for (char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
      return luaL_error(L, "invalid capture id");
  const char* prefix = getenv("KEYBOARD_CAPTURE_PREFIX");
  if (!prefix) return luaL_error(L, "capture output missing");
  const std::string path = std::string(prefix) + "--" + id + ".pbm";
  std::ofstream out(path, std::ios::binary);
  assert(out);
  out << "P4\n" << display.getDisplayWidth() << " " << display.getDisplayHeight() << "\n";
  const auto* bytes = display.getFrameBuffer();
  for (size_t i = 0; i < display.getBufferSize(); ++i) out.put(char(bytes[i] ^ 0xff));
  out.close();
  assert(out);
  printf("CAPTURE_FRAME %s %s\n", id.c_str(), path.c_str());
  return 0;
}
static int fixtureRead(lua_State* L) {
  const std::string name = luaL_checkstring(L, 1);
  if (name.rfind("art/", 0) != 0 || name.find("..") != std::string::npos) {
    lua_pushnil(L);
    return 1;
  }
  std::ifstream stream(name, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(stream)), {});
  if (!stream || bytes.size() > 30000) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushlstring(L, bytes.data(), bytes.size());
  return 1;
}
static int fixtureUpvalue(lua_State* L) {
  const char* wanted = luaL_checkstring(L, 2);
  for (int i = 1;; ++i) {
    const char* name = lua_getupvalue(L, 1, i);
    if (!name) break;
    if (strcmp(name, wanted) == 0) {
      if (lua_gettop(L) > 3) {
        lua_pop(L, 1);
        lua_pushvalue(L, 3);
        lua_setupvalue(L, 1, i);
        lua_pushboolean(L, true);
      }
      return 1;
    }
    lua_pop(L, 1);
  }
  return luaL_error(L, "missing fixture upvalue %s", wanted);
}
template <class A>
static void key(A& a, SDL_Scancode scan, bool down) {
  SDL_Event e{};
  e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
  e.key.keysym.scancode = scan;
  SDL_PushEvent(&e);
  gpio.beginFrame();
  gpio.update();
  a.loop();
}
// These implementation fragments share declarations and the SETTINGS macro.
// In particular, caller_behavior.inc undefines SETTINGS after closure uses it.
// clang-format off
#include "native_behavior.inc"
#include "numeric_behavior.inc"
#include "closure_behavior.inc"
#include "caller_behavior.inc"
#include "lcp_behavior.inc"
#include "snapshot_behavior.inc"
// clang-format on
int main(int argc, char** argv) {
  assert(argc >= 4);
  std::string mode = argv[1], app = argv[2];
  int orient = atoi(argv[3]);
  assert(Storage.begin());
  halClock.begin();
  renderer.begin();
  display.begin();
  gpio.begin();
  renderer.insertFont(UI_10_FONT_ID, family10);
  renderer.insertFont(UI_12_FONT_ID, family12);
  renderer.insertFont(SMALL_FONT_ID, smallFamily);
  // The neutral fixture header uses the actual existing UI12 family when Lexend
  // is not loaded. Key labels themselves always use UI12 (never this alias).
  renderer.insertFont(LEXENDDECA_16_FONT_ID, family12);
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orient));
  if (mode.rfind("caller-", 0) == 0) {
    if (mode == "caller-native.opds.lcp-passphrase")
      lcpBehavior();
    else
      callerBehavior(mode.substr(7));
    renderer.displayBuffer();
    for (int i = 0; i < 10; ++i) {
      display.presentIfNeeded();
      delay(50);
    }
    return 0;
  }
  if (mode.rfind("snapshot-", 0) == 0) {
    snapshotLifetime(mode);
    renderer.displayBuffer();
    for (int i = 0; i < 10; ++i) {
      display.presentIfNeeded();
      delay(50);
    }
    return 0;
  }
  if (mode.rfind("drawer-font-", 0) == 0) {
    drawerFontBehavior(mode.find("page") != std::string::npos, mode.find("large") != std::string::npos);
    renderer.displayBuffer();
    for (int i = 0; i < 10; ++i) {
      display.presentIfNeeded();
      delay(50);
    }
    return 0;
  }
  if (mode == "drawer-percent" || mode == "drawer-page") {
    drawerBehavior(mode == "drawer-page");
    renderer.displayBuffer();
    for (int i = 0; i < 10; ++i) {
      display.presentIfNeeded();
      delay(50);
    }
    return 0;
  }
  if (mode == "layout-settings") {
    KeyboardLayoutsActivity owner(renderer, mappedInput);
    owner.onEnter();
    owner.workingMask = 1;
    owner.render(RenderLock{});
    auto tapRow = [&](int value) {
      owner.render(RenderLock{});
      const auto r = numericRect(owner.app.interactions_, value);
      mouse(owner, r.x + r.width / 2, r.y + r.height / 2, true);
      mouse(owner, r.x + r.width / 2, r.y + r.height / 2, false);
    };
    tapRow(0);
    assert(owner.workingMask == 1);
    tapRow(1);
    assert(owner.workingMask == 3);
    tapRow(0);
    assert(owner.workingMask == 2);
    tapRow(1);
    assert(owner.workingMask == 2);
    owner.render(RenderLock{});
    puts("LAYOUT_SETTINGS_PASS actual_HAL_toggle last_Latin_locked no_persistence");
    renderer.displayBuffer();
    for (int i = 0; i < 20; ++i) {
      display.presentIfNeeded();
      delay(50);
    }
    return 0;
  }
  if (mode.rfind("numeric-", 0) == 0) {
    const bool equal = numericBehavior(mode);
    renderer.displayBuffer();
    for (int i = 0; i < 20; ++i) {
      display.presentIfNeeded();
      delay(50);
    }
    return equal ? 0 : 1;
  }
  if (mode.rfind("native", 0) == 0) {
    if (mode == "native-long" || mode == "native-rtl") {
      nativeTraversal(mode == "native-rtl");
      renderer.displayBuffer();
      for (int i = 0; i < 20; ++i) {
        display.presentIfNeeded();
        delay(50);
      }
      return 0;
    }
    if (mode != "native") {
      int language = 0, layer = 0;
      assert(sscanf(mode.c_str(), "native-layout-%d-%d", &language, &layer) == 2);
      assert(language >= 0 && language < keyboard_layouts::COUNT && layer >= 0 && layer <= 2);
      KeyboardEntryActivity layout(renderer, mappedInput, "Neutral layout", "", 512);
      layout.onEnter();
      layout.layoutId = keyboard_layouts::ALL[language].id;
      layout.shifted = layer == 1;
      layout.symbols = layer == 2;
      const auto& table = layout.currentLayout();
      const auto& first = table.rows[table.rowCount > 1 ? 1 : 0].keys[0];
      const char* output = freeink::ui::keyboardOutputFor(table, first.value);
      assert(output && *output);
      const std::string expected = output;
      tapValue(layout, first.value);
      assert(layout.text == expected);
      printf("NATIVE_LAYOUT_PASS language=%d layer=%d actual_HAL_utf8_bytes=%zu\n", language, layer, expected.size());
      layout.shifted = layer == 1;
      layout.symbols = layer == 2;
      layout.render(RenderLock{});
      renderer.displayBuffer();
      for (int i = 0; i < 20; ++i) {
        display.presentIfNeeded();
        delay(50);
      }
      return 0;
    }
    nativeBehavior();
    KeyboardEntryActivity a(renderer, mappedInput, "Neutral keyboard", "", 12, InputType::Text, 1);
    a.onEnter();
    // Before publication, no stale touch can route. Then exercise real HAL confirm.
    assert(!a.interactionsReady.load());
    a.render(RenderLock{});
    assert(a.interactionsReady.load());
    key(a, SDL_SCANCODE_RETURN, true);
    key(a, SDL_SCANCODE_RETURN, false);
    assert(a.text == "1");
    printf("NATIVE_HAL_CONFIRM text=%s first_paint=ready\n", a.text.c_str());
    a.render(RenderLock{});
  } else {
    lua_State* L = luaL_newstate();
    assert(L);
    assert(ink::runtime::openEssential(L) == LUA_OK);
    assert(ink::runtime::openNetwork(L, app.substr(app.find_last_of('/') + 1) == "browser") == LUA_OK);
    registerAdapter(L);
    if (app.substr(app.find_last_of('/') + 1) == "browser") {
      assert(ink::browser::openBrowserWork(L) == LUA_OK);
      lua_getglobal(L, "smudge");
      ink::browser::registerWork(L);
      lua_pop(L, 1);
    }
    lua_pushcfunction(L, fixtureUpvalue);
    lua_setglobal(L, "fixture_upvalue");
    lua_pushcfunction(L, fixtureCapture);
    lua_setglobal(L, "fixture_capture");
    assert(chdir(app.c_str()) == 0);
    if (mode.rfind("pad-", 0) == 0) {
      lua_pushcfunction(L, fixtureRead);
      lua_setglobal(L, "fixture_read");
      lua_pushstring(L, mode.c_str());
      lua_setglobal(L, "VARIANT");
      code(L, "smudge.read_file=fixture_read");
      if (luaL_dofile(L, argv[4]) != LUA_OK) {
        fprintf(stderr, "Pad: %s\n", lua_tostring(L, -1));
        return 2;
      }
      code(L, "px,py,pw,ph=PAD.x,PAD.y,PAD.w,PAD.h");
      auto number = [&](const char* n) {
        lua_getglobal(L, n);
        int v = lua_tointeger(L, -1);
        lua_pop(L, 1);
        return v;
      };
      const int x = number("px"), y = number("py"), w = number("pw"), h = number("ph");
      std::array<unsigned char, 48000> actual;
      memcpy(actual.data(), display.getFrameBuffer(), actual.size());
      freeink::ui::GfxRendererTarget target(renderer);
      target.setFont(freeink::ui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
      freeink::ui::InteractionBuffer<1> interactions;
      freeink::ui::Frame<1> frame(target, target.deviceContext(), {}, interactions);
      freeink::ui::KeyGridKey k;
      lua_getglobal(L, "PAD_LABEL");
      k.label = lua_tostring(L, -1);
      freeink::ui::KeyGridProps props;
      props.keys = &k;
      props.rows = 1;
      props.cols = 1;
      props.selectedIndex = number("PAD_SELECTED") ? 0 : -1;
      props.radius = freeink::ui::KeyboardProps{}.keyRadius;
      props.labelText.font = freeink::ui::GfxRendererTarget::FONT_BODY;
      renderer.fillRect(x, y, w, h, false);
      freeink::ui::keyGrid(frame, {int16_t(x), int16_t(y), int16_t(w), int16_t(h)}, props);
      bool equal = memcmp(actual.data(), display.getFrameBuffer(), actual.size()) == 0;
      lua_pop(L, 1);
      memcpy(display.getFrameBuffer(), actual.data(), actual.size());
      printf("PAD_NATIVE_KEY_PIXEL_EQUAL=%d\n", equal);
      fflush(stdout);
      renderer.displayBuffer();
      for (int i = 0; i < 20; ++i) {
        display.presentIfNeeded();
        delay(50);
      }
      lua_close(L);
      return equal ? 0 : 1;
    }
    if (mode == "fallback") code(L, "smudge.keyboard_key=nil;smudge.keyboard_metrics=nil;smudge.keyboard_snapshot=nil");
    if (mode == "nonsticky")
      code(L,
           "smudge.get_device=function()return 'x4' end;smudge.keyboard_key=function()error('non-Sticky native "
           "paint')end");
    if (mode == "lua")
      code(L,
           "assert(smudge.keyboard_metrics().label_font=='ui12');assert(not "
           "pcall(smudge.keyboard_key,'x',-1,0,20,20));assert(not "
           "pcall(smudge.keyboard_key,'x',0,0,math.maxinteger,20))");
    const unsigned locksBeforeSnapshot = renderLockConstructions;
    code(L, "U=dofile('ui.lua'); U.edit('Neutral keyboard','',12,function(v) result=v end); U.editDraw()");
    if (mode == "lua")
      assert(renderLockConstructions == locksBeforeSnapshot && "paint must not reacquire the render mutex");
    if (mode == "lua") {
      std::array<unsigned char, 48000> actual;
      memcpy(actual.data(), display.getFrameBuffer(), actual.size());
      KeyboardEntryActivity expected(renderer, mappedInput, "Neutral keyboard", "", 12);
      expected.onEnter();
      expected.render(RenderLock{});
      const bool fullEqual = memcmp(actual.data(), display.getFrameBuffer(), actual.size()) == 0;
      memcpy(display.getFrameBuffer(), actual.data(), actual.size());
      printf("LUA_NATIVE_FULL_FRAME_PIXEL_EQUAL=%d\n", fullEqual);
      fflush(stdout);
      assert(fullEqual);
      if (orient == 0) {
        for (int language = 0; language < keyboard_layouts::COUNT; ++language)
          for (int layer = 0; layer < 3; ++layer) {
            const std::string command =
                "U.editor.language=" + std::to_string(language) + ";U.editor.shift=" + (layer == 1 ? "true" : "false") +
                ";U.editor.numeric=" + (layer == 2 ? "true" : "false") + ";U.focus=1;U.editDraw()";
            code(L, command.c_str());
            memcpy(actual.data(), display.getFrameBuffer(), actual.size());
            KeyboardEntryActivity reference(renderer, mappedInput, "Neutral keyboard", "", 12);
            reference.onEnter();
            reference.layoutId = keyboard_layouts::ALL[language].id;
            reference.shifted = layer == 1;
            reference.symbols = layer == 2;
            reference.render(RenderLock{});
            bool matrixEqual = memcmp(actual.data(), display.getFrameBuffer(), actual.size()) == 0;
            memcpy(display.getFrameBuffer(), actual.data(), actual.size());
            printf("SNAPSHOT_MATRIX language=%d layer=%d equal=%d\n", language, layer, matrixEqual);
            fflush(stdout);
            assert(matrixEqual);
          }
        code(L, "U.closeEditor();U.edit('Neutral keyboard','',12,function(v)result=v end);U.editDraw()");
      }
    }
    // Explicit q lookup: native layout starts with the number row.
    code(L,
         "for i,v in ipairs(U.buttons) do if v[6]=='q' then b=v;bi=i;break end end; "
         "assert(b);bx,by,bw,bh=b[1],b[2],b[3],b[4]");
    auto number = [&](const char* n) {
      lua_getglobal(L, n);
      int v = lua_tointeger(L, -1);
      lua_pop(L, 1);
      return v;
    };
    int x = number("bx"), y = number("by"), w = number("bw"), h = number("bh");
    std::array<unsigned char, 48000> before;
    memcpy(before.data(), display.getFrameBuffer(), before.size());
    // Independently ask SDK keyGrid for the same focused cell, font and bounds.
    freeink::ui::GfxRendererTarget target(renderer);
    target.setFont(freeink::ui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
    freeink::ui::InteractionBuffer<1> interactions;
    freeink::ui::Frame<1> frame(target, target.deviceContext(), {}, interactions);
    freeink::ui::KeyGridKey k;
    k.label = "q";
    freeink::ui::KeyGridProps props;
    props.keys = &k;
    props.rows = 1;
    props.cols = 1;
    props.selectedIndex = number("bi") == 1 ? 0 : -1;
    props.radius = mode == "fallback" ? 0 : freeink::ui::KeyboardProps{}.keyRadius;
    props.labelText.font = freeink::ui::GfxRendererTarget::FONT_BODY;
    renderer.fillRect(x, y, w, h, false);
    freeink::ui::keyGrid(frame, {int16_t(x), int16_t(y), int16_t(w), int16_t(h)}, props);
    bool equal = memcmp(before.data(), display.getFrameBuffer(), before.size()) == 0;
    if (mode != "lua") printf("FALLBACK_KEYGRID_PIXEL_EQUAL=%d rect=%d,%d,%d,%d\n", equal, x, y, w, h);
    memcpy(display.getFrameBuffer(), before.data(), before.size());
    code(L,
         "assert(U.tap(bx+bw//2,by+bh//2));assert(U.editor.value=='q');U.editDraw(); for i,b in ipairs(U.buttons) do "
         "if b[6]=='Done' then U.tap(b[1]+b[3]//2,b[2]+b[4]//2);break end end;assert(result=='q' and U.editor==nil)");
    puts("LUA_PRODUCTION_TAP_DONE result=q");
    if (orient == 0 && argc > 4) {
      const auto name = app.substr(app.find_last_of('/') + 1);
      lua_pushstring(L, name.c_str());
      lua_setglobal(L, "APP");
      if (luaL_dofile(L, argv[4]) != LUA_OK) {
        fprintf(stderr, "Behavior: %s\n", lua_tostring(L, -1));
        return 2;
      }
    }
    code(L, "U.notice=''");
    // Red and Green frames deliberately show the same untouched opening state.
    code(L, "U.edit('Neutral keyboard','',12,function(v) result=v end);U.editDraw()");
    renderer.displayBuffer();
    for (int i = 0; i < 20; ++i) {
      display.presentIfNeeded();
      delay(50);
    }
    lua_close(L);
    return (mode == "nonsticky" || mode == "lua") ? 0 : (equal ? 0 : 1);
  }
  renderer.displayBuffer();
  for (int i = 0; i < 20; ++i) {
    display.presentIfNeeded();
    delay(50);
  }
  return 0;
}
