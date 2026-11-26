#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace notes {
// Caller serializes producer/consumer. Storage is one checked PSRAM allocation.
class AudioQueue {
 public:
  AudioQueue(uint8_t* memory, size_t capacity) : memory(memory), capacity(capacity) {}
  bool push(const uint8_t* bytes, size_t n) {
    if ((n & 1) || n > capacity - used) return false;
    for (size_t i = 0; i < n; ++i) memory[(first + used + i) % capacity] = bytes[i];
    used += n;
    return true;
  }
  size_t pop(uint8_t* out, size_t maximum) {
    const size_t n = std::min(used, maximum) & ~size_t(1);
    for (size_t i = 0; i < n; ++i) out[i] = memory[(first + i) % capacity];
    first = (first + n) % capacity;
    used -= n;
    return n;
  }
 private:
  uint8_t* memory;
  size_t capacity, first = 0, used = 0;
};
}  // namespace notes
