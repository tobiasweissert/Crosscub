// Compiled with PluginHttp's existing verified Connection; no alternate TLS stack.
namespace pluginhttp {
bool notesRealtime(const std::string& key, const std::function<bool(std::string&)>& outgoing,
                   const std::function<bool(const std::string&)>& incoming, const std::function<bool()>& cancel) {
#ifdef SIMULATOR
  (void)key; (void)outgoing; (void)incoming; (void)cancel;
  return false;
#else
  if (!notes::validKey(key) || !outgoing || !incoming) return false;
  Connection c(millis(), cancel, 60000);
  if (c.stopped()) return false;
  if (!(halClock.hasSynchronizedSystemTime() || halClock.syncSystemTimeFromNTP()) || !trustedTlsNow()) return false;
  Url origin;
  if (!parseUrl("https://api.openai.com/v1/realtime?intent=transcription", origin) || !c.open(origin)) return false;
  char nonce[16]; esp_fill_random(nonce, sizeof(nonce));
  const std::string challenge = notes::base64(std::string(nonce, sizeof(nonce)));
  const std::string hashInput = challenge + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  unsigned char hash[20];
  if (mbedtls_sha1(reinterpret_cast<const unsigned char*>(hashInput.data()), hashInput.size(), hash)) return false;
  const auto expected = notes::base64(std::string(reinterpret_cast<char*>(hash), sizeof(hash)));
  if (!c.write("GET /v1/realtime?intent=transcription HTTP/1.1\r\nHost: api.openai.com\r\n"
               "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " +
               challenge + "\r\nAuthorization: Bearer " + key + "\r\n\r\n")) return false;
  size_t budget = 8192;
  std::string line;
  if (!c.line(line, budget) || line.compare(0, 13, "HTTP/1.1 101 ") != 0) return false;
  bool upgrade = false, connection = false, accepted = false, headersComplete = false;
  while (c.line(line, budget)) {
    if (line.empty()) { headersComplete = true; break; }
    const auto colon = line.find(':');
    if (colon == std::string::npos) return false;
    const auto name = lower(line.substr(0, colon));
    auto value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
    if (name == "upgrade") { if (upgrade || lower(value) != "websocket") return false; upgrade = true; }
    if (name == "connection") { if (connection || lower(value) != "upgrade") return false; connection = true; }
    if (name == "sec-websocket-accept") { if (accepted || value != expected) return false; accepted = true; }
    // No extensions/subprotocols were offered.
    if (name == "sec-websocket-extensions" || name == "sec-websocket-protocol") return false;
  }
  if (!headersComplete || !upgrade || !connection || !accepted) return false;
  notes::WebSocket wire;
  while (!c.stopped()) {
    std::string message;
    if (!outgoing(message)) return true;
    if (!message.empty() && !c.write(notes::WebSocket::frame(message, 1, esp_random()))) return false;
    char bytes[1024];
    const int count = esp_tls_conn_read(c.tls, bytes, sizeof(bytes));
    if (count == 0) return false;
    if (count < 0 && count != ESP_TLS_ERR_SSL_WANT_READ && count != ESP_TLS_ERR_SSL_WANT_WRITE) return false;
    if (count > 0 && !wire.feed(bytes, static_cast<size_t>(count))) return false;
    unsigned opcode = 0;
    int result;
    while ((result = wire.next(message, opcode)) == 1) {
      if (opcode == 8) return false;
      if (opcode == 9 && !c.write(notes::WebSocket::frame(message, 10, esp_random()))) return false;
      if (opcode == 1 && !incoming(message)) return true;
    }
    if (result < 0) return false;
    delay(1);
  }
  return false;
#endif
}
}  // namespace pluginhttp
