#include <cassert>
#include <iostream>

#include "HttpDownloader.h"
#include "HttpRedirectPolicy.h"
#include "stubs.h"
#include "sync.inc"
int main() {
  // Body presence, not byte count, selects GET versus POST/PUT. Empty uploads
  // must retain their method and must not be replayed through a redirect.
  const auto consume = [](const uint8_t*, size_t) { return true; };
  for (const char* method : {"POST", "PUT"}) {
    HttpDownloader::DownloadOptions request;
    request.method = method;
    fixture({{200, "ok", "", "", true}});
    assert(HttpDownloader::streamUrl("https://example.org/body", consume, nullptr, "", "", request) ==
           HttpDownloader::OK);
    assert(requests.size() == 1 && requests[0].method == 0 && requests[0].sent.empty() &&
           requests[0].openedBodySize == 0);
    for (const std::string& payload : {std::string(), std::string("a\0b", 3)}) {
      request.body = &payload;
      fixture({{200, "ok", "", "", true}});
      assert(HttpDownloader::streamUrl("https://example.org/body", consume, nullptr, "", "", request) ==
             HttpDownloader::OK);
      assert(requests.size() == 1 &&
             requests[0].method == (std::string(method) == "PUT" ? HTTP_METHOD_PUT : HTTP_METHOD_POST));
      assert(requests[0].openedBodySize == payload.size() && requests[0].sent == payload);
      fixture({{302, "", "https://example.org/redirect", "", true}, {200, "must-not-send", "", "", true}});
      assert(HttpDownloader::streamUrl("https://example.org/body", consume, nullptr, "", "", request) ==
             HttpDownloader::HTTP_ERROR);
      assert(requests.size() == 1 && nextResponse == 1 && requests[0].sent == payload);
    }
  }
  std::cout << "production HTTP: absent/empty/binary body GET/POST/PUT and no upload redirect replay PASS\n";
  fixture({{200, "{\"ok\":true}", "", "", true}});
  SyncHttpClient sync;
  assert(sync.begin("https://sync.example.org/progress"));
  sync.addHeader("x-auth-key", "canary-sync");
  assert(sync.sendRequest("PUT", "{\"progress\":0.5}") == 200);
  assert(sync.getString() == "{\"ok\":true}" && requests[0].method == HTTP_METHOD_PUT);
  assert(requests[0].headers.at("x-auth-key") == "canary-sync");
  assert(requests[0].sent == "{\"progress\":0.5}");
  fixture({});
  HttpDownloader::DownloadOptions cancelled;
  cancelled.shouldCancel = [] { return true; };
  assert(HttpDownloader::streamUrl(
             "https://example.org/book", [](const uint8_t*, size_t) { return true; }, nullptr, "", "", cancelled) ==
         HttpDownloader::ABORTED);
  assert(requests.empty());
  std::string body;
  HttpDownloader::FetchOptions options;
  options.username = "canary-user";
  options.password = "canary-password";
  options.bearer = "canary-token";
  options.headers = {{"X-Secret", "canary"}};
  options.captureErrorBody = true;
  std::string auth;
  options.wwwAuthenticateOut = &auth;
  fixture({{302, "", "https://cdn.example.org/book", "", true}, {200, "verified-body", "", "", true}});
  assert(HttpDownloader::fetchUrl(
      "https://example.org/start",
      [&](const uint8_t* b, size_t n) {
        body.append(reinterpret_cast<const char*>(b), n);
        return true;
      },
      options));
  assert(body == "verified-body" && requests.size() == 2);
  assert(requests[0].headers.at("Authorization") == "Bearer canary-token");
  assert(requests[1].headers.count("Authorization") == 0 && requests[1].headers.count("X-Secret") == 0);
  for (const auto& r : requests) {
    assert(r.config.crt_bundle_attach == esp_crt_bundle_attach);
    assert(r.config.disable_auto_redirect);
    assert(!r.config.skip_cert_common_name_check);
  }
  fixture({{302, "", "http://example.org/book", "", true}});
  assert(!HttpDownloader::fetchUrl("https://example.org/start", [](const uint8_t*, size_t) { return true; }, options));
  assert(requests.size() == 1);
  fixture({{401, "authentication-document", "", "Bearer realm=fixture", true}});
  body.clear();
  assert(!HttpDownloader::fetchUrl(
      "https://example.org/start",
      [&](const uint8_t* b, size_t n) {
        body.append(reinterpret_cast<const char*>(b), n);
        return true;
      },
      options));
  assert(body == "authentication-document" && auth == "Bearer realm=fixture" && HttpDownloader::lastStatus() == 401);
  fixture({{200, "partial", "", "", false}});
  assert(!HttpDownloader::fetchUrl("https://example.org/start", [](const uint8_t*, size_t) { return true; }));
  fixture({{200, "token-response", "", "", true}});
  body.clear();
  assert(HttpDownloader::postForm("https://example.org/token", "synthetic=value", body));
  assert(body == "token-response" && requests[0].method == HTTP_METHOD_POST && requests[0].sent == "synthetic=value");
  fixture({{200, std::string(4097, 'x'), "", "", true}});
  assert(!HttpDownloader::postForm("https://example.org/token", "synthetic=value", body));
  fixture({{200, "book", "", "", true}});
  renameAcknowledged = false;
  const auto result =
      HttpDownloader::downloadToFile("https://example.org/book", "/book", nullptr, static_cast<const bool*>(nullptr),
                                     "", "", std::vector<HttpDownloader::Header>{});
  assert(result == HttpDownloader::PUBLICATION_UNCERTAIN && files.count("/book.part") && files.count("/book"));
  const auto count = removeCalls;
  assert(HttpDownloader::downloadToFile("https://example.org/book", "/book", nullptr, static_cast<const bool*>(nullptr),
                                        "", "", std::vector<HttpDownloader::Header>{}) == HttpDownloader::FILE_ERROR);
  assert(removeCalls == count);
  std::cout << "production HTTP: bundle/hostname config, redirect credentials, HTTPS downgrade denial, 401 auth doc, "
               "incomplete framing, POST cap, FAT uncertainty PASS\n";
}
