#pragma once
#include <cstring>
#include <string>
inline std::string keyFile = "sk-fixture";
struct HalFile {
  size_t pos = 0;
  size_t size() { return keyFile.size(); }
  int read(unsigned char* p, size_t n) {
    n = std::min(n, keyFile.size() - pos);
    memcpy(p, keyFile.data() + pos, n);
    pos += n;
    return n;
  }
  bool close() { return true; }
};
struct StorageFake {
  bool openFileForRead(const char*, const char* path, HalFile&) {
    return std::string(path) == "/.crosspoint/notes-openai.key" && !keyFile.empty();
  }
};
inline StorageFake Storage;
