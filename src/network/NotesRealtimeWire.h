#pragma once
#include <cstdint>
#include <string>

namespace notes {
inline bool validKey(const std::string& key) {
  if (key.empty() || key.size() > 512) return false;
  for (unsigned char c : key)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
      return false;
  return true;
}
inline std::string base64(const std::string& bytes) {
  static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  for (size_t i = 0; i < bytes.size(); i += 3) {
    const uint32_t a = static_cast<uint8_t>(bytes[i]);
    const uint32_t b = i + 1 < bytes.size() ? static_cast<uint8_t>(bytes[i + 1]) : 0;
    const uint32_t c = i + 2 < bytes.size() ? static_cast<uint8_t>(bytes[i + 2]) : 0;
    const uint32_t word = (a << 16) | (b << 8) | c;
    out += alphabet[word >> 18];
    out += alphabet[(word >> 12) & 63];
    out += i + 1 < bytes.size() ? alphabet[(word >> 6) & 63] : '=';
    out += i + 2 < bytes.size() ? alphabet[word & 63] : '=';
  }
  return out;
}
// Bounded RFC6455 message assembler. Control frames may interrupt fragments.
class WebSocket {
 public:
  static constexpr size_t LIMIT = 16384;
  bool feed(const char* p, size_t n) {
    if (n > LIMIT + 14 - input.size()) return false;
    input.append(p, n);
    return true;
  }
  int next(std::string& out, unsigned& op) {
    for (;;) {
      if (input.size() < 2) return 0;
      const auto a = static_cast<uint8_t>(input[0]), b = static_cast<uint8_t>(input[1]);
      op = a & 15;
      const bool fin = (a & 128) != 0;
      if ((a & 112) || (b & 128) || !(op == 0 || op == 1 || op == 8 || op == 9 || op == 10)) return -1;
      size_t n = b & 127, header = 2;
      if (n == 127) return -1;
      if (n == 126) {
        if (input.size() < 4) return 0;
        n = (static_cast<uint8_t>(input[2]) << 8) | static_cast<uint8_t>(input[3]);
        if (n < 126) return -1;
        header = 4;
      }
      if (n > LIMIT || (op >= 8 && (!fin || n > 125 || (op == 8 && n == 1)))) return -1;
      if (input.size() < header + n) return 0;
      if (op >= 8) {
        out = input.substr(header, n);
        input.erase(0, header + n);
        return 1;
      }
      if ((op == 0 && !fragmented) || (op == 1 && fragmented) || n > LIMIT - partial.size()) return -1;
      partial.append(input, header, n);
      input.erase(0, header + n);
      fragmented = !fin;
      if (fin) {
        out.swap(partial);
        partial.clear();
        op = 1;
        return 1;
      }
    }
  }
  static std::string frame(const std::string& payload, unsigned op, uint32_t mask) {
    if (payload.size() > LIMIT || (op >= 8 && payload.size() > 125)) return {};
    std::string out;
    out += static_cast<char>(128 | op);
    if (payload.size() < 126) out += static_cast<char>(128 | payload.size());
    else {
      out += static_cast<char>(128 | 126);
      out += static_cast<char>(payload.size() >> 8);
      out += static_cast<char>(payload.size());
    }
    const size_t start = out.size();
    for (unsigned i = 0; i < 4; ++i) out += static_cast<char>(mask >> (i * 8));
    for (size_t i = 0; i < payload.size(); ++i) out += static_cast<char>(payload[i] ^ out[start + i % 4]);
    return out;
  }
 private:
  std::string input, partial;
  bool fragmented = false;
};
}  // namespace notes
