#include <openssl/evp.h>
#include <openssl/sha.h>

#include "network/NotesRealtimeWire.h"
inline void esp_fill_random(void* out, size_t n) { memset(out, 7, n); }
inline uint32_t esp_random() { return 0x04030201; }
inline int mbedtls_sha1(const unsigned char* p, size_t n, unsigned char* out) { return SHA1(p, n, out) ? 0 : -1; }
#include "util/NotesRealtimeTransport.inc"
static std::string accept() {
  const std::string value = notes::base64(std::string(16, '\7')) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  unsigned char hash[20];
  SHA1(reinterpret_cast<const unsigned char*>(value.data()), value.size(), hash);
  return notes::base64(std::string(reinterpret_cast<char*>(hash), 20));
}
int main() {
  (void)&exchange;
  assert(notes::base64("abc") == "YWJj" && notes::base64("a") == "YQ==" && notes::base64("ab") == "YWI=");
  const auto response =
      "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
      accept() + "\r\n\r\n";
  replies = {response + std::string("\x81\x02{}", 4)};
  unsigned got = 0;
  bool sent = false;
  assert(pluginhttp::notesRealtime(
      "sk-fixture",
      [&](std::string& s) {
        if (!sent) {
          s = "{\"type\":\"session.update\"}";
          sent = true;
        }
        return true;
      },
      [&](const std::string& s) {
        assert(s == "{}");
        ++got;
        return false;
      },
      [] { return false; }));
  assert(got == 1 && created == destroyed && hosts.back() == "api.openai.com");
  assert(writes[0].find("GET /v1/realtime?intent=transcription HTTP/1.1\r\nHost: api.openai.com\r\n") == 0);
  assert(writes[0].find("Authorization: Bearer sk-fixture\r\n") != std::string::npos);
  const auto before = created;
  assert(!pluginhttp::notesRealtime(
      "bad\r\n", [](std::string&) { return true; }, [](const std::string&) { return true; }, [] { return false; }));
  assert(created == before);
  replies.push_back("HTTP/1.1 302 Found\r\nLocation: https://evil.invalid\r\n\r\n");
  assert(!pluginhttp::notesRealtime(
      "sk-fixture", [](std::string&) { return true; }, [](const std::string&) { return true; }, [] { return false; }));
  assert(created == before + 1 && created == destroyed);
  replies.push_back(
      "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
      "wrong\r\n\r\n");
  assert(!pluginhttp::notesRealtime(
      "sk-fixture", [](std::string&) { return true; }, [](const std::string&) { return true; }, [] { return false; }));
  assert(created == destroyed);
  replies.push_back(response.substr(0, response.size() - 2));
  unsigned earlyCallbacks = 0;
  assert(!pluginhttp::notesRealtime(
      "sk-fixture",
      [&](std::string&) {
        ++earlyCallbacks;
        return true;
      },
      [](const std::string&) { return true; }, [] { return false; }));
  assert(earlyCallbacks == 0 && created == destroyed);
  for (const auto& log : diagnostics) assert(log.find("sk-fixture") == std::string::npos);
  puts("Notes actual fixed-origin TLS adapter/WebSocket upgrade OK (scripted ESP, not cryptographic qualification)");
}
