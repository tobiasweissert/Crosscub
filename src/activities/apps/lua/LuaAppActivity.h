#pragma once

#include <HalLuaHttp.h>

#include <memory>
#include <mutex>
#include <string>

#include "../../../../lib/AirDrop/browser/BrowserLaunch.h"
#include "GfxRenderer.h"
#include "LuaTouchDispatch.h"
#include "MappedInputManager.h"
#include "Memory.h"
#include "activities/Activity.h"
#include "activities/apps/lua/LuaRunner.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

template <typename Runner = ink::LuaRunner>
class LuaAppActivityAdapter : public Activity {
 public:
  LuaAppActivityAdapter(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& appDir,
                        const std::string& appName, const std::string& entryScript = "main.lua",
                        airdrop::browser::LaunchHandle launch = {}, bool transferredWifi = false)
      : Activity(appName.c_str(), renderer, mappedInput),
        appDir_(appDir),
        appName_(appName),
        entryScript_(entryScript),
        launch_(std::move(launch)) {
    ownsWifi_ = transferredWifi;
    // Generate a simple appId from folder name
    size_t lastSlash = appDir_.find_last_of("/\\");
    if (lastSlash != std::string::npos && lastSlash + 1 < appDir_.length()) {
      appId_ = appDir_.substr(lastSlash + 1);
    } else {
      appId_ = appName_;
    }
  }

  void onEnter() override {
    Activity::onEnter();
    initialOrientation_ = renderer.getOrientation();
    if (launch_) {
      launchAttempt_ = launch_->beginAttempt();
      if (!launchAttempt_) {
        finish();
        return;
      }
    }
    runner_ = makeUniqueNoThrow<Runner>(renderer, mappedInput, appDir_, appId_);

    if (!runner_) {
      LOG_ERR("LUA", "Runner allocation failed");
      failLaunch(airdrop::browser::LaunchError::InitFailed);
      finish();
      return;
    }
    if (!runner_->init()) {
      failLaunch(airdrop::browser::LaunchError::InitFailed);
      if (launch_) {
        finish();
        return;
      }
      const std::string& err = runner_->getErrorMessage();
      renderError(err.empty() ? "Failed to initialize Lua VM" : err.c_str());
      return;
    }

    if (launch_) {
      if (!runner_->cooperativeBrowser()) {
        failLaunch(airdrop::browser::LaunchError::IncompatibleBrowser);
        finish();
        return;
      }
      if (!runner_->networkEnabled()) {
        failLaunch(airdrop::browser::LaunchError::NetworkUnavailable);
        finish();
        return;
      }
      // Existing mock runners and ordinary apps need no launch extension.
      if constexpr (requires(Runner& r) { r.setBrowserLaunch(launch_, launchAttempt_); }) {
        if (!runner_->setBrowserLaunch(launch_, launchAttempt_)) {
          failLaunch(airdrop::browser::LaunchError::IncompatibleBrowser);
          finish();
          return;
        }
      } else {
        failLaunch(airdrop::browser::LaunchError::IncompatibleBrowser);
        finish();
        return;
      }
    }
    prepareNetwork();
  }

  bool handleHomeGesture() override {
    if (runner_) runner_->requestNativeCancel();
    return false;  // ActivityManager's existing global Home path owns navigation.
  }

  void prepareForSleep() override {
    if (runner_) runner_->requestNativeCancel();
  }
  void onInputLockChanged(bool locked) override {
    if (locked && runner_) runner_->requestNativeCancel();
  }
  void onFrontlightPanelOpened() override {
    if (runner_) runner_->requestNativeCancel();
  }

  bool preventAutoSleep() override { return runner_ && runner_->preventAutoSleep(); }

  void loop() override {
    // Return handoff failures to the still-owning coordinator's Retry/Cancel UI.
    // Ordinary manually launched apps retain their existing error screen and Back flow.
    if (launch_ && scriptStarted_ && launch_->state() == airdrop::browser::LaunchState::Pending) {
      finish();
      return;
    }
    if (wifiPending_) {
      wifiPending_ = false;
      auto selector = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
      if (!selector) {
        LOG_ERR("LHTTP", "WiFi dialog allocation failed");
        failLaunch(airdrop::browser::LaunchError::NetworkUnavailable);
        finish();
        return;
      }
      ownsWifi_ = true;
      startActivityForResult(std::move(selector), [this](const ActivityResult& result) {
        if (result.isCancelled || !HalLuaHttp::wifiConnected()) {
          failLaunch(airdrop::browser::LaunchError::NetworkUnavailable);
          finish();
          return;
        }
        startScript();
      });
      return;
    }
    if (!scriptStarted_ && runner_ && !runner_->hasError()) return;
    if (!runner_ || runner_->shouldExit()) {
      finish();
      return;
    }

    if (runner_->hasError()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
          mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        finish();
        return;
      }
      return;
    }

