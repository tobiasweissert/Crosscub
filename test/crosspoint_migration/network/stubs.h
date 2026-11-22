#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using String = std::string;
class Stream {
 public:
  virtual ~Stream() = default;
  virtual size_t write(const uint8_t*, size_t) = 0;
};
template <class... T>
void logIgnore(T&&...) {}
#define LOG_ERR(...) logIgnore(__VA_ARGS__)
#define LOG_DBG(...) logIgnore(__VA_ARGS__)
#define LOG_INF(...) logIgnore(__VA_ARGS__)
#define CROSSSMUDGE_VERSION "test"
#define WL_CONNECTED 1
inline uint32_t millis() {
  static uint32_t t = 0;
  return ++t;
}
inline void delay(int) {}
[[maybe_unused]] inline struct {
  unsigned getFreeHeap() { return 100000; }
  unsigned getMaxAllocHeap() { return 100000; }
} ESP;
[[maybe_unused]] inline struct {
  int status() { return 1; }
  int RSSI() { return -40; }
} WiFi;
class WifiPowerSaveGuard {};
template <class T>
auto makeUniqueNoThrow(size_t n) {
  return std::make_unique<T>(n);
}
namespace base64 {
inline String encode(const char* s) { return std::string("encoded:") + s; }
}  // namespace base64
constexpr int O_WRONLY = 1, O_APPEND = 2;
inline std::map<std::string, std::string> files;
inline bool renameAcknowledged = true;
inline unsigned removeCalls = 0;
class HalFile {
  std::string path;

 public:
  HalFile() = default;
  explicit HalFile(std::string p) : path(std::move(p)) {}
  explicit operator bool() const { return !path.empty(); }
  size_t fileSize() const { return files[path].size(); }
  size_t write(const uint8_t* p, size_t n) {
    files[path].append(reinterpret_cast<const char*>(p), n);
    return n;
  }
  bool close() {
    path.clear();
    return true;
  }
  void flush() {}
  bool sync() { return true; }
  bool rename(const char* dest) {
    files[dest] = files[path];
    if (renameAcknowledged) files.erase(path);
    return renameAcknowledged;
  }
};
inline struct StorageFake {
  bool exists(const char* p) { return files.count(p); }
  bool remove(const char* p) {
    ++removeCalls;
    return files.erase(p);
  }
  bool openFileForRead(const char*, const char* p, HalFile& out) {
    if (!exists(p)) return false;
    out = HalFile(p);
    return true;
  }
  bool openFileForWrite(const char*, const char* p, HalFile& out) {
    files[p].clear();
    out = HalFile(p);
    return true;
  }
  HalFile open(const char* p, int = 0) {
    if (!exists(p)) return {};
    return HalFile(p);
  }
} Storage;
using esp_err_t = int;
constexpr int ESP_OK = 0, HTTP_EVENT_ON_HEADER = 1, HTTP_METHOD_POST = 1, HTTP_METHOD_PUT = 2;
struct esp_http_client_event_t {
  int event_id;
  void* user_data;
  const char* header_key;
  const char* header_value;
};
inline int esp_crt_bundle_attach(void*) { return 0; }
inline const char* esp_err_to_name(int) { return "fixture"; }
struct esp_http_client_config_t {
  const char* url = nullptr;
  int buffer_size = 0, buffer_size_tx = 0, timeout_ms = 0;
  int (*crt_bundle_attach)(void*) = nullptr;
  bool keep_alive_enable = false, disable_auto_redirect = false, skip_cert_common_name_check = false;
  int (*event_handler)(esp_http_client_event_t*) = nullptr;
  void* user_data = nullptr;
};
struct Response {
  int status = 200;
  std::string body = "body", location, auth;
  bool complete = true;
};
inline std::vector<Response> responses;
inline size_t nextResponse = 0;
struct Client {
  esp_http_client_config_t config;
  Response response;
  size_t read = 0;
  std::map<std::string, std::string> headers;
  std::string sent;
  int method = 0;
  size_t openedBodySize = 0;
};
using esp_http_client_handle_t = Client*;
#define ESP_ERR_HTTP_EAGAIN 11
inline int esp_http_client_set_timeout_ms(Client*, int) { return 0; }
inline std::vector<Client> requests;
inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* c) {
  if (nextResponse >= responses.size()) return nullptr;
  return new Client{*c, responses[nextResponse++], 0, {}, "", 0};
}
inline int esp_http_client_set_header(Client* c, const char* k, const char* v) {
  c->headers[k] = v;
  return 0;
}
inline int esp_http_client_set_method(Client* c, int m) {
  c->method = m;
  return 0;
}
inline int esp_http_client_open(Client* c, size_t bodySize) {
  c->openedBodySize = bodySize;
  return 0;
}
inline int esp_http_client_write(Client* c, const char* b, size_t n) {
  c->sent.append(b, n);
  return static_cast<int>(n);
}
inline int64_t esp_http_client_fetch_headers(Client* c) {
  auto send = [&](const char* k, const std::string& v) {
    if (!v.empty()) {
      esp_http_client_event_t e{HTTP_EVENT_ON_HEADER, c->config.user_data, k, v.c_str()};
      c->config.event_handler(&e);
    }
  };
  send("Location", c->response.location);
  send("WWW-Authenticate", c->response.auth);
  return c->response.body.size();
}
inline int esp_http_client_get_status_code(Client* c) { return c->response.status; }
inline int esp_http_client_read(Client* c, char* p, size_t n) {
  n = std::min(n, c->response.body.size() - c->read);
  memcpy(p, c->response.body.data() + c->read, n);
  c->read += n;
  return static_cast<int>(n);
}
inline bool esp_http_client_is_complete_data_received(Client* c) { return c->response.complete; }
inline void esp_http_client_cleanup(Client* c) {
  requests.push_back(*c);
  delete c;
}
inline int esp_http_client_get_and_clear_last_tls_error(Client*, int*, int*) { return 0; }
inline void fixture(std::vector<Response> r) {
  responses = std::move(r);
  nextResponse = 0;
  requests.clear();
  files.clear();
  removeCalls = 0;
  renameAcknowledged = true;
}
