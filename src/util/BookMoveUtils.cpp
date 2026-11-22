#include "BookMoveUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>
#include <fcntl.h>

#include <algorithm>
#include <cstring>

#include "BookmarkStore.h"
#include "ClippingStore.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "activities/home/FileActionPaths.h"

namespace {
constexpr char READ_FOLDER[] = "/Read";
// Separate from clipping's CPMOVE1: public clipping preparation must not bypass
// its own retained marker. This journal owns the encompassing metadata move.
constexpr char BOOK_MOVE_PENDING[] = "/.crosspoint/book-move.pending";

bool writeMoveIntent(const std::string& from, const std::string& to) {
  const auto validPath = [](const std::string& path) {
    return std::none_of(path.begin(), path.end(), [](const unsigned char c) { return c < 0x20 || c == 0x7f; });
  };
  if (!validPath(from) || !validPath(to)) return false;
  const std::string intent = "CPBOOKMOVE1\n" + from + "\n" + to + "\n";
  HalFile marker = Storage.open(BOOK_MOVE_PENDING, O_WRONLY | O_CREAT | O_EXCL);
  if (!marker || marker.write(reinterpret_cast<const uint8_t*>(intent.data()), intent.size()) != intent.size() ||
      !marker.sync() || !marker.close())
    return false;
  HalFile verify;
  if (!Storage.openFileForRead("BookMove", BOOK_MOVE_PENDING, verify) || verify.size() != intent.size()) return false;
  std::string bytes(intent.size(), '\0');
  return verify.read(&bytes[0], bytes.size()) == static_cast<int>(bytes.size()) && verify.close() && bytes == intent;
}

bool getCachePath(const std::string& bookPath, const char* bookType, std::string& cachePath) {
  if (strcmp(bookType, "epub") == 0) {
    cachePath = Epub::cachePathForFilePath(bookPath, "/.crosspoint");
  } else if (strcmp(bookType, "xtc") == 0) {
    cachePath = Xtc(bookPath, "/.crosspoint").getCachePath();
  } else if (strcmp(bookType, "txt") == 0) {
    cachePath = Txt(bookPath, "/.crosspoint").getCachePath();
  } else {
    LOG_ERR("BookMove", "Unknown book type for state migration: %s", bookType);
    return false;
  }
  return true;
}
}  // namespace

