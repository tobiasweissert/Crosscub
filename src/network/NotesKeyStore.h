#pragma once
#include <HalStorage.h>

#include <cstring>
#include <mutex>

#include "NotesRealtimeWire.h"

// Plaintext on the owner's SD card, deliberately outside app-readable paths.
// No persisted-secret getter is registered with Lua.
namespace notes::keys {
inline constexpr const char* PATH = "/.crosspoint/notes-openai.key";
inline constexpr const char* TEMP = "/.crosspoint/notes-openai.key.tmp";
inline constexpr const char* BACKUP = "/.crosspoint/notes-openai.key.bak";
inline std::mutex mutex;
struct Buffer {
  char bytes[515]{};
  size_t size = 0;
  ~Buffer() {
    volatile char* p = bytes;
    for (size_t i = 0; i < sizeof(bytes); ++i) p[i] = 0;
  }
};
inline bool valid(const char* value, size_t size) { return notes::validKey(value, size); }
inline bool readPath(const char* path, Buffer& out) {
  HalFile file;
  if (!Storage.openFileForRead("NSTT", path, file)) return false;
  const size_t n = file.size();
  const bool ok = n > 0 && n <= 514 && file.read(out.bytes, n) == static_cast<int>(n);
  const bool closed = file.close();
  if (!ok || !closed) return false;
  out.size = n;
  while (out.size && (out.bytes[out.size - 1] == '\r' || out.bytes[out.size - 1] == '\n')) --out.size;
  return valid(out.bytes, out.size);
}
inline bool read(Buffer& out) {
  std::lock_guard<std::mutex> lock(mutex);
  // A failed publication/rollback retains the previous key in BACKUP.
  return readPath(PATH, out) || readPath(BACKUP, out);
}
inline bool configured() {
  Buffer value;
  return read(value);
}
inline bool save(const char* value, size_t size) {
  if (!valid(value, size)) return false;
  std::lock_guard<std::mutex> lock(mutex);
  if (!Storage.ensureDirectoryExists("/.crosspoint")) return false;
  // Recover before beginning a new transaction; never discard the only old copy.
  if (!Storage.exists(PATH) && Storage.exists(BACKUP) && !Storage.rename(BACKUP, PATH)) return false;
  if (Storage.exists(TEMP) && !Storage.remove(TEMP)) return false;
  HalFile file;
  if (!Storage.openFileForWrite("NSTT", TEMP, file)) return false;
  bool ok = file.write(value, size) == size;
  ok = file.sync() && ok;
  ok = file.close() && ok;
  Buffer check;
  ok = ok && readPath(TEMP, check) && check.size == size && memcmp(check.bytes, value, size) == 0;
  if (!ok) {
    Storage.remove(TEMP);
    return false;
  }
  if (Storage.exists(BACKUP) && !Storage.remove(BACKUP)) {
    Storage.remove(TEMP);
    return false;
  }
  const bool previous = Storage.exists(PATH);
  if (previous && !Storage.rename(PATH, BACKUP)) {
    Storage.remove(TEMP);
    return false;
  }
  if (!Storage.rename(TEMP, PATH)) {
    if (previous) Storage.rename(BACKUP, PATH);
    Storage.remove(TEMP);
    return false;
  }
  Buffer published;
  if (!readPath(PATH, published) || published.size != size || memcmp(published.bytes, value, size) != 0) {
    if (Storage.remove(PATH) && previous) Storage.rename(BACKUP, PATH);
    return false;
  }
  // Publication succeeded. A leftover backup is recovered/removed next time.
  if (previous) Storage.remove(BACKUP);
  return true;
}
inline bool clear() {
  std::lock_guard<std::mutex> lock(mutex);
  // Remove recovery copies first so a successful clear cannot resurrect a key.
  for (const char* path : {TEMP, BACKUP, PATH})
    if (Storage.exists(path) && !Storage.remove(path)) return false;
  return true;
}
}  // namespace notes::keys
