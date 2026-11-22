#include <HalStorage.h>

#include <cassert>

#include "TtfRenderProfileStore.h"
int main(int argc, char** argv) {
  assert(argc == 2);
  testRoot = argv[1];
  TtfRenderProfileStore a;
  TtfRenderProfile p;
  p.twoBit = false;
  p.cacheKiB = 64;
  p.maxGlyphs = 512;
  assert(a.setProfile("Bitter", p));
  auto key = p.identity();
  assert(a.saveToFile());
  TtfRenderProfileStore b;
  assert(b.loadFromFile());
  assert(b.profileFor("Bitter") == p);
  assert(b.profileFor("Other") != p);
  p.cacheKiB = 0;
  assert(!b.setProfile("Bitter", p));
  assert(b.profileFor("Bitter").identity() == key);
  assert(b.resetProfile("Bitter"));
  assert(b.saveToFile());
  TtfRenderProfileStore c;
  assert(c.loadFromFile());
  assert(c.profileFor("Bitter") == TtfRenderProfile{});
  assert(!c.setProfile("../Bad", {}));
  assert(c.profileFor(nullptr) == TtfRenderProfile{});
  const char* invalidFamilies[] = {nullptr, "", "../Bad", "bad/name", "bad\\name", "bad\x1f"};
  for (const auto* invalid : invalidFamilies) {
    assert(!c.setProfile(invalid, {}));
    assert(!c.resetProfile(invalid));
  }
  assert(!c.setProfile(std::string(128, 'x').c_str(), {}));
  p.cacheKiB = 64;
  assert(c.setProfile("Bitter", p));
  p.maxGlyphs = 256;
  assert(c.setProfile("Bitter", p));  // Update the first matching family, not a duplicate.
  assert(c.profileFor("Bitter") == p);
  assert(c.saveToFile());
  const auto canonical = testRoot + TtfRenderProfileStore::getFilePath();
  const auto backup = testRoot + "/.crosspoint/crosscub-ttf-profiles.bak";
  const auto temp = testRoot + "/.crosspoint/crosscub-ttf-profiles.tmp";
  std::filesystem::copy_file(canonical, backup);
  {
    auto f = std::fopen(canonical.c_str(), "wb");
    assert(f);
    std::fputs("bad", f);
    std::fclose(f);
  }
  assert(!c.loadFromFile());  // A present invalid canonical must not fall through to backup.
  assert(c.profileFor("Bitter") == p);
  assert(!c.saveToFile());  // Recovery roots must survive an attempted publication.
  assert(std::filesystem::exists(backup));
  std::filesystem::remove(canonical);
  assert(c.loadFromFile());
  assert(c.profileFor("Bitter") == p);
  std::filesystem::remove(backup);
  {
    auto f = std::fopen(temp.c_str(), "wb");
    assert(f);
    std::fclose(f);
  }
  assert(!c.loadFromFile());
  assert(!c.saveToFile());
  assert(c.profileFor("Bitter") == p);
  std::filesystem::remove(temp);

  // A checksum-valid duplicate-family file must be rejected without replacing live entries.
  std::vector<uint8_t> bytes = {'C', 'T', 'P', '1', 2};
  for (int i = 0; i < 2; ++i) {
    bytes.push_back(6);
    for (char ch : std::string("Bitter")) bytes.push_back(ch);
    for (uint8_t value : {1, 64, 0, 0, 1}) bytes.push_back(value);
  }
  uint32_t digest = 2166136261u;
  for (auto byte : bytes) digest = (digest ^ byte) * 16777619u;
  for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(digest >> shift);
  {
    auto f = std::fopen(canonical.c_str(), "wb");
    assert(f);
    assert(std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size());
    std::fclose(f);
  }
  assert(!c.loadFromFile());
  assert(c.profileFor("Bitter") == p);
}
