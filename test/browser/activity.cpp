// Compile the production activity adapter. Only hardware and VM construction
// are host boundaries; browser Lua, HTTP dispatch/resources, work hook, locks,
// mapped Back and global Home routing are the shipped implementations.
#include <GfxRenderer.h>
#include <HttpResource.h>
#include <LuaHttp.h>
#include <MappedInputManager.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "BrowserWork.h"
#include "LuaRuntime.h"
static std::string root, body;
static void (*producer)(void*) = nullptr;
static void* argument = nullptr;
static bool spawn(void (*fn)(void*), void* arg) {
  assert(!producer);
  producer = fn;
  argument = arg;
  return true;
}
static void execute(void*, ink::http::Job& j) {
  for (size_t o = 0; o < body.size(); o += 2048)
    assert(j.resource.append(body.data() + o, std::min(size_t(2048), body.size() - o)));
  j.result.size = body.size();
  j.result.status = 200;
  strcpy(j.result.url, "https://example.org/candidate");
}
static void complete(const std::string& value) {
  assert(producer);
  body = value;
  auto fn = producer;
  producer = nullptr;
  fn(argument);
}
class HostRunner {
 public:
  static HostRunner* live;
  ink::http::Session session{{nullptr, execute, spawn}};
  lua_State* vm = nullptr;
  std::atomic<bool> nativeCancel{false};
  bool exit = false, error = false, redraw = true;
  std::string message;
  std::mutex holdMutex;
  std::condition_variable holdCondition;
  bool hold = false, entered = false;
  struct Heap {
    size_t used = 0, peak = 0;
    static void* alloc(void* ctx, void* p, size_t old, size_t n) {
      auto& h = *static_cast<Heap*>(ctx);
      if (!p) old = 0;
      if (!n) {
        free(p);
        h.used -= old;
        return nullptr;
      }
      if (n > 4194304 - (h.used - old)) return nullptr;
      auto* q = realloc(p, n);
      if (q) {
        h.used = h.used - old + n;
        h.peak = std::max(h.peak, h.used);
      }
      return q;
    }
  } heap;
  HostRunner(GfxRenderer&, MappedInputManager&, const std::string&, const std::string&) { live = this; }
  ~HostRunner() { shutdown(); }
  bool init() {
    vm = lua_newstate(Heap::alloc, &heap);
    assert(vm);
    assert(ink::runtime::openEssential(vm) == LUA_OK);
    assert(ink::runtime::openNetwork(vm, true) == LUA_OK);
    assert(ink::browser::openBrowserWork(vm) == LUA_OK);
    session.allowed = true;
    session.domains.publicHttps = true;
    session.cooperative = true;
    snprintf(session.resourceRoot, sizeof(session.resourceRoot), "%s", root.c_str());
    lua_newtable(vm);
    ink::http::registerApi(vm, session);
    // Controlled 0.2 ms per real native read, explicitly not physical SD timing.
    lua_getfield(vm, -1, "http_read");
    lua_pushcclosure(
        vm,
        [](lua_State* state) {
          int args = lua_gettop(state);
          std::this_thread::sleep_for(std::chrono::microseconds(200));
          lua_pushvalue(state, lua_upvalueindex(1));
          lua_insert(state, 1);
          lua_call(state, args, LUA_MULTRET);
          return lua_gettop(state);
        },
        1);
    lua_setfield(vm, -2, "http_read");
    lua_setglobal(vm, "http_api");
    lua_newtable(vm);
    ink::browser::registerWork(vm);
    lua_setglobal(vm, "browser_native");
    lua_pushstring(vm, root.c_str());
    lua_setglobal(vm, "test_root");
    lua_register(vm, "test_fs_list", (+[](lua_State* L) {
                   lua_newtable(L);
                   int i = 1;
                   for (const auto& entry : std::filesystem::directory_iterator(root)) {
                     auto name = entry.path().filename().string();
                     lua_pushlstring(L, name.data(), name.size());
                     lua_rawseti(L, -2, i++);
                   }
                   return 1;
                 }));
    lua_register(vm, "test_fs_remove", (+[](lua_State* L) {
                   std::error_code error;
                   bool removed = std::filesystem::remove(luaL_checkstring(L, 1), error);
                   lua_pushboolean(L, removed && !error);
                   return 1;
                 }));
    lua_register(vm, "test_read", (+[](lua_State* L) {
                   auto* path = luaL_checkstring(L, 1);
                   auto o = luaL_checkinteger(L, 2), n = luaL_checkinteger(L, 3);
                   assert(o >= 0 && n >= 0 && n <= 65537);
                   std::ifstream f(path, std::ios::binary);
                   if (!f) {
                     lua_pushnil(L);
                     return 1;
                   }
                   std::string s(n, '\0');
                   f.seekg(o);
                   f.read(s.data(), n);
                   lua_pushlstring(L, s.data(), f.gcount());
                   return 1;
                 }));
    return true;
  }
  bool loadScript(const std::string&) {
    assert(luaL_dofile(vm, "test/browser/activity.lua") == LUA_OK);
    lua_getglobal(vm, "dofile");
    lua_pushliteral(vm, "main.lua");
    check(lua_pcall(vm, 1, 0, 0));
    return true;
  }
  void check(int result) {
    if (result != LUA_OK) {
      fprintf(stderr, "VM: %s\n", lua_tostring(vm, -1));
      abort();
    }
  }
  void call(const char* name) {
    lua_getglobal(vm, name);
    check(lua_pcall(vm, 0, 0, 0));
  }
  void textCall(const char* name, const char* value) {
    lua_getglobal(vm, name);
    lua_pushstring(vm, value);
    check(lua_pcall(vm, 1, 0, 0));
  }
  std::string stringCall(const char* name) {
    lua_getglobal(vm, name);
    check(lua_pcall(vm, 0, 1, 0));
    auto s = std::string(lua_tostring(vm, -1));
    lua_pop(vm, 1);
    return s;
  }
  bool hasActiveTouchHold() { return false; }
  void onTouchRelease() { assert(false && "Browser has no active long-press"); }
  bool networkEnabled() { return false; }
  bool cooperative = true;
  bool cooperativeBrowser() { return cooperative; }
  bool preventAutoSleep() { return false; }
  bool shouldExit() { return exit; }
  bool hasError() { return error; }
  const std::string& getErrorMessage() { return message; }
  void requestNativeCancel() {
    nativeCancel.store(true);
    session.requestCancel();
  }
  void clearNativeCancel() { nativeCancel.store(false); }
  void onInit() { call("on_init"); }
  void onUpdate() {
    check(ink::http::dispatch(vm, session));
    lua_gc(vm, LUA_GCSTEP, 0);
    call("on_update");
  }
  void onDraw() {
    call("on_draw");
    lua_gc(vm, LUA_GCSTEP, 50);
    std::unique_lock lock(holdMutex);
    if (hold) {
      entered = true;
      holdCondition.notify_all();
      holdCondition.wait(lock, [&] { return !hold; });
    }
  }
  void onButton(const char* key, bool pressed) {
    lua_getglobal(vm, "on_button");
    lua_pushstring(vm, key);
    lua_pushboolean(vm, pressed);
    check(lua_pcall(vm, 2, 0, 0));
  }
  void onTap(int x, int y) {
    lua_getglobal(vm, "on_tap");
    lua_pushinteger(vm, x);
    lua_pushinteger(vm, y);
    check(lua_pcall(vm, 2, 0, 0));
  }
  void onTouch(int, int, const char*) {}
  bool hasLongPressHandler() { return false; }
  void onLongPress(int, int) {}
  void onSwipe(const char*) {}
  bool checkAndClearRedrawRequested() { return true; }
  bool checkAndClearFullRefreshRequested() { return false; }
  void onExit() { call("on_exit"); }
  void shutdown() {
    session.close();
    if (vm) {
      lua_close(vm);
      vm = nullptr;
      assert(heap.used == 0);
    }
  }
};
HostRunner* HostRunner::live = nullptr;
#include "LuaAppActivity.h"
#include "activities/GlobalHomeDispatch.h"
using Adapter = LuaAppActivityAdapter<HostRunner>;
static double maximum = 0, maxBack = 0;
static void tick(Adapter& activity, MappedInputManager& input) {
  auto start = std::chrono::steady_clock::now();
  activity.loop();
  input.back = false;
  activity.render(RenderLock{});
  maximum =
      std::max(maximum, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}
static void ticks(Adapter& activity, MappedInputManager& input, int n = 50) {
  for (int i = 0; i < n; ++i) tick(activity, input);
}
static void navigate(Adapter& activity, MappedInputManager& input) {
  auto& runner = *HostRunner::live;
  runner.call("test_address");
  ticks(activity, input, 5);
  runner.textCall("test_edit", "https://example.org/candidate");
  ticks(activity, input, 5);
  assert(producer);
}
int main() {
  char temp[256];
  snprintf(temp, sizeof(temp), "%s/browser-activity-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
  assert(mkdtemp(temp));
  root = temp;
  GfxRenderer renderer;
  MappedInputManager input;
  Adapter activity(renderer, input, "apps/browser", "Browser");
  activity.onEnter();
  ticks(activity, input);
  // Ordinary Notes runners also revoke out of band, without waiting for Lua.
  HostRunner::live->cooperative = false;
  activity.onInputLockChanged(true);
  assert(HostRunner::live->nativeCancel.load());
  HostRunner::live->clearNativeCancel();
  activity.prepareForSleep();
  assert(HostRunner::live->nativeCancel.load());
  HostRunner::live->clearNativeCancel();
  activity.onFrontlightPanelOpened();
  assert(HostRunner::live->nativeCancel.load());
  HostRunner::live->clearNativeCancel();
  assert(!activity.handleHomeGesture() && HostRunner::live->nativeCancel.load());
  HostRunner::live->clearNativeCancel();
  HostRunner::live->cooperative = true;
  navigate(activity, input);
  complete("<article><h1>OLD READABLE PAGE</h1><p>Retained contents</p></article>");
  ticks(activity, input, 100);
  auto& runner = *HostRunner::live;
  assert(runner.stringCall("test_text").find("OLD READABLE PAGE") != std::string::npos);
  auto oldIndex = runner.stringCall("test_index");
  const std::string heavy = "<head><style>p{font-weight:bold}</style><script>" + std::string(600000, 'x') +
                            "</script></head><article><div data-long='" + std::string(150000, 'y') +
                            "'><h1>CANCELLED NEW PAGE</h1><p>Useful final tail</p></div></article>";
  for (int cancelTick : {0, 1, 5, 20}) {
    navigate(activity, input);
    complete(heavy);
    ticks(activity, input, cancelTick);
    auto start = std::chrono::steady_clock::now();
    input.back = true;
    tick(activity, input);
    auto ack = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    maxBack = std::max(maxBack, ack);
    assert(ack < 100);
    ticks(activity, input, 30);
    assert(runner.stringCall("test_index") == oldIndex);
    assert(runner.stringCall("test_text").find("OLD READABLE PAGE") != std::string::npos);
  }
  // Cancel after verified copy and immediately before checked index publication.
  navigate(activity, input);
  complete("<article><h1>PREPUBLICATION CANDIDATE</h1><p>Tail</p></article>");
  bool reached = false;
  for (int i = 0; i < 500; ++i) {
    tick(activity, input);
    if (runner.stringCall("test_stage") == "Publishing") {
      reached = true;
      break;
    }
  }
  assert(reached);
  input.back = true;
  tick(activity, input);
  ticks(activity, input);
  assert(runner.stringCall("test_index") == oldIndex);
  navigate(activity, input);
  complete(heavy);
  tick(activity, input);
  // Hold an actual render invocation while the production adapter owns LuaMutex.
  {
    std::lock_guard lock(runner.holdMutex);
    runner.hold = true;
    runner.entered = false;
  }
  std::thread render([&] { activity.render(RenderLock{}); });
  {
    std::unique_lock lock(runner.holdMutex);
    assert(runner.holdCondition.wait_for(lock, std::chrono::seconds(2), [&] { return runner.entered; }));
  }
  input.back = true;
  auto start = std::chrono::steady_clock::now();
  activity.loop();
  input.back = false;
  double contention = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  assert(contention < 50 && runner.nativeCancel.load());
  bool home = false;
  assert(dispatchHomeToActivity(activity, [&] { home = true; }));
  assert(home && runner.nativeCancel.load());
  {
    std::lock_guard lock(runner.holdMutex);
    runner.hold = false;
    runner.holdCondition.notify_all();
  }
  render.join();
  tick(activity, input);
  // A real Cancel touch must latch while render owns Lua; it cannot be lost.
  navigate(activity, input);
  complete(heavy);
  tick(activity, input);
  auto point = runner.stringCall("test_cancel_point");
  sscanf(point.c_str(), "%d:%d", &input.tapX, &input.tapY);
  {
    std::lock_guard lock(runner.holdMutex);
    runner.hold = true;
    runner.entered = false;
  }
  std::thread touchRender([&] { activity.render(RenderLock{}); });
  {
    std::unique_lock lock(runner.holdMutex);
    assert(runner.holdCondition.wait_for(lock, std::chrono::seconds(2), [&] { return runner.entered; }));
  }
  runner.clearNativeCancel();
  input.tap = true;
  start = std::chrono::steady_clock::now();
  activity.loop();
  input.tap = false;
  double touchMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  bool latched = runner.nativeCancel.load();
  {
    std::lock_guard lock(runner.holdMutex);
    runner.hold = false;
    runner.holdCondition.notify_all();
  }
  touchRender.join();
  assert(latched && touchMs < 50);
  ticks(activity, input, 30);
  assert(runner.stringCall("test_index") == oldIndex);
  // Worker cancellation must be signalled before LuaMutex becomes available.
  navigate(activity, input);
  {
    std::lock_guard lock(runner.holdMutex);
    runner.hold = true;
    runner.entered = false;
  }
  std::thread networkRender([&] { activity.render(RenderLock{}); });
  {
    std::unique_lock lock(runner.holdMutex);
    assert(runner.holdCondition.wait_for(lock, std::chrono::seconds(2), [&] { return runner.entered; }));
  }
  input.back = true;
  activity.loop();
  input.back = false;
  bool workerCancelled = runner.session.job()->cancel.load();
  {
    std::lock_guard lock(runner.holdMutex);
    runner.hold = false;
    runner.holdCondition.notify_all();
  }
  networkRender.join();
  assert(workerCancelled && "HTTP cancel waited for LuaMutex");
  tick(activity, input);
  complete(heavy);
  ticks(activity, input, 10);
  // A held HTTP producer retains its native slot after Back; UI can queue one
  // new navigation and must wait until the original owner drains.
  navigate(activity, input);
  input.back = true;
  tick(activity, input);
  assert(ink::http::Session::busy());
  runner.call("test_address");
  ticks(activity, input, 5);
  runner.textCall("test_edit", "https://example.org/next");
  ticks(activity, input, 5);
  assert(producer && ink::http::Session::busy());
  complete(heavy);
  ticks(activity, input, 5);
  assert(producer);
  complete("<article><h1>OLD READABLE PAGE</h1><p>Fresh request after drain</p></article>");
  ticks(activity, input, 100);
  assert(runner.stringCall("test_text").find("OLD READABLE PAGE") != std::string::npos);
  // Real paused parse -> Home teardown, stale completion cannot target reentry.
  navigate(activity, input);
  complete(heavy);
  tick(activity, input);
  home = false;
  auto homeStart = std::chrono::steady_clock::now();
  dispatchHomeToActivity(activity, [&] {
    home = true;
    activity.onExit();
  });
  assert(home);
  double homeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - homeStart).count();
  assert(homeMs < 100);
  Adapter reentered(renderer, input, "apps/browser", "Browser");
  reentered.onEnter();
  ticks(reentered, input, 100);
  assert(HostRunner::live->stringCall("test_text").find("CANCELLED NEW PAGE") == std::string::npos);
  // Production loop admits an ordinary menu touch before dispatching HTTP.
  navigate(reentered, input);
  complete("<p>TOUCH ORDER PAGE</p>");
  auto menuPoint = HostRunner::live->stringCall("test_menu_point");
  sscanf(menuPoint.c_str(), "%d:%d", &input.tapX, &input.tapY);
  input.tap = true;
  tick(reentered, input);
  input.tap = false;
  ticks(reentered, input, 100);
  assert(HostRunner::live->stringCall("test_menu_open") == "yes");
  input.back = true;
  tick(reentered, input);
  ticks(reentered, input, 30);
  assert(HostRunner::live->stringCall("test_text").find("TOUCH ORDER PAGE") != std::string::npos);
  reentered.onExit();
  assert(!ink::http::Session::busy());
  std::filesystem::remove_all(root);
  printf(
      "PASS production activity/HTTP/Lua browser Back at begin/middle/prepublication; Home/reentry; held-render "
      "admission %.3fms; held-render Cancel touch %.3fms; max Back %.3fms; Home teardown %.3fms; max host tick "
      "%.3fms\n",
      contention, touchMs, maxBack, homeMs, maximum);
  assert(maximum < 50);
}
