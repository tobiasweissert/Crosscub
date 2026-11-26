#include "PluginHttp.h"

#include <Arduino.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#ifndef SIMULATOR
#include <esp_crt_bundle.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_tls.h>
#include <lwip/netdb.h>
#include <mbedtls/sha1.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif
#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

#include "network/ProtectedPaths.h"
#include "network/WifiPowerSaveGuard.h"

namespace {
constexpr size_t MAX_TOKEN_FILE_SIZE = 2 * 1024;

// Fixed, nonsecret failure-stage codes only; never include request/peer data.
// 1 qualification, 2 clock, 3 DNS, 4 address, 5 TLS handshake,
// 6 TLS verify, 7 TLS date/chain bounds, 8 framing, 9 input,
// 10 permission/cancellation/elapsed, 11 write.
enum class Failure : unsigned {
  Qualification = 1,
  Clock,
  Dns,
  Address,
  TlsHandshake,
  TlsVerify,
  TlsDate,
  Framing,
  Input,
  Stopped,
  Write
};
#if defined(CROSSCUB_PLUGIN_HUB_DIAGNOSTIC) && !defined(SIMULATOR)
std::atomic<TaskHandle_t> diagnosticTask{nullptr};
unsigned diagnosticStage = 0;
size_t diagnosticWrites = 0;
size_t diagnosticWriteCalls = 0;
bool inDiagnostic() { return diagnosticTask.load() == xTaskGetCurrentTaskHandle(); }
// This local qualification image admits read-only Hub indexes, not arbitrary plugins.
bool diagnosticHubUrl(const std::string& url) {
  return url == "https://raw.githubusercontent.com/crosspoint-reader/crosspoint-plugin-hub/main/catalog.json" ||
         url ==
             "https://raw.githubusercontent.com/crosspoint-reader/crosspoint-plugin-hub/main/community-catalog.json" ||
         url == "https://raw.githubusercontent.com/crosspoint-reader/crosspoint-plugin-hub/main/catalog-lists.json" ||
         url ==
             "https://raw.githubusercontent.com/crosspoint-reader/crosspoint-plugin-hub/main/"
             "catalog-lists-capabilities.json";
}
#endif
int fail(Failure stage) {
#if defined(CROSSCUB_PLUGIN_HUB_DIAGNOSTIC) && !defined(SIMULATOR)
  if (inDiagnostic()) diagnosticStage = static_cast<unsigned>(stage);
#endif
  LOG_ERR("PHTP", "stage=%u", static_cast<unsigned>(stage));
  return -1;
}
bool reject(Failure stage) {
  fail(stage);
  return false;
}

// No permission prompts: public plugin networking is admitted by default.
// This optional global opt-out is independent of the parent's qualification flag.
std::atomic<bool> networkAllowed{true};
constexpr uint32_t ELAPSED_MS = 30000;
constexpr size_t MAX_URL = 4096, MAX_HEADERS = 8192, MAX_LINE = 4096;
constexpr size_t MAX_UPLOAD = 64 * 1024, MAX_BODY = 30 * 1024 * 1024;

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}
bool token(const std::string& s) {
  if (s.empty()) return false;
  for (unsigned char c : s)
    if (!c || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                std::strchr("!#$%&'*+-.^_`|~", c)))
      return false;
  return true;
}
bool fieldValue(const std::string& s) {
  for (unsigned char c : s)
    if (c < 32 || c == 127) return false;
  return true;
}
struct Url {
  std::string host, authority, target;
  uint16_t port = 443;
};
bool parseUrl(const std::string& raw, Url& out) {
  out = {};
  if (raw.size() > MAX_URL || raw.size() < 9 || lower(raw.substr(0, 8)) != "https://") return false;
  // Reject raw controls, userinfo and ambiguous authority syntax before escaping.
  for (unsigned char c : raw)
    if (c < 32 || c == 127 || c == '\\') return false;
  const size_t end = raw.find_first_of("/?#", 8);
  std::string authority = raw.substr(8, end == std::string::npos ? end : end - 8);
  if (authority.empty() || authority.find_first_of("@%[] ") != std::string::npos) return false;
  const size_t colon = authority.find(':');
  if (colon != std::string::npos) {
    const std::string digits = authority.substr(colon + 1);
    if (digits.empty() || digits.size() > 5 || digits.find_first_not_of("0123456789") != std::string::npos)
      return false;
    const unsigned long port = strtoul(digits.c_str(), nullptr, 10);
    if (!port || port > 65535) return false;
    out.port = static_cast<uint16_t>(port);
    authority.erase(colon);
  }
  if (authority.size() > 253 || authority.back() == '.') return false;
  size_t label = 0;
  for (unsigned char c : authority) {
    if (c == '.') {
      if (!label || label > 63) return false;
      label = 0;
    } else {
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
      ++label;
    }
  }
  if (!label || label > 63) return false;
  out.host = lower(authority);
  out.authority = out.host + (out.port == 443 ? "" : ":" + std::to_string(out.port));
  std::string path = end == std::string::npos ? "/" : raw.substr(end);
  path.erase(path.find('#') == std::string::npos ? path.size() : path.find('#'));
  if (path.empty() || path[0] != '/') path.insert(path.begin(), '/');
  out.target = pluginhttp::escapeUrl(path);
  return out.target.size() <= MAX_URL;
}
bool publicV4(uint32_t ip) {
  const unsigned a = ip >> 24, b = (ip >> 16) & 255, c = (ip >> 8) & 255;
  return !(a == 0 || a == 10 || a == 127 || a >= 224 || (a == 100 && b >= 64 && b <= 127) || (a == 169 && b == 254) ||
           (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 192 && b == 0 && (c == 0 || c == 2)) ||
           (a == 192 && b == 88 && c == 99) || (a == 198 && (b == 18 || b == 19 || (b == 51 && c == 100))) ||
           (a == 203 && b == 0 && c == 113));
}
bool requestHeaders(const pluginhttp::Headers& headers) {
  size_t bytes = 0;
  if (headers.size() > 64) return false;
  for (const auto& h : headers) {
    if (!token(h.first) || !fieldValue(h.second) || h.first.size() + h.second.size() + 4 > MAX_LINE) return false;
    bytes += h.first.size() + h.second.size() + 4;
    const auto name = lower(h.first);
    if (bytes > MAX_HEADERS || name == "host" || name == "connection" || name == "content-length" ||
        name == "transfer-encoding" || name == "proxy-authorization" || name == "proxy-connection" ||
        name == "upgrade" || name == "trailer" || name == "te" || name == "expect")
      return false;
  }
  return true;
}
#ifndef SIMULATOR
// Pin the first synchronized UTC anchor, as the owner's Browser does. Later
// settings/SNTP wall-clock edits cannot revive an expired certificate.
std::mutex timeMutex;
int64_t timeEpoch = 0, timeMono = 0;
time_t trustedTlsNow() {
  if (!halClock.hasSynchronizedSystemTime()) return 0;
  std::lock_guard<std::mutex> lock(timeMutex);
  const int64_t mono = esp_timer_get_time();
  if (!timeEpoch) {
    const time_t epoch = time(nullptr);
    if (epoch < 1735689600 || epoch > 4102444800LL) return 0;
    timeEpoch = epoch;
    timeMono = mono;
  }
  if (mono < timeMono) return 0;
  return static_cast<time_t>(timeEpoch + (mono - timeMono) / 1000000);
}
int compareDate(const mbedtls_x509_time& a, const tm& b) {
  const int x[] = {a.year, a.mon, a.day, a.hour, a.min, a.sec};
  const int y[] = {b.tm_year + 1900, b.tm_mon + 1, b.tm_mday, b.tm_hour, b.tm_min, b.tm_sec};
  for (size_t i = 0; i < 6; ++i)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}