namespace BookMoveUtils {

RenameMigrationResult moveFile(const std::string& oldPath, const std::string& newPath) {
  if (Storage.exists(BOOK_MOVE_PENDING) || Storage.exists("/.crosspoint/clippings/move.pending"))
    return RenameMigrationResult::Uncertain;
  if (!FileActionPaths::isUserPath(oldPath) || !FileActionPaths::isUserPath(newPath) || oldPath == newPath ||
      Storage.exists(newPath.c_str()))
    return RenameMigrationResult::RolledBack;
  auto file = Storage.open(oldPath.c_str());
  if (!file || file.isDirectory()) return RenameMigrationResult::RolledBack;
  if (!file.close()) return RenameMigrationResult::Uncertain;
  const char* type = nullptr;
  std::string cache;
  if (FsHelpers::hasEpubExtension(oldPath)) {
    if (!FsHelpers::hasEpubExtension(newPath)) return RenameMigrationResult::RolledBack;
    type = "epub";
    cache = Epub::cachePathForFilePath(oldPath, "/.crosspoint");
  } else if (FsHelpers::hasXtcExtension(oldPath)) {
    if (!FsHelpers::hasXtcExtension(newPath)) return RenameMigrationResult::RolledBack;
    type = "xtc";
    cache = Xtc(oldPath, "/.crosspoint").getCachePath();
  } else if (FsHelpers::hasTxtExtension(oldPath) || FsHelpers::hasMarkdownExtension(oldPath)) {
    if (!FsHelpers::hasTxtExtension(newPath) && !FsHelpers::hasMarkdownExtension(newPath))
      return RenameMigrationResult::RolledBack;
    const auto modern = Epub::cachePathForFilePath(oldPath, "/.crosspoint");
    const auto legacy = Txt(oldPath, "/.crosspoint").getCachePath();
    if (Storage.exists(Epub::cachePathForFilePath(newPath, "/.crosspoint").c_str()) ||
        Storage.exists(Txt(newPath, "/.crosspoint").getCachePath().c_str()))
      return RenameMigrationResult::RolledBack;
    if (Storage.exists(modern.c_str())) {
      if (modern != legacy && Storage.exists(legacy.c_str())) return RenameMigrationResult::RolledBack;
      cache = modern;
      type = "epub";
    } else {
      cache = legacy;
      type = "txt";
    }
  }
  if (type) {
    std::string title = oldPath.substr(oldPath.rfind('/') + 1), author;
    for (const auto& book : RECENT_BOOKS.getBooks()) {
      if (book.path != oldPath) continue;
      title = book.title;
      author = book.author;
      break;
    }
    return migrateRenamedBookState(oldPath, newPath, cache, title, author, type, true);
  }
  if (!writeMoveIntent(oldPath, newPath)) return RenameMigrationResult::Uncertain;
  if (!Storage.rename(oldPath.c_str(), newPath.c_str())) return RenameMigrationResult::Uncertain;
  bool changed = false;
  if (APP_STATE.favoriteSleepImagePath == oldPath) {
    APP_STATE.favoriteSleepImagePath = newPath;
    changed = true;
  }
  if (APP_STATE.favoriteBootImagePath == oldPath) {
    APP_STATE.favoriteBootImagePath = newPath;
    changed = true;
  }
  if (changed && !APP_STATE.saveToFile()) return RenameMigrationResult::Uncertain;
  if (!Storage.remove(BOOK_MOVE_PENDING)) return RenameMigrationResult::Uncertain;
  return RenameMigrationResult::Success;
}

std::string buildReadFolderDestination(const std::string& srcPath) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  Storage.mkdir(READ_FOLDER);
  std::string dstPath = std::string(READ_FOLDER) + "/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = std::string(READ_FOLDER) + "/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  return dstPath;
}

