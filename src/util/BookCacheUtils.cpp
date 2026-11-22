#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>
#include <fcntl.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

namespace {

struct PreservedCacheFile {
  const char* name;
  const char* tmpName;
};

constexpr PreservedCacheFile EPUB_USER_STATE_FILES[] = {
    {"progress.bin", "upload_preserve_progress.bin"},
    {"progress.bin.bak", "upload_preserve_progress.bin.bak"},
    {"reader_settings.bin", "upload_preserve_reader_settings.bin"},
    {"dictionary_history.txt", "upload_preserve_dictionary_history.txt"},
    {"reading_stats_off", "upload_preserve_reading_stats_off"},
    {"reading_stats_off.tmp", "upload_preserve_reading_stats_off.tmp"},
};

constexpr PreservedCacheFile PAGE_PROGRESS_FILES[] = {
    {"reading_stats_off", "upload_preserve_reading_stats_off"},
    {"reading_stats_off.tmp", "upload_preserve_reading_stats_off.tmp"},
    {"progress.bin", "upload_preserve_progress.bin"},
};

constexpr PreservedCacheFile CACHE_CLEAR_USER_STATE_FILES[] = {
    {"dictionary_history.txt", "clear_preserve_dictionary_history.txt"},
    {"reading_stats_off", "clear_preserve_reading_stats_off"},
    {"reading_stats_off.tmp", "clear_preserve_reading_stats_off.tmp"},
};

struct ResolvedPreservedCacheFile {
  std::string name;
  std::string tmpName;
};

struct StatsFileCandidate {
  std::string name;
  int version = -1;
};

constexpr size_t MAX_PRESERVED_CACHE_FILES = std::size(EPUB_USER_STATE_FILES) > std::size(PAGE_PROGRESS_FILES)
                                                 ? std::size(EPUB_USER_STATE_FILES)
                                                 : std::size(PAGE_PROGRESS_FILES);
constexpr size_t MAX_STATS_FILES_TO_PRESERVE = 8;
constexpr char STATS_PREFIX[] = "stats";
constexpr char STATS_SUFFIX[] = ".bin";

std::string getBookCachePath(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    return Epub(path, "/.crosspoint").getCachePath();
  }
  if (FsHelpers::hasXtcExtension(path)) {
    return Xtc(path, "/.crosspoint").getCachePath();
  }
  if (FsHelpers::hasTxtExtension(path)) {
    return Txt(path, "/.crosspoint").getCachePath();
  }
  return "";
}

const PreservedCacheFile* preservedFilesForPath(const std::string& path, size_t& count) {
  if (FsHelpers::hasEpubExtension(path)) {
    count = std::size(EPUB_USER_STATE_FILES);
    return EPUB_USER_STATE_FILES;
  }
  if (FsHelpers::hasXtcExtension(path) || FsHelpers::hasTxtExtension(path)) {
    count = std::size(PAGE_PROGRESS_FILES);
    return PAGE_PROGRESS_FILES;
  }
  count = 0;
  return nullptr;
}

bool isStatsFileName(const char* name) {
  if (!name) {
    return false;
  }
  const size_t nameLen = strlen(name);
  constexpr size_t prefixLen = std::size(STATS_PREFIX) - 1;
  constexpr size_t suffixLen = std::size(STATS_SUFFIX) - 1;
  return nameLen >= prefixLen + suffixLen && strncmp(name, STATS_PREFIX, prefixLen) == 0 &&
         strcmp(name + nameLen - suffixLen, STATS_SUFFIX) == 0;
}

