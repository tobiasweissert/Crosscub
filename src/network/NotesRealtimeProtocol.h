#pragma once
#include <ArduinoJson.h>

#include <string>

namespace notes {
inline constexpr const char* SESSION_UPDATE =
    R"({"type":"session.update","session":{"type":"transcription","audio":{"input":{"format":{"type":"audio/pcm","rate":24000},"transcription":{"model":"gpt-live-transcribe"},"turn_detection":null}}}})";
inline constexpr const char* COMMIT = R"({"type":"input_audio_buffer.commit"})";
// Same byte/line contract as NotesCore. Reject invalid provider UTF-8 before edit/save.
inline bool validText(const std::string& text) {
  if (text.size() > 3072) return false;
  size_t lines = 1;
  for (size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i++]);
    if (c == '\n' && ++lines > 97) return false;
    if (c < 128) {
      if (c == 0 || (c < 32 && c != '\n' && c != '\t' && c != '\r')) return false;
      continue;
    }
    unsigned n;
    uint32_t cp;
    if (c >= 0xc2 && c <= 0xdf) {
      n = 1;
      cp = c & 31;
    } else if (c >= 0xe0 && c <= 0xef) {
      n = 2;
      cp = c & 15;
    } else if (c >= 0xf0 && c <= 0xf4) {
      n = 3;
      cp = c & 7;
    } else
      return false;
    if (n > text.size() - i) return false;
    for (unsigned j = 0; j < n; ++j) {
      const auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (next & 63);
    }
    if ((n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      return false;
  }
  return true;
}
struct Transcript {
  std::string text, item, committed, finalItem;
  bool ready = false, complete = false;
  bool consume(const std::string& raw) {
    ArduinoJson::JsonDocument doc;
    if (raw.size() > 16384 || deserializeJson(doc, raw) || !doc["type"].is<const char*>()) return false;
    const std::string type = doc["type"].as<const char*>();
    if (type == "error" || type == "conversation.item.input_audio_transcription.failed") return false;
    if (type == "session.updated") {
      ready = true;
      return true;
    }
    const bool delta = type == "conversation.item.input_audio_transcription.delta";
    const bool final = type == "conversation.item.input_audio_transcription.completed";
    const bool commit = type == "input_audio_buffer.committed";
    if (!delta && !final && !commit) return true;
    if (!doc["item_id"].is<const char*>()) return false;
    const std::string id = doc["item_id"].as<const char*>();
    if (id.empty() || id.size() > 128 || (!item.empty() && item != id) || (!committed.empty() && committed != id))
      return false;
    if (commit)
      committed = id;
    else {
      if (!doc["content_index"].is<unsigned>() || doc["content_index"].as<unsigned>() != 0) return false;
      const auto value = doc[delta ? "delta" : "transcript"];
      if (!value.is<const char*>()) return false;
      const ArduinoJson::JsonString s = value.as<ArduinoJson::JsonString>();
      const std::string candidate = (delta ? text : "") + std::string(s.c_str(), s.size());
      if (!validText(candidate) || (!finalItem.empty() && delta)) return false;
      item = id;
      text = candidate;
      if (final) finalItem = id;
    }
    complete = !committed.empty() && finalItem == committed;
    return true;
  }
};
}  // namespace notes
