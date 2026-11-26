#pragma once

#include <BrowserImage.h>
#include <HalStorage.h>
#include <LuaHttp.h>
#include "network/NotesDictation.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "../../../../lib/AirDrop/browser/LuaLaunch.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

class GfxRenderer;
class MappedInputManager;

namespace ink {

class LuaRunner {
 public:
  static constexpr int LUA_API_VERSION = 7;

  LuaRunner(GfxRenderer& renderer, MappedInputManager& input, const std::string& appDir, const std::string& appId);
  ~LuaRunner();

  bool init();
  bool loadScript(const std::string& scriptPath);
  bool setBrowserLaunch(const airdrop::browser::LaunchHandle& launch, uint32_t attempt);
  void shutdown();

  // Lifecycle dispatches
  void onInit();
  void onUpdate();
  void onDraw();
  void onButton(const char* buttonName, bool isPressed);
  void onTouch(int x, int y, const char* eventType);
  void onTap(int x, int y);
  bool hasLongPressHandler();
  void onLongPress(int x, int y);
  bool hasTouchReleaseHandler();
  bool hasActiveTouchHold() const { return touchHoldActive_; }
  void onTouchRelease();
  void onSwipe(const char* dir);
  void onExit();

  bool checkAndClearRedrawRequested() { return redrawRequested_.exchange(false); }
  bool checkAndClearFullRefreshRequested() { return fullRefreshRequested_.exchange(false); }

  bool shouldExit() const { return requestedExit_; }
  bool hasError() const { return hasError_; }
  const std::string& getErrorMessage() const { return errorMessage_; }

  bool cooperativeBrowser() const { return browser_ && http_.cooperative; }
  void requestNativeCancel() {
    renderCancel_.store(true, std::memory_order_relaxed);
    dictation_.cancel();
    if (cooperativeBrowser()) {
      http_.requestCancel();
      images_.requestCancel();
    }
  }
  void clearNativeCancel() { renderCancel_.store(false, std::memory_order_relaxed); }
  bool networkEnabled() const { return http_.allowed; }
  const http::Domains& networkDomains() const { return http_.domains; }

  bool preventAutoSleep() const { return preventAutoSleep_.load(std::memory_order_relaxed); }
  void setPreventAutoSleep(bool prevent) { preventAutoSleep_.store(prevent, std::memory_order_relaxed); }

 private:
  GfxRenderer& renderer_;
  MappedInputManager& input_;
  std::string appDir_;
  std::string appId_;
  std::string saveDir_;

  NotesDictation dictation_;
  http::Session http_{http::platformExecutor()};
  bool resolvePath(const char* path, std::string& result) const;
  lua_State* L = nullptr;
  bool browser_ = false;
  std::atomic<bool> renderCancel_{false};
  browser::ImageSession images_;
  HalFile browserFile_, browserDirectory_;
  std::string browserDirectoryPath_;
  std::string browserFilePath_;
  size_t browserFileOffset_ = 0;
  std::atomic<bool> requestedExit_{false}, hasError_{false}, redrawRequested_{false}, fullRefreshRequested_{false};
  bool touchHoldActive_ = false;
  int touchHoldX_ = 0, touchHoldY_ = 0;
  std::atomic<bool> preventAutoSleep_{false};
  std::string errorMessage_;

  // Memory sandbox tracking
  size_t currentAllocatedBytes_ = 0;
  size_t maxLuaHeapBytes_ = 0;
  bool luaHeapUsesPsram_ = false;

  static void* customLuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize);

  void registerSmudgeApi();
  void setError(const char* msg);

  // C-to-Lua Bridge functions
  airdrop::browser::LuaLaunchBinding launchBinding_;
  static int l_browserList(lua_State* L);
  static int l_browserRead(lua_State* L);
  static int l_browserClose(lua_State* L);
  static int l_imageStart(lua_State* L);
  static int l_imagePoll(lua_State* L);
  static int l_imageCancel(lua_State* L);
  static int l_imageBusy(lua_State* L);
  static int l_imageBlit(lua_State* L);
  static int l_browserAdvance(lua_State* L);
  static int l_browserPath(lua_State* L);
  static int l_browserImage(lua_State* L);
  static int l_dofile(lua_State* L);
  static int l_clear(lua_State* L);
  static int l_refresh(lua_State* L);
  static int l_text(lua_State* L);
  static int l_centeredText(lua_State* L);
  static int l_textWidth(lua_State* L);
  static int l_wrappedText(lua_State* L);
  static int l_lineHeight(lua_State* L);
  static int l_pixel(lua_State* L);
  static int l_line(lua_State* L);
  static int l_thickLine(lua_State* L);
  static int l_rect(lua_State* L);
  static int l_rectDither(lua_State* L);
  static int l_roundedRect(lua_State* L);
  static int l_circle(lua_State* L);
  static int l_heart(lua_State* L);
  static int l_bitmap(lua_State* L);
  static int l_getBounds(lua_State* L);
  static int l_getMetrics(lua_State* L);
  static int l_buttonHints(lua_State* L);
  static int l_header(lua_State* L);
  static int l_save(lua_State* L);
  static int l_load(lua_State* L);
  static int l_readFile(lua_State* L);
  static int l_findSection(lua_State* L);
  static int l_fileExists(lua_State* L);
  static int l_exit(lua_State* L);
  static int l_log(lua_State* L);
  static int l_time(lua_State* L);
  static int l_getDate(lua_State* L);
  static int l_random(lua_State* L);
  static int l_millis(lua_State* L);
  static int l_requestUpdate(lua_State* L);
  static int l_isButtonDown(lua_State* L);
  static int l_drawSprite(lua_State* L);
  static int l_fullRefresh(lua_State* L);
  static int l_invertRect(lua_State* L);
  static int l_invertScreen(lua_State* L);
  static int l_triangle(lua_State* L);
  static int l_inRect(lua_State* L);
  static int l_hasTouch(lua_State* L);
  static int l_getBattery(lua_State* L);
  static int l_getDevice(lua_State* L);
  static int l_writeFile(lua_State* L);
  static int l_listFiles(lua_State* L);
  static int l_deleteFile(lua_State* L);
  static int l_popup(lua_State* L);
  static int l_getMemory(lua_State* L);
  static int l_setOrientation(lua_State* L);
  static int l_getOrientation(lua_State* L);
  static int l_preventSleep(lua_State* L);
  static int l_isPreventSleep(lua_State* L);
  static int l_getVersion(lua_State* L);
  static int l_getApiVersion(lua_State* L);
  static int l_hasFeature(lua_State* L);
};

}  // namespace ink
