#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
inline std::string keyFile = "sk-fixture";
inline std::map<std::string, std::string> extraFiles;
inline bool failWrite = false, failSync = false, failClose = false, failRead = false;
inline std::string failRenameFrom, failRemove, failReadPath;
inline bool failRollback = false;
inline std::string& bytesAt(const std::string& path) {
  return path == "/.crosspoint/notes-openai.key" ? keyFile : extraFiles[path];
}
struct HalFile {
  size_t pos = 0;
  std::string path;
  size_t size() { return bytesAt(path).size(); }
  int read(void* p, size_t n) {
    if (failRead || failReadPath == path) return -1;
    auto& bytes = bytesAt(path);
    n = std::min(n, bytes.size() - pos);
    memcpy(p, bytes.data() + pos, n);
    pos += n;
    return n;
  }
  size_t write(const void* p, size_t n) {
    if (failWrite) n = 0;
    bytesAt(path).assign(static_cast<const char*>(p), n);
    return n;
  }
  bool sync() { return !failSync; }
  bool close() { return !failClose; }
};
struct StorageFake {
  bool exists(const char* p) {
    return std::string(p) == "/.crosspoint/notes-openai.key" ? !keyFile.empty() : extraFiles.count(p);
  }
  bool openFileForRead(const char*, const char* p, HalFile& f) {
    f.path = p;
    f.pos = 0;
    return exists(p);
  }
  bool openFileForWrite(const char*, const char* p, HalFile& f) {
    f.path = p;
    f.pos = 0;
    bytesAt(p).clear();
    return true;
  }
  bool ensureDirectoryExists(const char*) { return true; }
  bool remove(const char* p) {
    if (failRemove == p) return false;
    if (std::string(p) == "/.crosspoint/notes-openai.key")
      keyFile.clear();
    else
      extraFiles.erase(p);
    return true;
  }
  bool rename(const char* from, const char* to) {
    if (failRenameFrom == from || (failRollback && std::string(from) == "/.crosspoint/notes-openai.key.bak") ||
        !exists(from) || exists(to))
      return false;
    bytesAt(to) = bytesAt(from);
    return remove(from);
  }
};
inline StorageFake Storage;