RenameMigrationResult migrateRenamedBookState(const std::string& oldPath, const std::string& newPath,
                                              const std::string& oldCachePath, const std::string& title,
                                              const std::string& author, const char* bookType,
                                              const bool preserveOnRenameFailure) {
  if (Storage.exists(BOOK_MOVE_PENDING) || Storage.exists("/.crosspoint/clippings/move.pending")) {
    return RenameMigrationResult::Uncertain;
  }
  if (!bookType || !FileActionPaths::isUserPath(oldPath) || !FileActionPaths::isUserPath(newPath) ||
      oldPath == newPath || Storage.exists(newPath.c_str())) {
    LOG_ERR("BookMove", "Invalid or colliding book migration");
    return RenameMigrationResult::RolledBack;
  }

  std::string newCachePath;
  if (!getCachePath(newPath, bookType, newCachePath)) return RenameMigrationResult::RolledBack;
  if (preserveOnRenameFailure && Storage.exists(newCachePath.c_str())) {
    return RenameMigrationResult::RolledBack;
  }

  bool cacheMoved = false;
  bool bookRenamed = false;
  bool recentMoved = false;
  bool openPathMoved = false;
  const bool shouldMoveOpenPath = APP_STATE.openEpubPath == oldPath;
  bool metadataCommitted = true;
  BookmarkStore::RenameMigration bookmarkMigration;
  ClippingStore::RenameMigration clippingMigration;

  const auto commitMetadata = [&]() {
    if (!BookmarkStore::commitRenameMigration(bookmarkMigration)) {
      metadataCommitted = false;
      LOG_ERR("BookMove", "Renamed book kept stale bookmark rollback files: %s", newPath.c_str());
      if (preserveOnRenameFailure) return;
    }
    if (!ClippingStore::commitRenameMigration(clippingMigration)) {
      metadataCommitted = false;
      LOG_ERR("BookMove", "Renamed book kept stale clipping rollback files: %s", newPath.c_str());
    }
  };

  const auto rollbackMovedReferences = [&]() {
    bool rolledBack = true;
    if (openPathMoved) {
      APP_STATE.openEpubPath = oldPath;
      if (!APP_STATE.saveToFile()) {
        LOG_ERR("BookMove", "Failed to restore open book path after rename: %s", oldPath.c_str());
        rolledBack = false;
      } else {
        openPathMoved = false;
      }
    }
    if (recentMoved) {
      if (!RECENT_BOOKS.updatePath(newPath, oldPath, newCachePath, oldCachePath)) {
        rolledBack = false;
      } else {
        recentMoved = false;
      }
    }
    return rolledBack;
  };

  const auto rollbackPreparedStorage = [&]() {
    bool rolledBack = true;
    if (!ClippingStore::rollbackRenameMigration(clippingMigration)) {
      LOG_ERR("BookMove", "Failed to restore clipping metadata after rename: %s", newPath.c_str());
      rolledBack = false;
    }
    if (!BookmarkStore::rollbackRenameMigration(bookmarkMigration)) {
      LOG_ERR("BookMove", "Failed to restore bookmark metadata after rename: %s", newPath.c_str());
      rolledBack = false;
    }
    if (cacheMoved && Storage.exists(newCachePath.c_str()) &&
        !Storage.rename(newCachePath.c_str(), oldCachePath.c_str())) {
      LOG_ERR("BookMove", "Failed to roll back cache migration %s -> %s", newCachePath.c_str(), oldCachePath.c_str());
      rolledBack = false;
    }
    return rolledBack;
  };

  const auto keepRenamed = [&]() {
    LOG_ERR("BookMove", "Could not restore the original filename; preserving state at the new path");
    // Both calls are idempotent when that reference is already at newPath,
    // and repair a partially completed rollback when it is still at oldPath.
    if (!RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath)) {
      LOG_ERR("BookMove", "Failed to recover recent book at new path %s", newPath.c_str());
    }
    if (shouldMoveOpenPath) {
      APP_STATE.openEpubPath = newPath;
      if (!APP_STATE.saveToFile()) {
        LOG_ERR("BookMove", "Failed to recover open book at new path %s", newPath.c_str());
      }
    }
    commitMetadata();
    return RenameMigrationResult::KeepRenamed;
  };

  const auto recover = [&]() {
    if (bookRenamed) {
      if (!Storage.rename(newPath.c_str(), oldPath.c_str())) {
        if (preserveOnRenameFailure) return RenameMigrationResult::Uncertain;
        return keepRenamed();
      }
      bookRenamed = false;

      if (!rollbackMovedReferences()) {
        if (Storage.rename(oldPath.c_str(), newPath.c_str())) {
          bookRenamed = true;
          return keepRenamed();
        }
        LOG_ERR("BookMove", "Failed to re-establish new book path after incomplete state rollback: %s",
                newPath.c_str());
      }
    }

    if (!rollbackPreparedStorage()) {
      LOG_ERR("BookMove", "Storage rollback was incomplete; the original book path remains available");
      if (preserveOnRenameFailure) return RenameMigrationResult::Uncertain;
    }
    return RenameMigrationResult::RolledBack;
  };

  ClippingStore::PreparedRename preparedClippings;
  if (strcmp(bookType, "epub") == 0) {
    // Retain the source index and all checked writer/readback capacity before
    // the encompassing intent and before any other store mutates metadata.
    if (!ClippingStore::prepareRenameMigration(oldPath, newPath, title, author, bookType, clippingMigration,
                                               preparedClippings))
      return RenameMigrationResult::RolledBack;
  }
  if (preserveOnRenameFailure && !writeMoveIntent(oldPath, newPath)) return RenameMigrationResult::Uncertain;

  // Publish the new-path metadata before the book itself is renamed. Until
  // the physical rename succeeds, the old book and its source metadata remain
  // intact; after it succeeds, the new-path metadata is already discoverable.
  if (!BookmarkStore::beginRenameMigration(oldPath, newPath, title, author, bookType, bookmarkMigration,
                                           preserveOnRenameFailure)) {
    LOG_ERR("BookMove", "Failed to migrate bookmarks for renamed book %s -> %s", oldPath.c_str(), newPath.c_str());
    if (preserveOnRenameFailure) return RenameMigrationResult::Uncertain;
    return recover();
  }

  if (strcmp(bookType, "epub") == 0) {
    if (!ClippingStore::publishPreparedRename(clippingMigration, preparedClippings)) {
      LOG_ERR("BookMove", "Failed to migrate clippings for renamed book %s -> %s", oldPath.c_str(), newPath.c_str());
      if (preserveOnRenameFailure || clippingMigration.uncertain) return RenameMigrationResult::Uncertain;
      return recover();
    }
  }

  if (!oldCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
    if (!Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
      // A negative FAT acknowledgement does not prove no mutation: the two
      // entries may alias one cluster chain. Manual Move must retain every
      // candidate and stop, not rename/delete candidates in speculative recovery.
      if (preserveOnRenameFailure) return RenameMigrationResult::Uncertain;
      LOG_ERR("BookMove", "Failed to rename cache dir %s -> %s", oldCachePath.c_str(), newCachePath.c_str());
      return recover();
    }
    cacheMoved = true;
  }

  if (!Storage.rename(oldPath.c_str(), newPath.c_str())) {
    if (preserveOnRenameFailure) return RenameMigrationResult::Uncertain;
    LOG_ERR("BookMove", "Failed to rename file: %s -> %s", oldPath.c_str(), newPath.c_str());
    return recover();
  }
  bookRenamed = true;
  // The filesystem mutation is acknowledged even if a later reference save fails.
  library::markLibraryIndexDirty();

  if (!RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath)) {
    if (preserveOnRenameFailure) return RenameMigrationResult::Uncertain;
    return recover();
  }
  recentMoved = true;

  if (shouldMoveOpenPath) {
    APP_STATE.openEpubPath = newPath;
    openPathMoved = true;
    if (!APP_STATE.saveToFile()) {
      if (preserveOnRenameFailure) return RenameMigrationResult::Uncertain;
      LOG_ERR("BookMove", "Failed to save renamed open book path: %s", newPath.c_str());
      return recover();
    }
  }

  commitMetadata();

  if (preserveOnRenameFailure && !metadataCommitted) return RenameMigrationResult::Uncertain;
  if (preserveOnRenameFailure && !Storage.remove(BOOK_MOVE_PENDING)) return RenameMigrationResult::Uncertain;
  return RenameMigrationResult::Success;
}

