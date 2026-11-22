#pragma once

#include <array>
#include <cstdint>
#include <string>

struct BookReadingStats;

// A bounded read-only snapshot; recency comes from RecentBooksStore, not dates or durations.
struct RecentReadingStats {
  struct Book {
    std::string path;
    std::string title;
    std::string coverBmpPath;
    uint32_t totalReadingSeconds = 0;
  };
  std::array<Book, 3> books{};
  size_t count = 0;

  static RecentReadingStats load(const std::string& activeCachePath = "",
                                 const BookReadingStats* activeStats = nullptr);
};