int statsFileVersion(const char* name) {
  if (!name || strcmp(name, "stats.bin") == 0) {
    return 0;
  }

  constexpr char VERSION_PREFIX[] = "stats_v";
  constexpr size_t versionPrefixLen = std::size(VERSION_PREFIX) - 1;
  constexpr size_t suffixLen = std::size(STATS_SUFFIX) - 1;
  const size_t nameLen = strlen(name);
  if (nameLen <= versionPrefixLen + suffixLen || strncmp(name, VERSION_PREFIX, versionPrefixLen) != 0 ||
      strcmp(name + nameLen - suffixLen, STATS_SUFFIX) != 0) {
    return -1;
  }

  int version = 0;
  for (size_t i = versionPrefixLen; i < nameLen - suffixLen; ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return -1;
    }
    version = version * 10 + (name[i] - '0');
  }
  return version;
}

void appendFixedPreservedFiles(std::vector<ResolvedPreservedCacheFile>& files, const PreservedCacheFile* fixedFiles,
                               const size_t fixedCount) {
  for (size_t i = 0; i < fixedCount; ++i) {
    files.push_back({fixedFiles[i].name, fixedFiles[i].tmpName});
  }
}

bool appendStatsPreservedFiles(const std::string& cachePath, std::vector<ResolvedPreservedCacheFile>& files,
                               const char* tmpPrefix) {
  if (!tmpPrefix) {
    LOG_ERR("BookCache", "Missing stats preservation temp prefix: %s", cachePath.c_str());
    return false;
  }

  HalFile dir = Storage.open(cachePath.c_str());
  if (!dir) {
    if (Storage.exists(cachePath.c_str())) {
      LOG_ERR("BookCache", "Failed to open cache directory for stats preservation: %s", cachePath.c_str());
      return false;
    }
    return true;
  }
  if (!dir.isDirectory()) {
    dir.close();
    LOG_ERR("BookCache", "Cache path is not a directory during stats preservation: %s", cachePath.c_str());
    return false;
  }

  std::vector<StatsFileCandidate> candidates;
  candidates.reserve(MAX_STATS_FILES_TO_PRESERVE + 1);
  char name[96];
  for (HalFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const bool isDirectory = file.isDirectory();
    const size_t nameLen = file.getName(name, sizeof(name));
    file.close();
    if (isDirectory || nameLen == 0 || !isStatsFileName(name)) {
      continue;
    }
    candidates.push_back({name, statsFileVersion(name)});
  }
  dir.close();

  std::sort(candidates.begin(), candidates.end(), [](const StatsFileCandidate& lhs, const StatsFileCandidate& rhs) {
    if (lhs.version != rhs.version) {
      return lhs.version > rhs.version;
    }
    return lhs.name > rhs.name;
  });

  for (size_t i = 0; i < candidates.size(); ++i) {
    if (i >= MAX_STATS_FILES_TO_PRESERVE) {
      LOG_ERR("BookCache", "Too many durable stats versions; refusing cache clear");
      return false;
    }
    files.push_back({candidates[i].name, std::string(tmpPrefix) + candidates[i].name});
  }
  return true;
}

bool resolvePreservedFiles(const std::string& cachePath, const PreservedCacheFile* fixedFiles, const size_t fixedCount,
                           const bool includeStatsFiles, const char* statsTmpPrefix,
                           std::vector<ResolvedPreservedCacheFile>& files) {
  files.clear();
  files.reserve(fixedCount + (includeStatsFiles ? MAX_STATS_FILES_TO_PRESERVE : 0));
  appendFixedPreservedFiles(files, fixedFiles, fixedCount);
  if (includeStatsFiles && !appendStatsPreservedFiles(cachePath, files, statsTmpPrefix)) {
    return false;
  }
  return true;
}

