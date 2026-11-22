#pragma once

#include <algorithm>

#include "ReadingStatsUtils.h"

// Read-only view of the existing Boolean history. Never implies daily duration.
struct ReadingHeatmap {
  enum class Cell : uint8_t { Outside, Unknown, Inactive, Active };
  bool valid = false;
  uint32_t firstDay = 0, lastDay = 0;
  int32_t gridStart = 0;
  int columns = 0, todayColumn = 0, todayRow = 0;
  unsigned activeDays = 0, knownDays = 0;
  std::array<Cell, 14 * 7> cells{};

  Cell cell(int column, int row) const {
    return column >= 0 && column < columns && row >= 0 && row < 7 ? cells[column * 7 + row] : Cell::Outside;
  }

  static ReadingHeatmap build(const ReadingStatsDate& today, uint32_t anchor,
                              const std::array<uint8_t, READING_HISTORY_BYTES>& bits) {
    ReadingHeatmap out;
    if (!today.isValid()) return out;
    out.valid = true;
    out.lastDay = readingStatsDayIndex(today);
    // Fourteen Monday-first calendar columns, including the current partial week.
    out.todayRow = readingStatsDayOfWeekIndex(today);
    out.gridStart = static_cast<int32_t>(out.lastDay) - out.todayRow - 13 * 7;
    out.firstDay = static_cast<uint32_t>(std::max<int32_t>(0, out.gridStart));
    out.columns = 14;
    out.todayColumn = 13;
    ReadingStatsDate anchorDate;
    const bool hasHistory = (anchor != 0 || (bits[0] & 1u)) && readingStatsDateFromDayIndex(anchor, anchorDate);
    for (int col = 0; col < out.columns; ++col) {
      for (int row = 0; row < 7; ++row) {
        const int32_t day = out.gridStart + col * 7 + row;
        if (day < static_cast<int32_t>(out.firstDay) || day > static_cast<int32_t>(out.lastDay)) continue;
        auto& state = out.cells[col * 7 + row];
        state = Cell::Unknown;
        if (!hasHistory ||
            (anchor >= READING_HISTORY_DAYS && day <= static_cast<int32_t>(anchor - READING_HISTORY_DAYS)))
          continue;
        // Days after the latest recorded reading are empty, never future days.
        const uint32_t age = day <= static_cast<int32_t>(anchor) ? anchor - day : READING_HISTORY_DAYS;
        const bool active = age < READING_HISTORY_DAYS && (bits[age / 8] & (1u << (age % 8)));
        state = active ? Cell::Active : Cell::Inactive;
        ++out.knownDays;
        if (active) ++out.activeDays;
      }
    }
    return out;
  }
};