    // Browser render/cache blits must never make the input loop wait for the
    // Lua mutex. Latch Back out of band, then deliver once VM ownership returns.
    std::unique_lock<std::mutex> ownership(luaMutex_, std::defer_lock);
    if (runner_->cooperativeBrowser()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        pendingBack_ = true;
        runner_->requestNativeCancel();
      }
      if (!ownership.try_lock()) {
        // Touch events are one-frame snapshots too. Save one without querying
        // Lua (hasLongPressHandler would need this mutex), then deliver it once.
        int x = 0, y = 0;
        if (!pendingTap_ && mappedInput.wasScreenTapped(x, y)) {
          pendingTap_ = true;
          pendingTapX_ = x;
          pendingTapY_ = y;
          runner_->requestNativeCancel();
        }
        return;
      }
      runner_->clearNativeCancel();
    } else
      ownership.lock();
    bool buttonHandled = false;

    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || pendingBack_) {
      pendingBack_ = false;
      pendingTap_ = false;
      runner_->clearNativeCancel();
      runner_->onButton("back", true);
      buttonHandled = true;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      runner_->onButton("confirm", true);
      buttonHandled = true;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      runner_->onButton("left", true);
      buttonHandled = true;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      runner_->onButton("right", true);
      buttonHandled = true;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      runner_->onButton("up", true);
      buttonHandled = true;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
      runner_->onButton("page_back", true);
      buttonHandled = true;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      runner_->onButton("down", true);
      buttonHandled = true;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      runner_->onButton("page_forward", true);
      buttonHandled = true;
    }

    {
      if (pendingTap_) {
        pendingTap_ = false;
        runner_->onTap(pendingTapX_, pendingTapY_);
        runner_->onTouch(pendingTapX_, pendingTapY_, "tap");
        buttonHandled = true;
      } else
        buttonHandled = dispatchLuaTouch(mappedInput, *runner_) || buttonHandled;
    }

    auto swipe = mappedInput.wasSwipe();
    if (swipe != MappedInputManager::SwipeDir::None) {
      const char* dirStr = "none";
      if (swipe == MappedInputManager::SwipeDir::Up)
        dirStr = "up";
      else if (swipe == MappedInputManager::SwipeDir::Down)
        dirStr = "down";
      else if (swipe == MappedInputManager::SwipeDir::Left)
        dirStr = "left";
      else if (swipe == MappedInputManager::SwipeDir::Right)
        dirStr = "right";
      runner_->onSwipe(dirStr);
      buttonHandled = true;
    }

    {
      runner_->onUpdate();
    }

    if (buttonHandled || (runner_ && runner_->checkAndClearRedrawRequested())) {
      if (runner_->shouldExit()) {
        finish();
        return;
      }
      requestUpdate();
    }
  }

  void render(RenderLock&& lock) override {
    if (!runner_) return;
    if (runner_->hasError()) {
      renderError(runner_->getErrorMessage().c_str());
      return;
    }
    {
      std::lock_guard<std::mutex> lk(luaMutex_);
      runner_->onDraw();
    }
    if (runner_->hasError()) {
      renderError(runner_->getErrorMessage().c_str());
      return;
    }
    HalDisplay::RefreshMode refreshMode = (runner_ && runner_->checkAndClearFullRefreshRequested())
                                              ? HalDisplay::RefreshMode::FULL_REFRESH
                                              : HalDisplay::RefreshMode::FAST_REFRESH;
    renderer.displayBuffer(refreshMode);
  }

  void onExit() override {
    failLaunch(airdrop::browser::LaunchError::ExitedBeforeAck);
    {
      std::lock_guard<std::mutex> lock(luaMutex_);
      if (runner_) {
        runner_->onExit();
        runner_->shutdown();
        runner_.reset();
      }
    }
    if (ownsWifi_) HalLuaHttp::disconnectWifi();
    renderer.setOrientation(initialOrientation_);
    Activity::onExit();
  }

 private:
  uint32_t launchAttempt_ = 0;
  void failLaunch(airdrop::browser::LaunchError reason) {
    if (launch_) launch_->failAttempt(launchAttempt_, reason);
  }
  bool pendingBack_ = false, pendingTap_ = false;
  int pendingTapX_ = 0, pendingTapY_ = 0;
  bool wifiPending_ = false;
  bool ownsWifi_ = false;
  void prepareNetwork() {
    if (runner_->networkEnabled() && !HalLuaHttp::wifiConnected())
      wifiPending_ = true;
    else
      startScript();
  }
  bool scriptStarted_ = false;
  void startScript() {
    scriptStarted_ = true;
    std::string scriptPath = appDir_ + "/" + entryScript_;
    {
      std::lock_guard<std::mutex> lock(luaMutex_);
      if (runner_->loadScript(scriptPath)) runner_->onInit();
    }
    if (launch_ && runner_->hasError()) {
      failLaunch(airdrop::browser::LaunchError::ScriptFailed);
      finish();
      return;
    }
    requestUpdate();
  }
  std::string appDir_;
  std::string appName_;
  std::string entryScript_;
  std::string appId_;
  airdrop::browser::LaunchHandle launch_;
  std::unique_ptr<Runner> runner_;
  std::mutex luaMutex_;
  GfxRenderer::Orientation initialOrientation_ = GfxRenderer::Orientation::Portrait;

  void renderError(const char* msg) {
    renderer.setOrientation(initialOrientation_);
    int w = renderer.getScreenWidth();
    int h = renderer.getScreenHeight();
    const auto& m = UITheme::getInstance().getMetrics();

    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, appName_.c_str(), "Script Error");

    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 40, "Application Error", true, EpdFontFamily::BOLD);
    if (msg) {
      auto lines = renderer.wrappedText(SMALL_FONT_ID, msg, w - 64, 5);
      int y = h / 2 - 10;
      for (const auto& line : lines) {
        renderer.drawCenteredText(SMALL_FONT_ID, y, line.c_str(), true);
        y += renderer.getLineHeight(SMALL_FONT_ID) + 2;
      }
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
  }
};

using LuaAppActivity = LuaAppActivityAdapter<>;