bool restorePreservedFiles(const std::string& cachePath, const std::vector<ResolvedPreservedCacheFile>& files,
                           const bool* movedFiles = nullptr) {
  if (files.empty()) {
    return true;
  }

  bool restoredAny = false;
  for (size_t i = 0; i < files.size(); i++) {
    if (movedFiles && !movedFiles[i]) {
      continue;
    }
    const std::string tmpPath = cachePath + "." + files[i].tmpName;
    if (!Storage.exists(tmpPath.c_str())) {
      continue;
    }

    if (!Storage.exists(cachePath.c_str()) && !Storage.mkdir(cachePath.c_str())) return false;
    const std::string finalPath = cachePath + "/" + files[i].name;
    // Existing destinations may alias retained sources after a negative FAT ack.
    if (Storage.exists(finalPath.c_str())) return false;
    if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
      LOG_ERR("BookCache", "Failed to restore preserved cache state: %s", finalPath.c_str());
      return false;
    } else {
      restoredAny = true;
    }
  }

  if (restoredAny) {
    LOG_DBG("BookCache", "Restored preserved user cache state: %s", cachePath.c_str());
  }
  return true;
}

bool preserveUserStateFiles(const std::string& cachePath, const std::vector<ResolvedPreservedCacheFile>& files,
                            bool* movedFiles) {
  if (std::any_of(files.begin(), files.end(),
                  [&cachePath](const auto& file) { return Storage.exists((cachePath + "." + file.tmpName).c_str()); }))
    return false;
  for (size_t i = 0; i < files.size(); i++) {
    if (movedFiles) {
      movedFiles[i] = false;
    }
    const std::string sourcePath = cachePath + "/" + files[i].name;
    const std::string tmpPath = cachePath + "." + files[i].tmpName;

    if (Storage.exists(tmpPath.c_str())) return false;
    if (!Storage.exists(sourcePath.c_str())) {
      continue;
    }
    if (!Storage.rename(sourcePath.c_str(), tmpPath.c_str())) {
      LOG_ERR("BookCache", "Failed to preserve cache state: %s", sourcePath.c_str());
      return false;
    } else if (movedFiles) {
      movedFiles[i] = true;
    }
  }
  return true;
}

bool clearBookCacheForPath(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    return Epub(path, "/.crosspoint").clearCache();
  }
  if (FsHelpers::hasXtcExtension(path)) {
    return Xtc(path, "/.crosspoint").clearCache();
  }
  if (FsHelpers::hasTxtExtension(path)) {
    return Txt(path, "/.crosspoint").clearCache();
  }
  return false;
}

// Durable encompassing latch: a failed FAT acknowledgement is not rollback evidence.
bool hasCacheRecoveryRoots(const std::string& cachePath, const std::vector<ResolvedPreservedCacheFile>& files) {
  if (Storage.exists((cachePath + ".cache-clear.pending").c_str())) return true;
  return std::any_of(files.begin(), files.end(), [&cachePath](const auto& file) {
    return Storage.exists((cachePath + "." + file.tmpName).c_str());
  });
}

