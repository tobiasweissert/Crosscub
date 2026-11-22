#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Options actually supported by the selected TtfEpdFont runtime. These are NOT
// Ink's HalScalableFont hinting/weight/interpreter options: unsupported settings
// are deliberately not accepted or persisted as if they affected rasterization.
struct TtfRenderProfile {
  bool twoBit = true;
  uint16_t cacheKiB = 1024;  // PSRAM reader cache; non-PSRAM fallback stays capped at 32 KiB.
  uint16_t maxGlyphs = 4096;
  bool operator==(const TtfRenderProfile&) const = default;
  bool valid() const { return cacheKiB >= 16 && cacheKiB <= 1024 && maxGlyphs >= 128 && maxGlyphs <= 4096; }
  uint32_t identity() const;
};
class TtfRenderProfileStore {
 public:
  static TtfRenderProfileStore& getInstance();
  static constexpr const char* getFilePath() { return "/.crosspoint/crosscub-ttf-profiles.bin"; }
  bool loadFromFile();
  bool saveToFile() const;
  TtfRenderProfile profileFor(const char* family) const;
  bool setProfile(const char* family, const TtfRenderProfile& profile);
  bool resetProfile(const char* family);

 private:
  struct Entry {
    std::string family;
    TtfRenderProfile profile;
  };
  static constexpr size_t MAX_PROFILES = 24;
  std::vector<Entry> entries_;
  static bool decode(const char* path, std::vector<Entry>& out);
};
#define TTF_RENDER_PROFILES TtfRenderProfileStore::getInstance()
