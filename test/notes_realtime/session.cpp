#include <Arduino.h>
#include <HalLuaHttp.h>
#include <HalStorage.h>
#include <esp_heap_caps.h>
#include <freertos/task.h>

#include <cassert>
#include <cstdio>

#include "mic_fixture.h"
#include "network/NotesDictation.h"
#include "util/PluginHttp.h"
static void waitFor(const std::function<bool()>& ready) {
  for (int i = 0; i < 3000 && !ready(); ++i) delay(1);
  assert(ready());
}
int main() {
  NotesDictation s;
  keyFile = "";
  assert(!s.start());
  assert(!s.snapshot().busy && !rail);
  keyFile = "sk-fixture";
  wifiUp = false;
  assert(!s.start());
  wifiUp = true;
  failAllocation = true;
  assert(!s.start());
  failAllocation = false;
  std::atomic<unsigned> calls{0};
  std::atomic<bool> holdNetwork{false};
  pluginhttp::fixture = [&](auto&, auto& o, auto& i, auto& cancel) {
    ++calls;
    std::string out;
    assert(o(out));
    assert(out.find("gpt-live-transcribe") != std::string::npos);
    assert(i(R"({"type":"session.updated"})"));
    out.clear();
    assert(o(out));
    waitFor([] { return rail.load(); });
    assert(i(
        R"({"type":"conversation.item.input_audio_transcription.delta","item_id":"x","content_index":0,"delta":"Hello"})"));
    while (holdNetwork.load() && !cancel()) delay(1);
    while (!cancel()) {
      out.clear();
      if (!o(out)) break;
      if (out.find("input_audio_buffer.commit") != std::string::npos) {
        assert(i(R"({"type":"input_audio_buffer.committed","item_id":"x"})"));
        assert(!i(
            R"({"type":"conversation.item.input_audio_transcription.completed","item_id":"x","content_index":0,"transcript":"Hello!"})"));
        return true;
      }
      delay(1);
    }
    return false;
  };
  assert(s.start());
  waitFor([&] { return s.snapshot().text == "Hello"; });
  assert(s.snapshot().busy && rail);
  delay(100);
  s.stop();
  assert(!rail);
  waitFor([&] { return !s.snapshot().busy; });
  assert(s.snapshot().text == "Hello!" && std::string(s.snapshot().status) == "Done");
  assert(deletes == 1);
  // Queued work cannot power up or make a request after Home/lock/exit cancellation.
  holdTasks = true;
  assert(s.start());
  s.cancel();
  assert(!rail);
  const auto old = calls.load();
  holdTasks = false;
  waitFor([&] { return !s.snapshot().busy; });
  assert(calls == old && s.snapshot().text.empty() && !rail);
  // Capture deadline belongs to its worker even when the editor does not poll.
  resetMic();
  testTick = 0;
  holdNetwork = true;
  assert(s.start());
  waitFor([] { return rail.load(); });
  testTick = 30001;
  waitFor([] { return !rail; });
  assert(s.snapshot().busy);
  holdNetwork = false;
  waitFor([&] { return !s.snapshot().busy; });
  assert(deletes == 1);
  puts(
      "Notes actual session: live partials, Stop commit/final, offline/OOM, queued cancellation and worker deadline "
      "OK");
}
