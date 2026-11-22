#if defined(ARDUINO_ARCH_ESP32) && defined(FREEINK_DEVICE_STICKY) && FREEINK_DEVICE_STICKY && !defined(SIMULATOR)
#include "DictationActivity.h"

#include <AppNetworkPolicy.h>
#include <I18n.h>
#include <WiFi.h>

#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "fontIds.h"
namespace dictation {
static const char* errorText(const char* message) {
  if (!message) return tr(STR_VOICE_FAILED);
  if (strstr(message, "Key rejected")) return tr(STR_VOICE_AUTH);
  if (strstr(message, "Quota")) return tr(STR_VOICE_QUOTA);
  if (strstr(message, "clock") || strstr(message, "key, connect")) return tr(STR_VOICE_POLICY);
  if (strstr(message, "expired")) return tr(STR_VOICE_EXPIRED);
  if (strstr(message, "large") || strstr(message, "size")) return tr(STR_VOICE_SIZE);
  return tr(STR_VOICE_ERROR);
}
int DictationActivity::action() const {
  auto state = job.state.load();
  if (state == Job::Ready || state == Job::Idle || state == Job::Failed) {
    if (I18N.getLanguage() != Language::EN && I18N.getLanguage() != Language::DE) return 8;
    if (!configured()) return 6;
    if (!WiFi.isConnected()) return WiFi.getMode() == WIFI_OFF ? 7 : 9;
  }
  return state;
}
void DictationActivity::render(RenderLock&&) {
  auto state = action();
  const char* actionLabel = tr(STR_VOICE_RECORD);
  const char* detail = tr(STR_VOICE_LOCAL);
  if (state == 8) {
    actionLabel = tr(STR_VOICE_UNAVAILABLE);
    detail = tr(STR_VOICE_LANG);
  }
  if (state == 6) {
    actionLabel = tr(STR_VOICE_KEY);
    detail = tr(STR_VOICE_SETUP_HINT);
  }
  if (state == 9) {
    actionLabel = tr(STR_VOICE_UNAVAILABLE);
    detail = tr(STR_VOICE_BUSY);
  }
  if (state == 7) {
    actionLabel = tr(STR_VOICE_WIFI);
    detail = tr(STR_VOICE_CONNECT);
  }
  if (state == Job::Recording) {
    actionLabel = tr(STR_VOICE_STOP);
    detail = tr(STR_VOICE_MIC);
  }
  if (state == Job::Ready) {
    actionLabel = tr(STR_VOICE_SEND);
    detail = tr(STR_VOICE_COST);
  }
  if (state == Job::Sending) {
    actionLabel = tr(STR_VOICE_SENDING);
    detail = tr(STR_VOICE_RECALL);
  }
  if (state == Job::Review) {
    actionLabel = tr(STR_VOICE_REVIEW);
    detail = tr(STR_VOICE_EDIT);
  }
  if (state == Job::Failed) {
    detail = errorText(job.error.load());
    actionLabel = tr(STR_VOICE_RETRY);
  }
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 32, tr(STR_NOTES_DICTATION));
  int y = 82;
  for (auto& line : renderer.wrappedText(SMALL_FONT_ID, detail, renderer.getScreenWidth() - 32, 4)) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, line.c_str());
    y += renderer.getLineHeight(SMALL_FONT_ID) + 2;
  }
  int h = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_12_FONT_ID, h - 146, actionLabel);
  renderer.drawCenteredText(UI_12_FONT_ID, h - 62, tr(STR_CANCEL));
  renderer.displayBuffer();
  // Include orientation/height: a release must still target the painted band.
  const int identity = h * 16 + state;
  if (painted.exchange(identity) != identity) ++paintGeneration;
}
void DictationActivity::loop() {
  if (leaving) return;
  int x = 0, y = 0;
  bool tap = mappedInput.wasScreenTapped(x, y);
  const int h = renderer.getScreenHeight();
  if (revoked || mappedInput.wasReleased(MappedInputManager::Button::Back) || (tap && y >= h - 86)) {
    leaving = true;
    job.cancelled = true;
    ActivityResult r;
    r.isCancelled = true;
    setResult(std::move(r));
    finish();
    return;
  }
  int state = action();
  const int identity = h * 16 + state;
  const auto generation = paintGeneration.load();
  if (painted != identity || seenGeneration != generation || seen != identity) {
    seenGeneration = generation;
    seen = painted == identity ? identity : -1;
    armed = -1;
    neutral = false;
    requestUpdate();
    return;
  }
  using Button = MappedInputManager::Button;
  bool down = mappedInput.wasPressed(Button::Confirm), up = mappedInput.wasReleased(Button::Confirm);
  int px = 0, py = 0;
  bool contact = mappedInput.wasScreenTouchDown(px, py);
  auto band = [h](int at) { return at >= h - 172 && at < h - 88; };
  if (!neutral) {
    neutral = !down && !up && !tap && !contact && !mappedInput.isPressed(Button::Confirm) &&
              !mappedInput.isScreenContactActive();
    return;
  }
  if (down || contact) {
    armed = (contact && !band(py)) ? -1 : identity;
    touchArm = contact;
    return;
  }
  if (!up && !tap) {
    if (!mappedInput.isPressed(Button::Confirm) && !mappedInput.isScreenContactActive()) armed = -1;
    return;
  }
  bool chosen = painted == identity && paintGeneration.load() == generation && armed == identity &&
                (touchArm ? (tap && band(y)) : up);
  armed = -1;
  neutral = false;
  if (!chosen) return;
  if (state == 7) {
    bool ok = false;
    if (!ink::http::networkEnabled(ok) || !ok || WiFi.getMode() != WIFI_OFF) return;
    // Claim only an idle radio; the selector releases cancelled attempts and
    // hands a successful STA connection back to this parent.
    ownsRadio = true;
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false),
                           [this](const ActivityResult& r) {
                             if (r.isCancelled) ownsRadio = false;
                             seen = armed = -1;
                             neutral = false;
                             requestUpdate();
                           });
  } else if (state == Job::Idle || state == Job::Failed) {
    job.start();
  } else if (state == Job::Recording)
    job.stop = true;
  else if (state == Job::Ready)
    job.send = true;
  else if (state == Job::Review) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_VOICE_APPEND),
                                                                   job.transcript, maxText, InputType::Text, 1),
                           [this](const ActivityResult& r) {
                             if (leaving || revoked) return;
                             if (!r.isCancelled) {
                               if (auto k = std::get_if<KeyboardResult>(&r.data)) setResult(KeyboardResult{k->text});
                             }
                             leaving = true;
                             finish();
                           });
  }
  requestUpdate();
}
void DictationActivity::releaseRadio() {
  if (ownsRadio && WiFi.getMode() == WIFI_STA) {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
  }
  ownsRadio = false;
}
void DictationActivity::onExit() {
  leaving = true;
  job.drain();
  releaseRadio();
  Activity::onExit();
}
}  // namespace dictation
#endif