bool migrateMovedEpubState(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                           const std::string& title, const std::string& author, const bool keepInRecents) {
  bool ok = true;

  const std::string newCachePath = Epub::cachePathForFilePath(newPath, "/.crosspoint");
  if (!oldCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
    if (!Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
      LOG_ERR("BookMove", "Failed to rename cache dir %s -> %s (non-fatal)", oldCachePath.c_str(),
              newCachePath.c_str());
      ok = false;
    }
  }

  if (!BookmarkStore::migrateForFilePath(oldPath, newPath, title, author, "epub")) {
    LOG_ERR("BookMove", "Failed to migrate bookmarks for moved book %s -> %s", oldPath.c_str(), newPath.c_str());
    ok = false;
  }

  if (!ClippingStore::migrateForFilePath(oldPath, newPath, title, author, "epub")) {
    LOG_ERR("BookMove", "Failed to migrate clippings for moved book %s -> %s", oldPath.c_str(), newPath.c_str());
    ok = false;
  }

  if (keepInRecents) {
    (void)RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath);
  } else {
    RECENT_BOOKS.removeByPath(oldPath);
    RECENT_BOOKS.removeByPath(newPath);
  }

  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    APP_STATE.saveToFile();
  }

  return ok;
}

}  // namespace BookMoveUtils