bool beginCacheClear(const std::string& cachePath, const std::vector<ResolvedPreservedCacheFile>& files) {
  if (hasCacheRecoveryRoots(cachePath, files)) return false;
  const auto marker = cachePath + ".cache-clear.pending";
  constexpr char intent[] = "CPCACHECLEAR1\n";
  HalFile file = Storage.open(marker.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  if (!file || file.write(reinterpret_cast<const uint8_t*>(intent), sizeof(intent) - 1) != sizeof(intent) - 1 ||
      !file.sync() || !file.close())
    return false;
  auto verify = Storage.open(marker.c_str());
  char bytes[sizeof(intent)] = {};
  return verify && verify.size() == sizeof(intent) - 1 &&
         verify.read(bytes, sizeof(intent) - 1) == static_cast<int>(sizeof(intent) - 1) && verify.close() &&
         memcmp(bytes, intent, sizeof(intent) - 1) == 0;
}

bool clearCacheDirectoryPreservingFiles(const std::string& cachePath, const PreservedCacheFile* fixedPreservedFiles,
                                        const size_t fixedPreservedCount, const bool includeStatsFiles,
                                        const char* statsTmpPrefix) {
  if (cachePath.empty() || Storage.exists("/.crosspoint/book-move.pending") ||
      Storage.exists("/.crosspoint/clippings/move.pending"))
    return false;

  std::vector<ResolvedPreservedCacheFile> preservedFiles;
  if (!resolvePreservedFiles(cachePath, fixedPreservedFiles, fixedPreservedCount, includeStatsFiles, statsTmpPrefix,
                             preservedFiles)) {
    return false;
  }

  if (!Storage.exists(cachePath.c_str())) return !hasCacheRecoveryRoots(cachePath, preservedFiles);
  if (!beginCacheClear(cachePath, preservedFiles)) return false;
  bool movedFiles[MAX_PRESERVED_CACHE_FILES + MAX_STATS_FILES_TO_PRESERVE] = {};
  const bool preserveOk = preserveUserStateFiles(cachePath, preservedFiles, movedFiles);
  if (!preserveOk) {
    // Negative rename acknowledgements retain all candidates; no speculative rollback.
    LOG_ERR("BookCache", "Aborted cache clear because preserved state could not be moved: %s", cachePath.c_str());
    return false;
  }

  const bool clearOk = Storage.removeDir(cachePath.c_str());
  if (!clearOk) return false;  // Retain roots after uncertain directory deletion.
  const bool restoreOk = restorePreservedFiles(cachePath, preservedFiles, movedFiles);
  return restoreOk && Storage.remove((cachePath + ".cache-clear.pending").c_str());
}

}  // namespace

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  constexpr char EPUB_PREFIX[] = "epub_";
  constexpr char TXT_PREFIX[] = "txt_";
  constexpr char XTC_PREFIX[] = "xtc_";

  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0 ||
         strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0 ||
         strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0;
}

void clearBookCache(const std::string& path) { clearBookCachePreservingUserState(path); }

bool clearBookCachePreservingUserState(const std::string& path) {
  if (Storage.exists("/.crosspoint/book-move.pending") || Storage.exists("/.crosspoint/clippings/move.pending"))
    return false;
  size_t preservedCount = 0;
  const PreservedCacheFile* preservedFiles = preservedFilesForPath(path, preservedCount);
  if (!preservedFiles || preservedCount == 0) {
    return clearBookCacheForPath(path);
  }

  const std::string cachePath = getBookCachePath(path);
  if (cachePath.empty()) {
    return false;
  }

  std::vector<ResolvedPreservedCacheFile> resolvedFiles;
  const bool includeStatsFiles = FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path);
  if (!resolvePreservedFiles(cachePath, preservedFiles, preservedCount, includeStatsFiles, "upload_preserve_",
                             resolvedFiles)) {
    return false;
  }

  if (!Storage.exists(cachePath.c_str())) return !hasCacheRecoveryRoots(cachePath, resolvedFiles);
  if (!beginCacheClear(cachePath, resolvedFiles)) return false;
  bool movedFiles[MAX_PRESERVED_CACHE_FILES + MAX_STATS_FILES_TO_PRESERVE] = {};
  const bool preserveOk = preserveUserStateFiles(cachePath, resolvedFiles, movedFiles);
  if (!preserveOk) {
    // Negative rename acknowledgements retain all candidates; no speculative rollback.
    LOG_ERR("BookCache", "Aborted cache clear because user state could not be preserved: %s", cachePath.c_str());
    return false;
  }
  const bool clearOk = clearBookCacheForPath(path);
  if (!clearOk) return false;  // Retain roots after uncertain cache deletion.
  const bool restoreOk = restorePreservedFiles(cachePath, resolvedFiles, movedFiles);
  return restoreOk && Storage.remove((cachePath + ".cache-clear.pending").c_str());
}

bool clearBookCacheDirectoryPreservingStats(const std::string& cachePath) {
  return clearCacheDirectoryPreservingFiles(cachePath, CACHE_CLEAR_USER_STATE_FILES,
                                            std::size(CACHE_CLEAR_USER_STATE_FILES), true, "clear_preserve_");
}
