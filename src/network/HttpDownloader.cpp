#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <base64.h>
#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>
#endif
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <strings.h>

#include <cstdio>
#include <functional>
#include <string>
#include <utility>

#include "AppVersion.h"
#include "network/HttpRedirectPolicy.h"
#include "network/WifiPowerSaveGuard.h"

namespace {
int g_lastStatus = 0;
bool g_lastRedirected = false;
constexpr size_t PROGRESS_UPDATE_BYTES = 64 * 1024;
constexpr uint32_t PROGRESS_UPDATE_MS = 250;
constexpr int HTTP_RX_BUF = 4096;
constexpr int HTTP_TX_BUF = 1024;
constexpr int HTTP_TIMEOUT_MS = 60000;
constexpr int HTTP_READ_POLL_TIMEOUT_MS = 5000;
constexpr uint32_t DOWNLOAD_IDLE_TIMEOUT_MS = 30000;
constexpr size_t DEFAULT_DOWNLOAD_BUFFER_SIZE = 2048;
constexpr uint8_t MAX_REDIRECTS = 5;

void logNetworkState(const char* phase) {
  LOG_DBG("HTTP", "%s: heap free=%u maxAlloc=%u wifi=%d rssi=%d", phase, ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          static_cast<int>(WiFi.status()), WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
}

void logDownloadState(const char* phase, const size_t downloaded, const size_t total, const uint32_t idleMs) {
  LOG_ERR("HTTP", "%s after %zu/%zu bytes (idle=%lu ms, timeout=%lu ms)", phase, downloaded, total,
          static_cast<unsigned long>(idleMs), static_cast<unsigned long>(DOWNLOAD_IDLE_TIMEOUT_MS));
  logNetworkState(phase);
}

bool isRedirect(const int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

struct ResponseHeaders {
  std::string location, authenticate;
};

esp_err_t captureLocationHeader(esp_http_client_event_t* evt) {
  auto* headers = static_cast<ResponseHeaders*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_HEADER && headers && evt->header_key && evt->header_value) {
    if (strcasecmp(evt->header_key, "Location") == 0) headers->location.assign(evt->header_value);
    if (strcasecmp(evt->header_key, "WWW-Authenticate") == 0) {
      if (!headers->authenticate.empty()) headers->authenticate += ", ";
      headers->authenticate += evt->header_value;
    }
  }
  return ESP_OK;
}

bool isCancelRequested(bool* cancelFlag, const HttpDownloader::CancelCallback& shouldCancel) {
  if (cancelFlag && *cancelFlag) return true;
  if (shouldCancel && shouldCancel()) {
    if (cancelFlag) *cancelFlag = true;
    return true;
  }
  return false;
}

class ProgressNotifier {
 public:
  explicit ProgressNotifier(const HttpDownloader::ProgressCallback& progress) : progress_(&progress) {}

  void setTotal(const size_t total) { total_ = total; }

  void notify(size_t downloaded, bool force) {
    if (!progress_ || !*progress_ || total_ == 0) return;

    const uint32_t now = millis();
    if (force || downloaded == total_ || downloaded - lastProgressBytes_ >= PROGRESS_UPDATE_BYTES ||
        now - lastProgressMs_ >= PROGRESS_UPDATE_MS) {
      lastProgressBytes_ = downloaded;
      lastProgressMs_ = now;
      (*progress_)(downloaded, total_);
    }
  }

 private:
  size_t total_ = 0;
  size_t lastProgressBytes_ = 0;
  uint32_t lastProgressMs_ = 0;
  const HttpDownloader::ProgressCallback* progress_ = nullptr;
};

struct Sink {
  std::function<bool(const uint8_t*, size_t)> write;
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  HttpDownloader::CancelCallback shouldCancel;
  size_t resumeOffset = 0;
  size_t downloaded = 0;
  size_t total = 0;
  bool rangeIgnored = false;
  const HttpDownloader::DownloadOptions* options = nullptr;
};

void setRequestHeaders(esp_http_client_handle_t client, const std::string& username, const std::string& password,
                       size_t resumeOffset, bool sendAuthorization) {
  esp_http_client_set_header(client, "User-Agent", "Crosscub-ESP32-" CROSSSMUDGE_VERSION);
  esp_http_client_set_header(client, "Connection", "close");
  if (resumeOffset > 0) {
    char rangeHeader[40];
    snprintf(rangeHeader, sizeof(rangeHeader), "bytes=%zu-", resumeOffset);
    esp_http_client_set_header(client, "Range", rangeHeader);
    LOG_DBG("HTTP", "Resuming download at byte %zu", resumeOffset);
  }
  if (sendAuthorization) {
    const std::string credentials = username + ":" + password;
    const String header = "Basic " + base64::encode(credentials.c_str());
    esp_http_client_set_header(client, "Authorization", header.c_str());
  }
}

void logTlsError(esp_http_client_handle_t client, const char* phase) {
  int tlsError = 0;
  int tlsFlags = 0;
  const esp_err_t err = esp_http_client_get_and_clear_last_tls_error(client, &tlsError, &tlsFlags);
  if (err != ESP_OK || tlsError != 0 || tlsFlags != 0) {
    const int tlsCode = tlsError < 0 ? -tlsError : tlsError;
    LOG_ERR("HTTP", "%s TLS error: err=%s mbedtls=0x%x flags=0x%x", phase, esp_err_to_name(err), tlsCode, tlsFlags);
  }
}

HttpDownloader::DownloadError runGetDefault(const std::string& url, const std::string& username,
                                            const std::string& password,
                                            const HttpRedirectPolicy::Url& credentialOrigin, const bool hasCredentials,
                                            Sink& sink, const size_t bufferSize) {
  std::string currentUrl = url;

  for (uint8_t hop = 0; hop < MAX_REDIRECTS; ++hop) {
    if (isCancelRequested(sink.cancelFlag, sink.shouldCancel)) return HttpDownloader::ABORTED;
    HttpRedirectPolicy::Url currentOrigin;
    const bool currentParsed = HttpRedirectPolicy::parseUrl(currentUrl, currentOrigin);
    const bool sendAuthorization =
        currentParsed && HttpRedirectPolicy::shouldSendAuthorization(currentOrigin, credentialOrigin, hasCredentials);
    if (!currentParsed) return HttpDownloader::HTTP_ERROR;
    ResponseHeaders responseHeaders;

    esp_http_client_config_t config = {};
    config.url = currentUrl.c_str();
    config.buffer_size = HTTP_RX_BUF;
    config.buffer_size_tx = HTTP_TX_BUF;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = false;
    config.disable_auto_redirect = true;
    config.skip_cert_common_name_check = false;
    config.event_handler = captureLocationHeader;
    config.user_data = &responseHeaders;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
      LOG_ERR("HTTP", "Client init failed");
      logNetworkState("Client init failure");
      return HttpDownloader::HTTP_ERROR;
    }

    setRequestHeaders(client, username, password, sink.resumeOffset, sendAuthorization);

    const auto* options = sink.options;
    const bool same = HttpRedirectPolicy::sameOrigin(currentOrigin, credentialOrigin);
    if (same && options) {
      for (const auto& header : options->headers)
        esp_http_client_set_header(client, header.first.c_str(), header.second.c_str());
      if (!options->bearer.empty()) {
        const std::string auth = "Bearer " + options->bearer;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
      }
    }
    if (options && !options->accept.empty()) esp_http_client_set_header(client, "Accept", options->accept.c_str());
    if (options && !options->acceptLanguage.empty())
      esp_http_client_set_header(client, "Accept-Language", options->acceptLanguage.c_str());
    const std::string* requestBody = nullptr;
    if (options) {
      requestBody = options->body;
      if (requestBody)
        esp_http_client_set_method(client, strcmp(options->method, "PUT") == 0 ? HTTP_METHOD_PUT : HTTP_METHOD_POST);
    }
    const size_t bodySize = requestBody ? requestBody->size() : 0;
    esp_err_t err = esp_http_client_open(client, bodySize);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "Open failed: %s", esp_err_to_name(err));
      logTlsError(client, "Open failure");
      logNetworkState("Open failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    if (requestBody) {
      size_t sent = 0;
      while (sent < bodySize) {
        if (isCancelRequested(sink.cancelFlag, sink.shouldCancel)) {
          esp_http_client_cleanup(client);
          return HttpDownloader::ABORTED;
        }
        const int n = esp_http_client_write(client, requestBody->data() + sent, bodySize - sent);
        if (n <= 0) {
          esp_http_client_cleanup(client);
          return HttpDownloader::HTTP_ERROR;
        }
        sent += static_cast<size_t>(n);
      }
    }
    int64_t responseLength = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    g_lastStatus = status;
    g_lastRedirected = !HttpRedirectPolicy::sameOrigin(currentOrigin, credentialOrigin);
    if (options && options->statusOut) *options->statusOut = status;
    if (options && options->wwwAuthenticateOut) *options->wwwAuthenticateOut = responseHeaders.authenticate;
    if (responseLength < 0) {
      LOG_ERR("HTTP", "Fetch headers failed: %lld", static_cast<long long>(responseLength));
      logNetworkState("Fetch headers failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    if (isRedirect(status)) {
      if (requestBody) {
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (responseHeaders.location.empty()) {
        LOG_ERR("HTTP", "Redirect missing Location header");
        logNetworkState("Redirect missing Location");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }

      const std::string redirectUrl = HttpRedirectPolicy::buildRedirectUrl(currentUrl, responseHeaders.location);
      HttpRedirectPolicy::Url redirect;
      if (!HttpRedirectPolicy::parseUrl(redirectUrl, redirect)) {
        LOG_ERR("HTTP", "Rejected redirect with unsupported Location");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (currentParsed && !HttpRedirectPolicy::isAllowedRedirect(currentOrigin, redirect)) {
        LOG_ERR("HTTP", "Rejected HTTPS downgrade redirect to %s", redirect.host.c_str());
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      currentUrl = redirectUrl;
      LOG_DBG("HTTP", "Redirecting to: %s", redirect.host.c_str());
      esp_http_client_cleanup(client);
      continue;
    }

    const bool isResumeResponse = sink.resumeOffset > 0 && status == 206;
    if (!(status >= 200 && status < 300) && !isResumeResponse &&
        !(options && options->captureErrorBody && status == 401)) {
      LOG_ERR("HTTP", "Unexpected status: %d", status);
      logNetworkState("Unexpected status");
      esp_http_client_cleanup(client);
      return (status == 401 || status == 403) ? HttpDownloader::UNAUTHORIZED : HttpDownloader::HTTP_ERROR;
    }
    if (sink.resumeOffset > 0 && !isResumeResponse) {
      LOG_DBG("HTTP", "Server ignored range request; restarting download");
      sink.rangeIgnored = true;
      sink.resumeOffset = 0;
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    const size_t bodyLength = responseLength > 0 ? static_cast<size_t>(responseLength) : 0;
    sink.total = bodyLength > 0 ? sink.resumeOffset + bodyLength : 0;
    sink.downloaded = sink.resumeOffset;
    if (sink.total > 0) {
    } else {
    }
#ifdef ESP_ERR_HTTP_EAGAIN
    err = esp_http_client_set_timeout_ms(client, HTTP_READ_POLL_TIMEOUT_MS);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "Failed to set read timeout: %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
#endif

    auto buffer = makeUniqueNoThrow<char[]>(bufferSize);
    if (!buffer) {
      LOG_ERR("HTTP", "Failed to allocate %zu byte download buffer", bufferSize);
      logNetworkState("Download buffer allocation failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    ProgressNotifier progressNotifier(sink.progress);
    progressNotifier.setTotal(sink.total);
#ifdef ESP_ERR_HTTP_EAGAIN
    uint32_t lastReadMs = millis();
#endif
    while (true) {
      if (isCancelRequested(sink.cancelFlag, sink.shouldCancel)) {
        esp_http_client_cleanup(client);
        return HttpDownloader::ABORTED;
      }

      const int bytesRead = esp_http_client_read(client, buffer.get(), bufferSize);
      if (bytesRead < 0) {
#ifdef ESP_ERR_HTTP_EAGAIN
        if (bytesRead == -ESP_ERR_HTTP_EAGAIN) {
          const uint32_t idleMs = millis() - lastReadMs;
          if (idleMs >= DOWNLOAD_IDLE_TIMEOUT_MS) {
            logDownloadState("Read timed out", sink.downloaded, sink.total, idleMs);
            esp_http_client_cleanup(client);
            return HttpDownloader::HTTP_ERROR;
          }
          delay(1);
          continue;
        }
#endif
        LOG_ERR("HTTP", "Read error after %zu/%zu bytes", sink.downloaded, sink.total);
        logNetworkState("Read error");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (bytesRead == 0) break;

      if (!sink.write(reinterpret_cast<const uint8_t*>(buffer.get()), static_cast<size_t>(bytesRead))) {
        LOG_ERR("HTTP", "Write failed after %zu/%zu bytes", sink.downloaded, sink.total);
        logNetworkState("Write failure");
        esp_http_client_cleanup(client);
        return HttpDownloader::FILE_ERROR;
      }

      sink.downloaded += static_cast<size_t>(bytesRead);
#ifdef ESP_ERR_HTTP_EAGAIN
      lastReadMs = millis();
#endif
      if (sink.total > 0 && sink.total <= PROGRESS_UPDATE_BYTES) {
      }
      progressNotifier.notify(sink.downloaded, false);
      if (sink.total > 0 && sink.downloaded >= sink.total) break;
      delay(0);
    }

    const bool complete = esp_http_client_is_complete_data_received(client);
    esp_http_client_cleanup(client);
    progressNotifier.notify(sink.downloaded, true);
    if (!complete) {
      LOG_ERR("HTTP", "Incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
      logNetworkState("Incomplete transfer");
      return HttpDownloader::HTTP_ERROR;
    }

    return HttpDownloader::OK;
  }

  LOG_ERR("HTTP", "Redirect limit exceeded");
  logNetworkState("Redirect limit exceeded");
  return HttpDownloader::HTTP_ERROR;
}

HttpDownloader::DownloadError runGet(const std::string& url, const std::string& username, const std::string& password,
                                     const std::string_view authorizationOrigin, Sink& sink, const size_t bufferSize,
                                     const HttpDownloader::Transport transport) {
  HttpRedirectPolicy::Url credentialOrigin;
  const std::string_view credentialUrl = authorizationOrigin.empty() ? std::string_view(url) : authorizationOrigin;
  const bool hasCredentials = !username.empty() && !password.empty();
  if (!HttpRedirectPolicy::parseUrl(credentialUrl, credentialOrigin)) return HttpDownloader::HTTP_ERROR;
  g_lastStatus = 0;
  g_lastRedirected = false;
  (void)transport;  // Verified ESP bundle transport; never opt out of peer verification.
  return runGetDefault(url, username, password, credentialOrigin, hasCredentials, sink, bufferSize);
}
}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  return fetchUrl(
      url, [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; }, username,
      password);
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  outContent.clear();
  return fetchUrl(
      url,
      [&outContent](const uint8_t* data, size_t len) {
        outContent.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      username, password);
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password) {
  return streamUrl(url, onData, nullptr, username, password) == OK;
}

HttpDownloader::DownloadError HttpDownloader::streamUrl(const std::string& url, const DataCallback& onData,
                                                        ProgressCallback progress, const std::string& username,
                                                        const std::string& password, DownloadOptions options) {
  WifiPowerSaveGuard wifiPowerSaveGuard;
  (void)wifiPowerSaveGuard;

  if (!onData) {
    LOG_ERR("HTTP", "Fetch failed: missing data callback");
    return HTTP_ERROR;
  }

  Sink sink;
  sink.options = &options;
  sink.write = onData;
  sink.progress = std::move(progress);
  sink.shouldCancel = std::move(options.shouldCancel);
  const size_t bufferSize = options.bufferSize > 0 ? options.bufferSize : DEFAULT_DOWNLOAD_BUFFER_SIZE;
  return runGet(url, username, password, options.authorizationOrigin, sink, bufferSize, options.transport);
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             DownloadOptions options) {
  WifiPowerSaveGuard wifiPowerSaveGuard;
  (void)wifiPowerSaveGuard;

  const size_t bufferSize = options.bufferSize > 0 ? options.bufferSize : DEFAULT_DOWNLOAD_BUFFER_SIZE;
  size_t resumeOffset = 0;
  if (options.resumePartial && Storage.exists(destPath.c_str())) {
    HalFile existingFile;
    if (Storage.openFileForRead("HTTP", destPath.c_str(), existingFile)) {
      resumeOffset = existingFile.fileSize();
      existingFile.close();
    }
  }

  if (resumeOffset == 0 && Storage.exists(destPath.c_str())) {
    Storage.remove(destPath.c_str());
  }

  Sink sink;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.shouldCancel = std::move(options.shouldCancel);
  sink.resumeOffset = resumeOffset;
  sink.options = &options;

  HalFile file;
  bool fileOpen = false;
  auto openOutputFile = [&]() {
    if (fileOpen) return true;
    if (sink.resumeOffset > 0) {
      file = Storage.open(destPath.c_str(), O_WRONLY | O_APPEND);
    } else {
      fileOpen = Storage.openFileForWrite("HTTP", destPath.c_str(), file);
      if (!fileOpen) {
        LOG_ERR("HTTP", "Failed to open file for writing");
        return false;
      }
    }
    fileOpen = static_cast<bool>(file);
    if (!fileOpen) {
      LOG_ERR("HTTP", "Failed to open file for writing");
    }
    return fileOpen;
  };

  sink.write = [&](const uint8_t* data, size_t len) { return openOutputFile() && file.write(data, len) == len; };

  DownloadError result =
      runGet(url, username, password, options.authorizationOrigin, sink, bufferSize, options.transport);
  if (sink.rangeIgnored) {
    if (fileOpen) {
      file.close();
      fileOpen = false;
    }
    Storage.remove(destPath.c_str());
    sink.rangeIgnored = false;
    sink.resumeOffset = 0;
    sink.downloaded = 0;
    sink.total = 0;
    sink.write = [&](const uint8_t* data, size_t len) { return openOutputFile() && file.write(data, len) == len; };
    result = runGet(url, username, password, options.authorizationOrigin, sink, bufferSize, options.transport);
  }

  if (fileOpen) {
    if (!file.sync()) result = FILE_ERROR;
    if (!file.close()) result = FILE_ERROR;
  }

  if (result != OK) {
    LOG_ERR("HTTP", "Transfer failed: error=%d downloaded=%zu expected=%zu preservePartial=%d resumePartial=%d",
            static_cast<int>(result), sink.downloaded, sink.total, options.preservePartial, options.resumePartial);
    if (result == ABORTED || !options.preservePartial) {
      Storage.remove(destPath.c_str());
    }
    return result;
  }

  if (sink.downloaded == 0) {
    LOG_ERR("HTTP", "Download failed: no data received");
    if (!options.preservePartial) {
      Storage.remove(destPath.c_str());
    }
    return HTTP_ERROR;
  }

  if (sink.total > 0 && sink.downloaded != sink.total) {
    LOG_ERR("HTTP", "Size mismatch: got %zu, expected %zu", sink.downloaded, sink.total);
    if (!options.preservePartial) {
      Storage.remove(destPath.c_str());
    }
    return HTTP_ERROR;
  }

  return OK;
}

int HttpDownloader::lastStatus() { return g_lastStatus; }
bool HttpDownloader::lastAnswerRedirected() { return g_lastRedirected; }
bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& cb, const std::string& user,
                              const std::string& password, const std::vector<Header>& headers) {
  FetchOptions options;
  options.username = user;
  options.password = password;
  options.headers = headers;
  return fetchUrl(url, cb, options);
}
bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& cb, const FetchOptions& options) {
  DownloadOptions d;
  d.headers = options.headers;
  d.bearer = options.bearer;
  d.accept = options.accept;
  d.acceptLanguage = options.acceptLanguage;
  d.statusOut = options.statusOut;
  d.wwwAuthenticateOut = options.wwwAuthenticateOut;
  d.captureErrorBody = options.captureErrorBody;
  if (d.statusOut) *d.statusOut = 0;
  if (d.wwwAuthenticateOut) d.wwwAuthenticateOut->clear();
  const auto result = streamUrl(url, cb, nullptr, options.username, options.password, d);
  return result == OK && lastStatus() >= 200 && lastStatus() < 300;
}
bool HttpDownloader::postForm(const std::string& url, const std::string& body, std::string& response, int* statusOut) {
  response.clear();
  DownloadOptions d;
  d.body = &body;
  d.statusOut = statusOut;
  d.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
  if (statusOut) *statusOut = 0;
  return streamUrl(
             url,
             [&](const uint8_t* data, size_t len) {
               if (len > 4096 - response.size()) return false;
               response.append(reinterpret_cast<const char*>(data), len);
               return true;
             },
             nullptr, "", "", d) == OK;
}

// Publication v1: no overwrite, retain both roots after ambiguous FAT rename.
// Existing destination replacement needs the coordinator's durable storage contract.
static HttpDownloader::DownloadError publishDownload(const std::string& part, const std::string& dest) {
  if (Storage.exists(dest.c_str())) return HttpDownloader::FILE_ERROR;
  HalFile file = Storage.open(part.c_str());
  if (!file) return HttpDownloader::FILE_ERROR;
  const bool acknowledged = file.rename(dest.c_str());
  const bool closed = file.close();
  return acknowledged && closed ? HttpDownloader::OK : HttpDownloader::PUBLICATION_UNCERTAIN;
}
HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& dest,
                                                             ProgressCallback progress, const bool* cancelFlag,
                                                             const std::string& user, const std::string& password,
                                                             const std::vector<Header>& headers,
                                                             bool downgradeRedirectsToHttp) {
  (void)downgradeRedirectsToHttp;  // API compatibility is not permission to downgrade HTTPS.
  const std::string part = dest + ".part";
  if (Storage.exists(dest.c_str()) || Storage.exists(part.c_str())) return FILE_ERROR;
  DownloadOptions d;
  d.headers = headers;
  d.shouldCancel = [cancelFlag] { return cancelFlag && *cancelFlag; };
  const auto result = downloadToFile(url, part, std::move(progress), nullptr, user, password, d);
  if (result != OK) return result;
  return publishDownload(part, dest);
}
HttpDownloader::DownloadError HttpDownloader::postToFile(const std::string& url, const std::string& body,
                                                         const std::string& type, const std::string& dest,
                                                         ProgressCallback progress, const bool* cancelFlag) {
  const std::string part = dest + ".part";
  if (Storage.exists(dest.c_str()) || Storage.exists(part.c_str())) return FILE_ERROR;
  DownloadOptions d;
  d.body = &body;
  d.headers.emplace_back("Content-Type", type);
  d.shouldCancel = [cancelFlag] { return cancelFlag && *cancelFlag; };
  const auto result = downloadToFile(url, part, std::move(progress), nullptr, "", "", d);
  if (result != OK) return result;
  return publishDownload(part, dest);
}
