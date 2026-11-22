#include "RecentReadingStats.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Xtc.h>

#include "BookReadingStats.h"
#include "RecentBooksStore.h"

RecentReadingStats RecentReadingStats::load(const std::string& activeCachePath, const BookReadingStats* activeStats) {
  RecentReadingStats out;
  for (const auto& recent : RECENT_BOOKS.readOnlySnapshot()) {
    if (recent.path.empty() || !Storage.exists(recent.path.c_str())) continue;
    bool duplicate = false;
    for (size_t i = 0; i < out.count; ++i) duplicate |= out.books[i].path == recent.path;
    if (duplicate) continue;
    auto& book = out.books[out.count++];
    book.path = recent.path;
    book.title = recent.title.empty() ? recent.path.substr(recent.path.find_last_of('/') + 1) : recent.title;
    book.coverBmpPath = recent.coverBmpPath;
    // Markdown follows the EPUB reader. TXT has no BookReadingStats producer;
    // an absent record is honestly zero, not inferred from its separate index cache.
    const std::string cache = FsHelpers::hasXtcExtension(book.path)
                                  ? Xtc(book.path, "/.crosspoint").getCachePath()
                                  : Epub::cachePathForFilePath(book.path, "/.crosspoint");
    // Home can cache only an adaptive thumbnail, while recent.json retains the
    // size-template path. Reuse a bounded existing thumbnail from THIS book's
    // cache, rather than generating artwork or showing a cover from another book.
    if (book.coverBmpPath.find("[WIDTH]") != std::string::npos ||
        book.coverBmpPath.find("[HEIGHT]") != std::string::npos) {
      for (const auto& file : Storage.listFiles(cache.c_str(), 48)) {
        const std::string path = file.c_str();
        const std::string name = path.substr(path.find_last_of('/') + 1);
        if (name.rfind("thumb_", 0) == 0 && name.size() > 4 && name.substr(name.size() - 4) == ".bmp") {
          book.coverBmpPath = cache + "/" + name;
          break;
        }
      }
    }
    book.totalReadingSeconds = BookReadingStats::load(cache, BookReadingStats::LoadMode::ReadOnly).totalReadingSeconds;
    // The reader already supplies its lifetime preview, including accepted live time.
    // Replace, never add/max: a reset preview can legitimately be lower than disk.
    if (activeStats && !activeCachePath.empty() && cache == activeCachePath) {
      book.totalReadingSeconds = activeStats->totalReadingSeconds;
    }
    if (out.count == out.books.size()) break;
  }
  return out;
}
