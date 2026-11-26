#include "NotesDictation.h"
#include <Arduino.h>
#include <HalLuaHttp.h>
#include <HalStorage.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <new>
#include "hal/NotesMicrophone.h"
#include "NotesAudioQueue.h"
#include "NotesRealtimeProtocol.h"
#include "NotesRealtimeWire.h"
#include "util/PluginHttp.h"
#ifndef SIMULATOR
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace {
std::atomic<bool> microphoneOwned{false};
constexpr size_t QUEUE_BYTES = 32768;
}
struct NotesDictation::Impl {
  mutable std::mutex mutex;
  NotesMicrophone mic;
  uint8_t* memory = nullptr;
  std::unique_ptr<notes::AudioQueue> queue;
  std::string key, text;
  const char* status = "Connecting";
  std::atomic<bool> running{false}, capturing{false}, cancelled{false}, stopping{false}, captureFailed{false};
  bool cleanupOK = true;
  uint32_t captureStart = 0;
  size_t capturedBytes = 0;
  ~Impl() {
#ifndef SIMULATOR
    heap_caps_free(memory);
#endif
    std::fill(key.begin(), key.end(), '\0');
  }
  void label(const char* value) { std::lock_guard<std::mutex> lock(mutex); status = value; }
#ifndef SIMULATOR
  static void captureEntry(void* arg) {
    auto& self = *static_cast<Impl*>(arg);
    self.captureStart = millis();
    if (!self.cancelled.load() && !self.stopping.load() && self.mic.begin()) {
      self.label("Listening");
      uint8_t bytes[256];
      while (!self.cancelled.load() && !self.stopping.load() &&
             uint32_t(millis() - self.captureStart) < 30000 && self.capturedBytes < 1440000) {
        const size_t n = self.mic.read(bytes, sizeof(bytes));
        if (n && !self.cancelled.load() && !self.stopping.load()) {
          std::lock_guard<std::mutex> lock(self.mutex);
          if (!self.queue->push(bytes, n)) { self.captureFailed.store(true); break; }
          self.capturedBytes += n;
        }
        delay(1);
      }
    } else if (!self.cancelled.load() && !self.stopping.load()) self.captureFailed.store(true);
    self.stopping.store(true);
    self.cleanupOK = self.mic.close();
    if (!self.cleanupOK) self.captureFailed.store(true);
    self.capturing.store(false);
    vTaskDelete(nullptr);
  }
  static void networkEntry(void* arg) {
    auto& self = *static_cast<Impl*>(arg);
    {
    notes::Transcript transcript;
    bool configured = false, captureStarted = false, committed = false;
    uint32_t waitStart = millis();
    const auto cancel = [&] { return self.cancelled.load() || self.captureFailed.load(); };
    bool success = false;
    if (!cancel()) success = pluginhttp::notesRealtime(self.key, [&](std::string& out) {
      if (cancel()) return false;
      if (!configured) { out = notes::SESSION_UPDATE; configured = true; waitStart = millis(); return true; }
      if (!transcript.ready) return uint32_t(millis() - waitStart) < 15000;
      if (!captureStarted) {
        if (self.stopping.load()) return false;
        captureStarted = true;
        self.capturing.store(true);
        if (xTaskCreate(captureEntry, "notes-pdm", 4096, &self, 2, nullptr) != pdPASS) {
          self.capturing.store(false); self.captureFailed.store(true); return false;
        }
      }
      uint8_t bytes[2048]; size_t n;
      { std::lock_guard<std::mutex> lock(self.mutex); n = self.queue->pop(bytes, sizeof(bytes)); }
      if (n) out = "{\"type\":\"input_audio_buffer.append\",\"audio\":\"" +
          notes::base64(std::string(reinterpret_cast<char*>(bytes), n)) + "\"}";
      else if (!self.capturing.load() && !committed) {
        if (self.capturedBytes < 4800) return false;
        out = notes::COMMIT; committed = true; waitStart = millis(); self.label("Finishing");
      }
      return !committed || uint32_t(millis() - waitStart) < 15000;
    }, [&](const std::string& event) {
      if (cancel()) return false;
      if (!transcript.consume(event)) { self.captureFailed.store(true); return false; }
      { std::lock_guard<std::mutex> lock(self.mutex); if (!self.cancelled.load()) self.text = transcript.text; }
      return !transcript.complete;
    }, cancel);
    self.mic.stop();
    self.stopping.store(true);
    while (self.capturing.load()) delay(1);
    std::fill(self.key.begin(), self.key.end(), '\0'); self.key.clear();
    if (self.cancelled.load()) {
      std::lock_guard<std::mutex> lock(self.mutex); self.text.clear(); self.status = "Cancelled";
    } else self.label(success && transcript.complete ? "Done" : "Dictation failed; text kept");
    // A failed driver teardown keeps the microphone lease quarantined until reboot.
    if (self.cleanupOK) microphoneOwned.store(false);
    }  // Destroy transcript/callback owners before FreeRTOS deletes this stack.
    self.running.store(false);
    vTaskDelete(nullptr);
  }
#endif
};
NotesDictation::NotesDictation() = default;
NotesDictation::~NotesDictation() { close(); }
bool NotesDictation::start() {
#ifdef SIMULATOR
  idleStatus = "Dictation requires Sticky hardware";
  return false;
#else
  if (impl && impl->running.load()) return false;
  impl.reset();
  if (!HalLuaHttp::wifiConnected()) { idleStatus = "Connect Wi-Fi in Control Center first"; return false; }
  HalFile file;
  if (!Storage.openFileForRead("NSTT", "/.crosspoint/notes-openai.key", file)) {
    idleStatus = "OpenAI key not configured"; return false;
  }
  std::string key;
  const size_t n = file.size();
  if (n > 0 && n <= 514) {
    key.resize(n);
    if (file.read(reinterpret_cast<uint8_t*>(&key[0]), n) != static_cast<int>(n)) key.clear();
  }
  file.close();
  while (!key.empty() && (key.back() == '\n' || key.back() == '\r')) key.pop_back();
  if (!notes::validKey(key)) { idleStatus = "OpenAI key missing or invalid"; return false; }
  bool free = false;
  if (!microphoneOwned.compare_exchange_strong(free, true)) { idleStatus = "Microphone busy; restart if stuck"; return false; }
  auto next = std::unique_ptr<Impl>(new (std::nothrow) Impl);
  if (next) {
    next->memory = static_cast<uint8_t*>(heap_caps_malloc(QUEUE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (next->memory) next->queue.reset(new (std::nothrow) notes::AudioQueue(next->memory, QUEUE_BYTES));
  }
  if (!next || !next->queue) { microphoneOwned.store(false); idleStatus = "Not enough PSRAM"; return false; }
  next->key.swap(key);
  next->running.store(true);
  impl = std::move(next);
  if (xTaskCreate(Impl::networkEntry, "notes-stt", 12288, impl.get(), 1, nullptr) != pdPASS) {
    impl->running.store(false); impl.reset(); microphoneOwned.store(false); idleStatus = "Cannot start dictation"; return false;
  }
  return true;
#endif
}
void NotesDictation::stop() {
  if (impl) { impl->stopping.store(true); impl->mic.stop(); }
}
void NotesDictation::cancel() {
  if (impl) { impl->cancelled.store(true); stop(); }
}
void NotesDictation::close() {
  cancel();
  while (impl && impl->running.load()) delay(1);
  impl.reset();
}
NotesDictation::Snapshot NotesDictation::snapshot() const {
  if (!impl) return {{}, idleStatus, false};
  std::lock_guard<std::mutex> lock(impl->mutex);
  return {impl->cancelled.load() ? "" : impl->text, impl->cancelled.load() ? "Cancelled" : impl->status, impl->running.load()};
}