// Same verified ESP transport primitive as HalLuaHttp: bundle, original SNI,
// numeric TCP, TLS 1.2 and explicit chain dates (prebuilt mbedTLS may omit dates).
struct Connection {
  esp_tls_t* tls = nullptr;
  uint32_t start;
  const std::function<bool()>& cancel;
  uint32_t budget;
  Connection(uint32_t s, const std::function<bool()>& c, uint32_t b = ELAPSED_MS) : start(s), cancel(c), budget(b) {}
  ~Connection() {
    if (tls) esp_tls_conn_destroy(tls);
  }
  bool stopped() const {
    return !networkAllowed.load() || uint32_t(millis() - start) >= budget || (cancel && cancel());
  }
  bool open(const Url& u) {
    if (stopped()) return reject(Failure::Stopped);
    if (!halClock.hasSynchronizedSystemTime()) return reject(Failure::Clock);
    const time_t epoch = trustedTlsNow();
    if (epoch < 1735689600 || epoch > 4102444800LL) return reject(Failure::Clock);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* answers = nullptr;
    const int rc = getaddrinfo(u.host.c_str(), nullptr, &hints, &answers);
    if (rc || !answers) {
      if (answers) freeaddrinfo(answers);
      return reject(Failure::Dns);
    }
    uint32_t chosen = 0;
    bool valid = true;
    for (auto* a = answers; a; a = a->ai_next) {
      if (a->ai_family != AF_INET || a->ai_addrlen < sizeof(sockaddr_in)) {
        valid = false;
        break;
      }
      const auto ip = reinterpret_cast<sockaddr_in*>(a->ai_addr)->sin_addr.s_addr;
      if (!publicV4(ntohl(ip))) {
        valid = false;
        break;
      }
      if (!chosen) chosen = ip;
    }
    freeaddrinfo(answers);
    if (!valid || !chosen) return reject(Failure::Address);
    if (stopped()) return reject(Failure::Stopped);
    char numeric[16];
    in_addr address{};
    address.s_addr = chosen;
    if (!inet_ntop(AF_INET, &address, numeric, sizeof(numeric))) return reject(Failure::Address);
    tls = esp_tls_init();
    if (!tls) return reject(Failure::TlsHandshake);
    esp_tls_cfg_t cfg{};
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.common_name = u.host.c_str();
    cfg.skip_common_name = false;
    cfg.tls_version = ESP_TLS_VER_TLS_1_2;
    cfg.timeout_ms = 1000;
    cfg.non_block = true;
#if defined(CROSSCUB_PLUGIN_HUB_DIAGNOSTIC)
    if (inDiagnostic()) LOG_INF("PDIA", "numeric=%s host=%s nameVerify=1 bundle=1", numeric, u.host.c_str());
#endif
    int result;
    do {
      if (stopped()) return reject(Failure::Stopped);
      result = esp_tls_conn_new_async(numeric, strlen(numeric), u.port, &cfg, tls);
      if (!result) delay(10);
    } while (!result);
    if (stopped()) return reject(Failure::Stopped);
    if (result != 1) {
#if defined(CROSSCUB_PLUGIN_HUB_DIAGNOSTIC)
      if (inDiagnostic()) {
        esp_tls_error_handle_t error = nullptr;
        int code = 0, flags = 0;
        if (esp_tls_get_error_handle(tls, &error) == ESP_OK) {
          const auto last = esp_tls_get_and_clear_last_error(error, &code, &flags);
          LOG_INF("PDIA", "tlsError=%d mbedtls=%d verifyFlags=%d", last, code, flags);
        }
      }
#endif
      return reject(Failure::TlsHandshake);
    }
    auto* ssl = static_cast<mbedtls_ssl_context*>(esp_tls_get_ssl_context(tls));
    if (!ssl || mbedtls_ssl_get_verify_result(ssl) != 0) return reject(Failure::TlsVerify);
    auto* cert = mbedtls_ssl_get_peer_cert(ssl);
    time_t now = trustedTlsNow();
    tm utc{};
    if (!now || !cert || !gmtime_r(&now, &utc)) return reject(Failure::TlsDate);
    size_t count = 0, total = 0;
    for (; cert; cert = cert->next) {
#if defined(CROSSCUB_PLUGIN_HUB_DIAGNOSTIC)
      if (inDiagnostic())
        LOG_INF("PDIA", "cert=%u from=%04d%02d%02d to=%04d%02d%02d utc=%lld", static_cast<unsigned>(count),
                cert->valid_from.year, cert->valid_from.mon, cert->valid_from.day, cert->valid_to.year,
                cert->valid_to.mon, cert->valid_to.day, static_cast<long long>(now));
#endif
      total += cert->raw.len;
      if (++count > 8 || total > 32768 || compareDate(cert->valid_from, utc) > 0 ||
          compareDate(cert->valid_to, utc) < 0)
        return reject(Failure::TlsDate);
    }
    return !stopped();
  }
  int transfer(uint8_t* data, size_t n, bool writing) {
    for (;;) {
      if (stopped()) return -1;
#if defined(CROSSCUB_PLUGIN_HUB_DIAGNOSTIC)
      if (writing && inDiagnostic()) ++diagnosticWriteCalls;
#endif
      const int rc = writing ? esp_tls_conn_write(tls, data, n) : esp_tls_conn_read(tls, data, n);
#if defined(CROSSCUB_PLUGIN_HUB_DIAGNOSTIC)
      if (writing && inDiagnostic() && rc > 0) diagnosticWrites += static_cast<size_t>(rc);
#endif
      if (rc != ESP_TLS_ERR_SSL_WANT_READ && rc != ESP_TLS_ERR_SSL_WANT_WRITE) return rc;
      delay(1);
    }
  }
  bool write(const std::string& s) {
    size_t pos = 0;
    while (pos < s.size()) {
      int n = transfer(reinterpret_cast<uint8_t*>(const_cast<char*>(s.data() + pos)),
                       std::min<size_t>(2048, s.size() - pos), true);
      if (n <= 0) return false;
      pos += static_cast<size_t>(n);
    }
    return !stopped();
  }
  // CRLF only; aggregate wire budget includes trailers and chunk metadata.
  bool line(std::string& s, size_t& budget) {
    s.clear();
    for (;;) {
      uint8_t c;
      if (!budget || transfer(&c, 1, false) != 1) return false;
      --budget;
      if (c == '\n') {
        if (s.empty() || s.back() != '\r') return false;
        s.pop_back();
        return true;
      }
      if (s.size() >= MAX_LINE || c == 0) return false;
      s += static_cast<char>(c);
    }
  }
};
#else
// Simulator has no ESP TLS backend. Never silently substitute plaintext.
struct Connection {
  Connection(uint32_t, const std::function<bool()>&) {}
  bool stopped() const { return true; }
  bool open(const Url&) { return false; }
  bool write(const std::string&) { return false; }
  bool line(std::string&, size_t&) { return false; }
  int transfer(uint8_t*, size_t, bool) { return -1; }
};
#endif
bool number(const std::string& s, unsigned base, size_t& value) {
  value = 0;
  if (s.empty()) return false;
  for (unsigned char c : s) {
    const unsigned d = c >= '0' && c <= '9'   ? c - '0'
                       : c >= 'a' && c <= 'f' ? c - 'a' + 10
                       : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                              : 255;
    if (d >= base || value > (SIZE_MAX - d) / base) return false;
    value = value * base + d;
  }
  return true;
}
// One exchange; never retries a potentially accepted mutation. Redirects are
// followed only for GET/HEAD, with fresh DNS vetting and no foreign headers.
int exchange(const std::string& initial, const std::string& method, const std::string& body,
             const pluginhttp::Headers& headers, const pluginhttp::StreamStart& onStart,
             const pluginhttp::StreamData& onData, bool& complete, size_t cap, const std::function<bool()>& cancel) {
  complete = false;
#if !defined(CROSSCUB_VERIFIED_PLUGIN_TRANSPORT) || CROSSCUB_VERIFIED_PLUGIN_TRANSPORT != 1
  // Source integration is not certificate qualification. Parent enables this
  // only after the actual ESP adapter's target crypto/compatibility gates.
#if defined(CROSSCUB_PLUGIN_HUB_DIAGNOSTIC) && !defined(SIMULATOR)
  if (!inDiagnostic() && !(method == "GET" && body.empty() && headers.empty() && diagnosticHubUrl(initial)))
    return fail(Failure::Qualification);
#else
  return fail(Failure::Qualification);
#endif
#endif
  Url origin;
  if (!onStart || !onData || !parseUrl(initial, origin) || !token(method) || body.size() > MAX_UPLOAD ||
      !requestHeaders(headers) || cap > MAX_BODY)
    return fail(Failure::Input);
  const uint32_t start = millis();
  Connection preparation(start, cancel);
  if (preparation.stopped()) return fail(Failure::Stopped);
#ifndef SIMULATOR
  // Reuse per-boot HAL synchronization, not persisted UI/RTC preferences.
  // HAL's 5s SNTP wait is not a total bound: mutex/DNS are synchronous.
#if defined(CROSSCUB_PLUGIN_HUB_DIAGNOSTIC)
  LOG_INF("PDIA", "prepare synchronized=%u", halClock.hasSynchronizedSystemTime());
#endif
  const bool timeReady = halClock.hasSynchronizedSystemTime() || halClock.syncSystemTimeFromNTP();
  // Pin immediately, before callbacks or later wall-clock edits can intervene.
  const time_t prepared = timeReady ? trustedTlsNow() : 0;
  if (preparation.stopped()) return fail(Failure::Stopped);
  if (!prepared) return fail(Failure::Clock);
#endif
  std::string current = initial;
  for (unsigned hop = 0; hop <= 3; ++hop) {
#if defined(CROSSCUB_PLUGIN_HUB_DIAGNOSTIC) && !defined(SIMULATOR)
    if (!inDiagnostic() && !diagnosticHubUrl(current)) return fail(Failure::Qualification);
#endif
    Url u;
    if (!parseUrl(current, u)) return fail(Failure::Input);
    Connection c(start, cancel);
    if (!c.open(u)) return -1;
    std::string wire = method + " " + u.target + " HTTP/1.1\r\nHost: " + u.authority +
                       "\r\nConnection: close\r\nUser-Agent: Crosscub-Plugin\r\n";
    if (u.authority == origin.authority)
      for (const auto& h : headers) wire += h.first + ": " + h.second + "\r\n";
    wire += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    if (!c.write(wire) || !c.write(body)) return fail(Failure::Write);
    size_t budget = MAX_HEADERS, length = 0, count = 0;
    std::string line, location;
    if (!c.line(line, budget) || line.size() < 12 ||
        (line.compare(0, 9, "HTTP/1.1 ") && line.compare(0, 9, "HTTP/1.0 ")) || line[9] < '1' || line[9] > '5' ||
        line[10] < '0' || line[10] > '9' || line[11] < '0' || line[11] > '9' || (line.size() > 12 && line[12] != ' '))
      return fail(Failure::Framing);
    const int status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
    if (status < 200) return -1;  // interim responses are not silently misframed
    bool haveLength = false, chunked = false, haveEncoding = false, haveLocation = false;
    pluginhttp::Headers response;
    for (;;) {
      if (!c.line(line, budget)) return -1;
      if (line.empty()) break;
      if (++count > 64) return -1;
      const auto colon = line.find(':');
      if (colon == std::string::npos || !token(line.substr(0, colon))) return -1;
      std::string name = lower(line.substr(0, colon)), value = line.substr(colon + 1);
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
      while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
      if (!fieldValue(value)) return -1;
      if (name == "content-length") {
        if (haveLength || !number(value, 10, length)) return -1;
        haveLength = true;
      }
      if (name == "transfer-encoding") {
        if (haveEncoding || lower(value) != "chunked") return -1;
        haveEncoding = chunked = true;
      }
      if (name == "location") {
        if (haveLocation) return -1;
        haveLocation = true;
        location = value;
      }
      response.emplace_back(name, value);
    }
    if (chunked && haveLength) return fail(Failure::Framing);
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      if ((method == "GET" || method == "HEAD") && body.empty() && !location.empty()) {
        if (hop == 3) return -1;
        if (location.compare(0, 2, "//") == 0)
          current = "https:" + location;
        else if (location.find("://") != std::string::npos)
          current = location;
        else if (location[0] == '/')
          current = "https://" + u.authority + location;
        else if (location[0] == '?')
          current = "https://" + u.authority + u.target.substr(0, u.target.find('?')) + location;
        else {
          std::string path = u.target.substr(0, u.target.find('?'));
          current = "https://" + u.authority + path.substr(0, path.rfind('/') + 1) + location;
        }
        continue;  // close without draining an untrusted redirect body
      }
    }
    const bool noBody = method == "HEAD" || status == 204 || status == 304;
    if (!noBody && haveLength && length > cap) return -1;
    if (c.stopped() ||
        !onStart(status, response, haveLength && !chunked ? static_cast<long>(std::min<size_t>(length, LONG_MAX)) : -1))
      return -1;
    size_t received = 0;
    auto fixed = [&](size_t remaining) {
      if (remaining > cap - received) return false;
      uint8_t buf[2048];
      while (remaining) {
        int n = c.transfer(buf, std::min(sizeof(buf), remaining), false);
        if (n <= 0 || !onData(buf, static_cast<size_t>(n)) || c.stopped()) return false;
        remaining -= static_cast<size_t>(n);
        received += static_cast<size_t>(n);
      }
      return true;
    };
    if (!noBody && chunked) {
      budget = 256 * 1024;  // separate bounded chunk-metadata budget, not retained headers
      for (;;) {
        if (!c.line(line, budget)) return -1;
        const size_t semi = line.find(';');
        size_t n;
        if (!number(line.substr(0, semi), 16, n)) return -1;
        if (!n) {
          for (;;) {
            if (!c.line(line, budget)) return -1;
            if (line.empty()) break;
            // No unreviewed trailer semantics (auth, framing, cookies).
            return -1;
          }
          break;
        }
        if (!fixed(n) || !c.line(line, budget) || !line.empty()) return -1;
      }
    } else if (!noBody && haveLength) {
      if (!fixed(length)) return -1;
    } else if (!noBody) {
      uint8_t buf[2048];
      for (;;) {
        int n = c.transfer(buf, sizeof(buf), false);
        if (n < 0) return -1;
        if (!n) break;
        if (static_cast<size_t>(n) > cap - received || !onData(buf, static_cast<size_t>(n))) return -1;
        received += static_cast<size_t>(n);
      }
    }
    if (c.stopped()) return -1;
    complete = true;
    return status;
  }
  return -1;
}
}  // namespace

