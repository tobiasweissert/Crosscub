#include "TtfRenderProfileStore.h"

#include <HalStorage.h>

#include <algorithm>
#include <cctype>
#include <cstring>
namespace {
constexpr const char* Temp = "/.crosspoint/crosscub-ttf-profiles.tmp";
constexpr const char* Backup = "/.crosspoint/crosscub-ttf-profiles.bak";
bool safe(const char* name) {
  if (!name || !*name || strlen(name) > 127 || strstr(name, "..")) return false;
  for (const auto* p = name; *p; ++p)
    if (static_cast<unsigned char>(*p) < 32 || *p == '/' || *p == '\\') return false;
  return true;
}
uint32_t hash(uint32_t h, const void* bytes, size_t size) {
  const auto* p = static_cast<const uint8_t*>(bytes);
  for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 16777619u;
  return h;
}
void pack(const TtfRenderProfile& p, uint8_t out[5]) {
  out[0] = p.twoBit;
  out[1] = p.cacheKiB;
  out[2] = p.cacheKiB >> 8;
  out[3] = p.maxGlyphs;
  out[4] = p.maxGlyphs >> 8;
}
}  // namespace
uint32_t TtfRenderProfile::identity() const {
  uint8_t data[5];
  pack(*this, data);
  return hash(2166136261u, data, 5);
}
TtfRenderProfileStore& TtfRenderProfileStore::getInstance() {
  static TtfRenderProfileStore store;
  return store;
}
TtfRenderProfile TtfRenderProfileStore::profileFor(const char* family) const {
  if (family) {
    const auto it =
        std::find_if(entries_.begin(), entries_.end(), [family](const auto& e) { return e.family == family; });
    if (it != entries_.end()) return it->profile;
  }
  return {};
}
bool TtfRenderProfileStore::setProfile(const char* family, const TtfRenderProfile& profile) {
  if (!safe(family) || !profile.valid()) return false;
  const auto it =
      std::find_if(entries_.begin(), entries_.end(), [family](const auto& e) { return e.family == family; });
  if (it != entries_.end()) {
    it->profile = profile;
    return true;
  }
  if (entries_.size() >= MAX_PROFILES) return false;
  entries_.push_back({family, profile});
  return true;
}
bool TtfRenderProfileStore::resetProfile(const char* family) {
  if (!safe(family)) return false;
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(), [family](const auto& e) { return e.family == family; }),
      entries_.end());
  return true;
}
bool TtfRenderProfileStore::decode(const char* path, std::vector<Entry>& out) {
  HalFile f = Storage.open(path);
  uint8_t header[5];
  if (!f || f.size() > 4096 || f.read(header, 5) != 5 || memcmp(header, "CTP1", 4) || header[4] > MAX_PROFILES)
    return false;
  uint32_t digest = hash(2166136261u, header, 5);
  size_t bytes = 5;
  for (unsigned i = 0; i < header[4]; ++i) {
    uint8_t n = 0, data[5];
    char family[128] = {};
    if (f.read(&n, 1) != 1 || !n || n > 127 || f.read(family, n) != n || !safe(family) || strlen(family) != n ||
        f.read(data, 5) != 5)
      return false;
    TtfRenderProfile p{data[0] != 0, static_cast<uint16_t>(data[1] | data[2] << 8),
                       static_cast<uint16_t>(data[3] | data[4] << 8)};
    if (data[0] > 1 || !p.valid()) return false;
    if (std::any_of(out.begin(), out.end(), [&family](const auto& e) { return e.family == family; })) return false;
    digest = hash(digest, &n, 1);
    digest = hash(digest, family, n);
    digest = hash(digest, data, 5);
    bytes += n + 6;
    out.push_back({family, p});
  }
  uint8_t stored[4];
  if (f.read(stored, 4) != 4 || f.size() != bytes + 4) return false;
  const uint32_t expected =
      uint32_t(stored[0]) | uint32_t(stored[1]) << 8 | uint32_t(stored[2]) << 16 | uint32_t(stored[3]) << 24;
  return digest == expected && f.close();
}
bool TtfRenderProfileStore::loadFromFile() {
  std::vector<Entry> loaded;
  const char* path = Storage.exists(getFilePath()) ? getFilePath() : Storage.exists(Backup) ? Backup : nullptr;
  if (!path) return !Storage.exists(Temp);  // Incomplete publication is not permission to reset preferences.
  if (!decode(path, loaded)) return false;
  entries_ = std::move(loaded);
  return true;
}
bool TtfRenderProfileStore::saveToFile() const {
  // Uncertain prior writes must be recovered explicitly; never overwrite recovery roots.
  if (Storage.exists(Temp) || Storage.exists(Backup)) return false;
  if (!Storage.mkdir("/.crosspoint", true) && !Storage.exists("/.crosspoint")) return false;
  HalFile f;
  if (!Storage.openFileForWrite("TTFPROFILE", Temp, f)) return false;
  const uint8_t header[5] = {'C', 'T', 'P', '1', static_cast<uint8_t>(entries_.size())};
  uint32_t digest = hash(2166136261u, header, 5);
  bool ok = f.write(header, 5) == 5;
  for (const auto& e : entries_) {
    uint8_t n = e.family.size(), data[5];
    pack(e.profile, data);
    digest = hash(digest, &n, 1);
    digest = hash(digest, e.family.data(), n);
    digest = hash(digest, data, 5);
    ok = ok && f.write(&n, 1) == 1 && f.write(e.family.data(), n) == n && f.write(data, 5) == 5;
  }
  const uint8_t stored[4] = {static_cast<uint8_t>(digest), static_cast<uint8_t>(digest >> 8),
                             static_cast<uint8_t>(digest >> 16), static_cast<uint8_t>(digest >> 24)};
  ok = ok && f.write(stored, 4) == 4;
  ok = f.sync() && ok;
  ok = f.close() && ok;
  std::vector<Entry> check;
  if (!ok || !decode(Temp, check)) return false;
  if (Storage.exists(getFilePath()) && !Storage.rename(getFilePath(), Backup)) return false;
  if (!Storage.rename(Temp, getFilePath())) return false;
  check.clear();
  if (!decode(getFilePath(), check) || check.size() != entries_.size()) return false;
  for (size_t i = 0; i < check.size(); ++i)
    if (check[i].family != entries_[i].family || check[i].profile != entries_[i].profile) return false;
  return !Storage.exists(Backup) || Storage.remove(Backup);
}