namespace pluginhttp {

// True when a dotted-path segment addresses an array index.
static bool segIsIndex(const std::string& seg) { return !seg.empty() && isdigit(static_cast<unsigned char>(seg[0])); }

void splitPath(const std::string& dotted, std::vector<std::string>& out) {
  out.clear();
  out.reserve(static_cast<size_t>(std::count(dotted.begin(), dotted.end(), '.')) + 1);
  size_t start = 0;
  while (start <= dotted.size()) {
    const size_t dot = dotted.find('.', start);
    if (dot == std::string::npos) {
      if (start < dotted.size()) out.push_back(dotted.substr(start));
      break;
    }
    if (dot > start) out.push_back(dotted.substr(start, dot - start));
    start = dot + 1;
  }
}

JsonVariantConst resolvePath(JsonVariantConst node, const std::string& dotted) {
  if (dotted.empty()) return node;
  std::vector<std::string> segs;
  splitPath(dotted, segs);
  for (const auto& seg : segs) {
    if (node.isNull()) break;
    if (segIsIndex(seg)) {
      node = node[atoi(seg.c_str())];
    } else {
      node = node[seg.c_str()];
    }
  }
  return node;
}

std::string variantToString(JsonVariantConst v) {
  if (v.isNull()) return "";
  if (v.is<const char*>()) return v.as<const char*>();
  char buf[24];
  if (v.is<long long>() || v.is<int>()) {
    snprintf(buf, sizeof(buf), "%lld", v.as<long long>());
    return buf;
  }
  return "";
}

void substituteAll(std::string& s, const char* key, const std::string& value) {
  const size_t keyLen = strlen(key);
  size_t pos = 0;
  while ((pos = s.find(key, pos)) != std::string::npos) {
    s.replace(pos, keyLen, value);
    pos += value.size();
  }
}

void readRequest(JsonVariantConst node, const char* defaultMethod, RequestSpec& out) {
  out.url = node["url"] | "";
  out.method = node["method"] | defaultMethod;
  out.body = node["body"] | "";
  readHeaders(node["headers"], out.headers);
}

void readHeaders(JsonVariantConst node, Headers& out) {
  out.clear();
  const JsonObjectConst headers = node.as<JsonObjectConst>();
  out.reserve(headers.size());
  for (JsonPairConst kv : headers) {
    out.emplace_back(kv.key().c_str(), kv.value().as<const char*>() ? kv.value().as<const char*>() : "");
  }
}

std::string urlEncodeQuery(const std::string& s) {
  std::string out;
  out.reserve(s.size() * 3);
  for (const unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string escapeUrl(const std::string& url) {
  const auto kept = [](const unsigned char c) { return c > 0x20 && c < 0x7F; };
  const size_t escaped =
      std::count_if(url.begin(), url.end(), [&](const char c) { return !kept(static_cast<unsigned char>(c)); });
  std::string out;
  out.reserve(url.size() + 2 * escaped);
  for (const unsigned char c : url) {
    if (kept(c)) {
      out += static_cast<char>(c);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string inPluginDir(const std::string& pluginDir, const char* path) {
  if (!path || !path[0] || path[0] == '/') return path ? path : "";
  return pluginDir + "/" + path;
}

// A manifest may not point the firmware at a credential store.
static bool allowedFile(const std::string& file) {
  if (protectedpaths::isPluginPath(file)) return true;
  LOG_ERR("PHTP", "Manifest file not allowed: %s", file.c_str());
  return false;
}

bool loadTokenFromFile(const std::string& file, const std::string& path, std::string& out) {
  out.clear();
  if (file.empty()) return true;  // token-less plugin
  if (!allowedFile(file)) return false;
  std::string raw;
  if (!Storage.readFileToString("PHTP", file, MAX_TOKEN_FILE_SIZE, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return false;
  out = variantToString(resolvePath(doc.as<JsonVariantConst>(), path));
  return !out.empty();
}

bool saveTokenToFile(const std::string& file, const std::string& path, const std::string& value) {
  if (file.empty()) return false;
  if (!allowedFile(file)) return false;
  JsonDocument doc;
  // Build the nesting the read path expects (numeric segments unsupported).
  std::vector<std::string> segs;
  splitPath(path, segs);
  if (segs.empty()) return false;
  JsonVariant node = doc.to<JsonObject>();
  for (size_t i = 0; i + 1 < segs.size(); i++) node = node[segs[i]].to<JsonObject>();
  node[segs.back()] = value;
  String out;
  serializeJson(doc, out);
  if (doc.overflowed() || out.length() > MAX_TOKEN_FILE_SIZE || out.length() != measureJson(doc)) {
    LOG_ERR("PHTP", "Invalid or oversized token JSON");
    return false;
  }
  if (!Storage.writeFile(file.c_str(), out)) {
    LOG_ERR("PHTP", "Failed to save token file");
    return false;
  }
  return true;
}

void loadConfigFile(const std::string& file, Headers& out) {
  out.clear();
  if (file.empty()) return;
  if (!allowedFile(file)) return;
  std::string raw;
  if (!Storage.readFileToString("PHTP", file, MAX_TOKEN_FILE_SIZE, raw)) return;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return;
  const JsonObjectConst fields = doc.as<JsonObjectConst>();
  out.reserve(fields.size());
  for (JsonPairConst kv : fields) {
    out.emplace_back(kv.key().c_str(), variantToString(kv.value()));
  }
}

void setNetworkEnabled(bool enabled) { networkAllowed.store(enabled); }
bool networkEnabled() { return networkAllowed.load(); }

int request(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
            const std::string& body, const Headers& headers, String& out, const size_t maxResponse,
            Headers* responseHeaders, const std::function<bool()>& shouldAbort) {
  (void)session;  // Compatibility parameter: never trust/reuse a caller's insecure cached socket.
  WifiPowerSaveGuard psGuard;
  out.remove(0);
  if (responseHeaders) responseHeaders->clear();
  bool complete = false;
  const int status = exchange(
      url, method, body, headers,
      [&](int, const Headers& received, long) {
        if (responseHeaders) *responseHeaders = received;
        return true;
      },
      [&](const uint8_t* data, size_t n) {
        return n <= maxResponse - out.length() && out.concat(reinterpret_cast<const char*>(data), n);
      },
      complete, std::min(maxResponse, MAX_BODY), shouldAbort);
  if (status < 0 || !complete) {
    out.remove(0);
    if (responseHeaders) responseHeaders->clear();
    return -1;
  }
  // OAuth polling and authentication documents retain their 4xx bodies.
  return status;
}

int requestStream(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                  const std::string& body, const Headers& headers, const StreamStart& onStart, const StreamData& onData,
                  bool& complete, const std::function<bool()>& shouldAbort, const size_t maxResponse) {
  (void)session;
  WifiPowerSaveGuard psGuard;
  return exchange(url, method, body, headers, onStart, onData, complete, std::min(maxResponse, MAX_BODY), shouldAbort);
}

#if defined(CROSSCUB_PLUGIN_HUB_DIAGNOSTIC) && !defined(SIMULATOR)
void runDiagnostics() {
  // A task identity confines the gate exception to these fixed probes. Other tasks
  // remain limited to the exact public Hub index URLs; no test CA is installed.
  if (diagnosticTask.load()) return;
  diagnosticTask.store(xTaskGetCurrentTaskHandle());
  static const char* const probes[] = {
      "https://raw.githubusercontent.com/crosspoint-reader/crosspoint-plugin-hub/main/catalog.json",
      "https://pluginhub.crosspointreader.com/device.json",
      "https://wrong.host.badssl.com/",
      "https://self-signed.badssl.com/",
      "https://untrusted-root.badssl.com/",
      "https://expired.badssl.com/",
      "https://127.0.0.1/"};
  LOG_INF("PDIA", "begin synchronized=%u gate=closed hubReadOnly=1", halClock.hasSynchronizedSystemTime());
  for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
    diagnosticStage = 0;
    diagnosticWrites = 0;
    diagnosticWriteCalls = 0;
    size_t received = 0;
    bool complete = false;
    const uint32_t started = millis();
    const int status = requestStream(
        nullptr, probes[i], "GET", "", {}, [](int, const Headers&, long) { return true; },
        [&](const uint8_t*, size_t n) {
          received += n;
          return true;
        },
        complete, nullptr, 256 * 1024);
    LOG_INF("PDIA",
            "case=%u status=%d complete=%u stage=%u httpWritten=%u writeCalls=%u bodyBytes=%u ms=%u "
            "synchronized=%u heap=%u largest=%u psram=%u psramLargest=%u stack=%u",
            i, status, complete, diagnosticStage, static_cast<unsigned>(diagnosticWrites),
            static_cast<unsigned>(diagnosticWriteCalls), static_cast<unsigned>(received), millis() - started,
            halClock.hasSynchronizedSystemTime(), ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram(),
            ESP.getMaxAllocPsram(), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  }
  diagnosticStage = 0;
  diagnosticWrites = 0;
  diagnosticWriteCalls = 0;
  bool complete = false;
  const int cancelled = requestStream(
      nullptr, probes[0], "GET", "", {}, [](int, const Headers&, long) { return true; },
      [](const uint8_t*, size_t) { return true; }, complete, [] { return true; }, 256 * 1024);
  LOG_INF("PDIA", "cancel status=%d stage=%u httpWritten=%u", cancelled, diagnosticStage,
          static_cast<unsigned>(diagnosticWrites));
  LOG_INF("PDIA", "end missing=future-leaf,invalid-date-intermediate,blocked-ntp,wallclock-rewind");
  diagnosticTask.store(nullptr);
}
#endif

int requestToFile(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                  const std::string& body, const Headers& headers, const char* destPath, const size_t maxResponse,
                  const std::function<bool()>& shouldAbort) {
  if (!destPath || !allowedFile(destPath)) return -1;
  HalFile file;
  bool opened = false, complete = false;
  const int status = requestStream(
      session, url, method, body, headers,
      [&](int, const Headers&, long) {
        opened = Storage.openFileForWrite("PHTP", destPath, file);
        return opened;
      },
      [&](const uint8_t* data, size_t n) { return file.write(data, n) == n; }, complete, shouldAbort, maxResponse);
  bool saved = opened && complete && status >= 0;
  if (opened) {
    const bool synced = file.sync();
    const bool closed = file.close();
    saved = saved && synced && closed;
  }
  if (!networkAllowed.load() || (shouldAbort && shouldAbort())) saved = false;
  if (!saved) {
    if (opened) Storage.remove(destPath);
    return -1;
  }
  return status;
}

bool mintPasswordToken(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                       const std::string& body, const Headers& headers, const std::string& tokenPath,
                       std::string& outToken, const std::function<bool()>& shouldAbort) {
  outToken.clear();
  String response;
  constexpr size_t MAX_AUTH_RESPONSE = 8 * 1024;
  const int status = request(session, url, method, body, headers, response, MAX_AUTH_RESPONSE, nullptr, shouldAbort);
  if (status < 200 || status >= 300) return false;
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok || (shouldAbort && shouldAbort())) return false;
  outToken = variantToString(resolvePath(doc.as<JsonVariantConst>(), tokenPath));
  return !outToken.empty();
}

}  // namespace pluginhttp

// clang-format off: the implementation fragment consumes notes:: declarations from the wire header.
#include "network/NotesRealtimeWire.h"
#include "NotesRealtimeTransport.inc"
// clang-format on
