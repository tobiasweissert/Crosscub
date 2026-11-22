#include "ClippingStore.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>
#include <esp_random.h>
#include <uzlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>

#include "clippings/ClippingPreview.h"
#include "network/ProtectedPaths.h"

namespace {
constexpr uint8_t LEGACY_VERSION = 1;
constexpr uint8_t TEXT_OFFSET_VERSION = 2;
constexpr uint8_t LAYOUT_SIGNATURE_VERSION = 3;
constexpr uint8_t TABLE_VERSION = 4;
constexpr uint8_t VERSION = 5;
constexpr char CLIPPINGS_DIR[] = "/.crosspoint/clippings";
constexpr size_t TEXT_COPY_BUFFER_SIZE = 128;
constexpr char MOVE_PENDING[] = "/.crosspoint/clippings/move.pending";

struct ClippingFileHeader {
  std::string title;
  std::string author;
  std::string path;
  std::string bookType;
  uint16_t count = 0;
};

std::string storeFilePathForBook(const std::string& filePath, const std::string& bookType) {
  const uint32_t crc = uzlib_crc32(filePath.data(), static_cast<unsigned int>(filePath.size()), 0);
  return std::string(CLIPPINGS_DIR) + "/" + bookType + "_" + std::to_string(crc) + ".bin";
}

bool hasStoreRoots(const std::string& path) {
  const auto suffixes = {"", ".deleted", ".bak", ".tmp", ".rename.bak", ".migrate.bak"};
  return std::any_of(suffixes.begin(), suffixes.end(),
                     [&](const char* suffix) { return Storage.exists((path + suffix).c_str()); });
}

std::string modernStorePathForBook(const std::string& path, const std::string& type) {
  return std::string(CLIPPINGS_DIR) + "/" + type + "_" + std::to_string(std::hash<std::string>{}(path)) + ".bin";
}

std::string existingStorePathForBook(const std::string& path, const std::string& type) {
  const auto owner = storeFilePathForBook(path, type);
  const auto modern = modernStorePathForBook(path, type);
  const bool ownerRoots = hasStoreRoots(owner);
  const bool modernRoots = modern != owner && hasStoreRoots(modern);
  // Independent histories have no automatic winner, even if one is only a
  // recovery root or legacy deletion queue. Empty is an explicit refusal.
  if (ownerRoots && modernRoots) return "";
  return modernRoots ? modern : owner;
}

void copyBounded(char* dst, const size_t dstSize, const char* src) {
  if (dstSize == 0) return;
  if (!src) src = "";
  size_t length = 0;
  while (length < dstSize - 1 && src[length]) ++length;
  // If the first excluded byte continues a codepoint, exclude its lead
  // and any already copied continuation bytes as well.
  while (length > 0 && (static_cast<unsigned char>(src[length]) & 0xC0) == 0x80) --length;
  memcpy(dst, src, length);
  dst[length] = '\0';
}

bool readClippingFileHeader(const std::string& fullPath, const char* name, ClippingFileHeader& header) {
  HalFile f;
  if (!Storage.openFileForRead("CLIP", fullPath, f)) {
    return false;
  }

  uint8_t version = 0;
  uint16_t count = 0;
  if (!serialization::tryReadPod(f, version) ||
      (version != LEGACY_VERSION && version != TEXT_OFFSET_VERSION && version != LAYOUT_SIGNATURE_VERSION &&
       version != TABLE_VERSION && version != VERSION) ||
      !serialization::tryReadPod(f, count) || !serialization::tryReadString(f, header.title, CLIPPING_TEXT_MAX) ||
      !serialization::tryReadString(f, header.author, CLIPPING_TEXT_MAX) ||
      !serialization::tryReadString(f, header.path, CLIPPING_TEXT_MAX)) {
    f.close();
    return false;
  }
  if (!f.close()) return false;

  if (count > CLIPPING_MAX_PER_BOOK) {
    return false;
  }
  header.count = count;
  header.bookType = "epub";
  const std::string nameStr = name ? name : "";
  const size_t underscorePos = nameStr.find('_');
  if (underscorePos != std::string::npos) {
    header.bookType = nameStr.substr(0, underscorePos);
  }
  return true;
}

bool copyBytes(HalFile& in, HalFile& out, uint16_t length) {
  std::array<uint8_t, TEXT_COPY_BUFFER_SIZE> buffer{};
  while (length > 0) {
    const size_t chunk = std::min<size_t>(length, buffer.size());
    if (in.read(buffer.data(), chunk) != static_cast<int>(chunk)) {
      return false;
    }
    if (out.write(buffer.data(), chunk) != chunk) {
      return false;
    }
    length = static_cast<uint16_t>(length - chunk);
  }
  return true;
}
}  // namespace

ClippingStore ClippingStore::instance;

bool ClippingStore::loadForBook(const std::string& filePath, const std::string& title, const std::string& author,
                                const std::string& bookType) {
  return loadForBookImpl(filePath, title, author, bookType, false);
}

bool ClippingStore::loadForBookImpl(const std::string& filePath, const std::string& title, const std::string& author,
                                    const std::string& bookType, const bool moveOwner) {
  loaded = false;
  struct FailedLoadCleanup {
    ClippingStore& store;
    ~FailedLoadCleanup() {
      if (!store.loaded) {
        store.clippings.clear();
        store.deletedIds.clear();
      }
    }
  } cleanup{*this};
  clippings.clear();
  quarantined = false;
  if (!moveOwner && Storage.exists(MOVE_PENDING)) return false;
  if (bookType != "epub" || filePath.empty() || filePath[0] != '/' || title.size() > CLIPPING_TEXT_MAX ||
      author.size() > CLIPPING_TEXT_MAX || filePath.size() > CLIPPING_TEXT_MAX) {
    LOG_ERR("CLIP", "Unknown clipping book type: %s", bookType.c_str());
    return false;
  }

  loaded = false;
  deletedIds.clear();
  bookFilePath = filePath;
  bookTitle = title;
  bookAuthor = author;
  dirty = false;
  clippings.clear();

  storeFilePath = existingStorePathForBook(filePath, bookType);
  if (storeFilePath.empty()) return false;
  // Retained publication roots are durable uncertainty, never empty new data.
  const auto suffixes = {".tmp", ".bak", ".rename.bak", ".migrate.bak"};
  if (std::any_of(suffixes.begin(), suffixes.end(),
                  [&](const char* suffix) { return Storage.exists((storeFilePath + suffix).c_str()); }))
    return false;
  if (!Storage.exists(storeFilePath.c_str())) {
    if (hasStoreRoots(storeFilePath)) return false;
    return loaded = true;
  }
  ClippingFileHeader identity;
  if (!readClippingFileHeader(storeFilePath, "epub_", identity) || identity.path != filePath) return false;
  loaded = readFromFile();
  if (!loaded) {
    clippings.clear();
    return false;
  }
  // Sync callers deliberately omit metadata. Only a validated source may
  // supply fallback values; explicitly supplied nonempty metadata still wins.
  if (bookTitle.empty()) bookTitle = identity.title;
  if (bookAuthor.empty()) bookAuthor = identity.author;
  // Import old upstream journals read-only; never truncate an interrupted tail.
  const std::string journalPath = storeFilePath + ".deleted";
  HalFile versionFile;
  uint8_t diskVersion = 0;
  if (!Storage.openFileForRead("CLIP", storeFilePath, versionFile) ||
      !serialization::tryReadPod(versionFile, diskVersion) || !versionFile.close())
    return loaded = false;
  if (diskVersion != VERSION && Storage.exists(journalPath.c_str())) {
    HalFile journal;
    if (!Storage.openFileForRead("CLIP", journalPath, journal) || journal.size() / 65 > UINT16_MAX)
      return loaded = false;
    // Only complete legacy entries participate. The raw journal, including
    // an interrupted tail, remains untouched through checked v5 publication.
    const size_t completeBytes = journal.size() - journal.size() % 65;
    while (journal.position() < completeBytes) {
      char id[65];
      if (journal.read(reinterpret_cast<uint8_t*>(id), sizeof(id)) != sizeof(id) || !memchr(id, 0, sizeof(id)) ||
          !id[0])
        return loaded = false;
      for (const char* p = id; *p; ++p)
        if (static_cast<unsigned char>(*p) < 0x20 || static_cast<unsigned char>(*p) >= 0x7f) return loaded = false;
      if (!deletedIds.push_back(CheckedClippingId(id))) {
        clippings.clear();
        deletedIds.clear();
        return loaded = false;
      }
    }
    if (!journal.close() || deletedIds.size() > UINT16_MAX) return loaded = false;
    // Persist import before acknowledging any remote operation.
    dirty = true;
  }
  return true;
}

void ClippingStore::unload() {
  if (dirty && loaded) saveToFile();
  loaded = false;
  deletedIds.clear();
  clippings.clear();
  bookFilePath.clear();
  bookTitle.clear();
  bookAuthor.clear();
  storeFilePath.clear();
  dirty = false;
}

ClippingStore::AddResult ClippingStore::addClipping(const uint16_t spineIndex, const uint16_t startPage,
                                                    const uint16_t endPage, const uint16_t pageCount,
                                                    const uint16_t startWordIndex, const uint16_t endWordIndex,
                                                    const uint16_t wordCount, const char* chapterTitle,
                                                    const uint16_t paragraphIndex, const std::string& text,
                                                    const uint16_t tableSelection, const uint32_t layoutSignature) {
  if (!loaded || text.size() > CLIPPING_TEXT_MAX) return AddResult::SaveFailed;
  if (clippings.size() >= CLIPPING_MAX_PER_BOOK) {
    LOG_ERR("CLIP", "Clipping limit (%u) reached", CLIPPING_MAX_PER_BOOK);
    return AddResult::LimitReached;
  }

  Clipping clipping;
  clipping.spineIndex = spineIndex;
  clipping.startPage = startPage;
  clipping.endPage = endPage;
  clipping.pageCount = std::max<uint16_t>(1, pageCount);
  clipping.startWordIndex = startWordIndex;
  clipping.endWordIndex = endWordIndex;
  clipping.wordCount = wordCount;
  clipping.paragraphIndex = paragraphIndex;
  clipping.timestamp = static_cast<uint32_t>(millis() / 1000UL);
  clipping.layoutSignature = layoutSignature;
  clipping.tableSelection = tableSelection;
  copyBounded(clipping.chapterTitle, sizeof(clipping.chapterTitle), chapterTitle);
  clipping.textLength = static_cast<uint16_t>(std::min(text.size(), CLIPPING_TEXT_MAX));

  if (!clippings.push_back(clipping)) return AddResult::SaveFailed;
  dirty = true;
  if (!writeToFile(&text, clippings.size() - 1)) {
    clippings.pop_back();
    dirty = true;
    return AddResult::SaveFailed;
  }
  dirty = false;
  return AddResult::Added;
}

bool ClippingStore::removeClippingAt(const size_t index) {
  if (!loaded || index >= clippings.size()) return false;
  if (clippings[index].id[0] && deletedIds.size() >= UINT16_MAX) return false;
  const size_t oldDeletionCount = deletedIds.size();
  if (clippings[index].id[0] && !deletedIds.push_back(CheckedClippingId(clippings[index].id))) return false;
  Clipping clipping = std::move(clippings[index]);
  clippings.erase(clippings.begin() + index);
  dirty = true;
  if (!saveToFile()) {
    deletedIds.resize(oldDeletionCount);
    clippings.insert(clippings.begin() + index, std::move(clipping));
    dirty = true;
    return false;
  }
  return true;
}

const Clipping* ClippingStore::clippingAt(const size_t index) const {
  if (quarantined || index >= clippings.size()) return nullptr;
  return &clippings[index];
}

bool ClippingStore::cacheResolvedLayoutRange(const size_t index, const uint16_t page, const uint16_t startWord,
                                             const uint16_t endWord, const uint32_t layoutSignature) {
  if (index >= clippings.size()) return false;
  if (!cacheClippingResolvedLayoutRange(clippings[index], page, startWord, endWord, layoutSignature)) {
    return false;
  }
  dirty = true;
  return true;
}

bool ClippingStore::readClippingPreview(const size_t index, std::string& out) const {
  out.clear();
  const Clipping* clipping = clippingAt(index);
  if (!clipping || storeFilePath.empty()) {
    LOG_ERR("CLIP", "Invalid clipping preview index: %u", static_cast<unsigned>(index));
    return false;
  }
  if (clipping->textLength == 0) return true;

  HalFile f;
  if (!Storage.openFileForRead("CLIP", storeFilePath, f)) return false;
  if (!f.seek(clipping->textOffset)) {
    f.close();
    LOG_ERR("CLIP", "Failed to seek clipping preview at %u", clipping->textOffset);
    return false;
  }
  const bool ok = clippingPreview::read(f, clipping->textLength, out);
  f.close();
  if (!ok) LOG_ERR("CLIP", "Failed to read clipping preview at %u", clipping->textOffset);
  return ok;
}

bool ClippingStore::readClippingText(const size_t index, std::string& out) const {
  const Clipping* clipping = clippingAt(index);
  if (!clipping) return false;
  return readClippingText(*clipping, out);
}

bool ClippingStore::readClippingText(const Clipping& clipping, std::string& out) const {
  out.clear();
  if (quarantined) return false;
  if (clipping.textLength == 0) return true;
  if (storeFilePath.empty()) return false;

  HalFile f;
  if (!Storage.openFileForRead("CLIP", storeFilePath, f)) {
    return false;
  }
  if (!f.seek(clipping.textOffset)) {
    f.close();
    LOG_ERR("CLIP", "Failed to seek clipping text at %u: %s", clipping.textOffset, storeFilePath.c_str());
    return false;
  }
  out.resize(clipping.textLength);
  const int expected = static_cast<int>(clipping.textLength);
  const bool readOk = f.read(&out[0], clipping.textLength) == expected;
  const bool closed = f.close();
  const bool ok = readOk && closed;
  if (!ok) {
    out.clear();
    LOG_ERR("CLIP", "Failed to read clipping text at %u: %s", clipping.textOffset, storeFilePath.c_str());
  }
  return ok;
}

bool ClippingStore::saveToFile() {
  if (!loaded) return false;
  if (!dirty) return true;
  if (writeToFile()) {
    dirty = false;
    return true;
  }
  return false;
}

void ClippingStore::clearAll() {
  if (!loaded) return;
  CheckedClippingStorage<Clipping> old;
  CheckedClippingStorage<CheckedClippingId> oldDeleted;
  if (!old.cloneFrom(clippings) || !oldDeleted.cloneFrom(deletedIds) ||
      deletedIds.size() + clippings.size() > UINT16_MAX || !deletedIds.reserve(deletedIds.size() + clippings.size()))
    return;
  for (const auto& c : clippings) {
    if (c.id[0] && std::find(deletedIds.begin(), deletedIds.end(), c.id) == deletedIds.end())
      deletedIds.push_back(CheckedClippingId(c.id));  // admitted above
  }
  if (deletedIds.size() > UINT16_MAX) {
    deletedIds = std::move(oldDeleted);
    return;
  }
  clippings.clear();
  dirty = true;
  if (!saveToFile()) {
    clippings = std::move(old);
    deletedIds = std::move(oldDeleted);
  }
}

bool ClippingStore::readFromFile() { return readFromFile(storeFilePath, clippings, &deletedIds); }

bool ClippingStore::readFromFile(const std::string& path, CheckedClippingStorage<Clipping>& out,
                                 CheckedClippingStorage<CheckedClippingId>* deletions) const {
  CheckedClippingStorage<Clipping> ownerRows, modernRows;
  CheckedClippingStorage<CheckedClippingId> ownerDeleted, modernDeleted;
  bool allocationFailed = false;
  const bool ownerOk = readVersionedFile(path, ownerRows, &ownerDeleted, false, &allocationFailed);
  if (allocationFailed) return false;
  const bool modernOk = readVersionedFile(path, modernRows, &modernDeleted, true, &allocationFailed);
  if (allocationFailed) return false;
  out.clear();
  // Never silently choose between two valid interpretations of the same v4.
  if (!ownerOk && !modernOk) return false;
  // An exact empty v4 has no divergent row fields: both decoders describe
  // the same empty history, not two competing interpretations.
  if (ownerOk && modernOk && (!ownerRows.empty() || !modernRows.empty())) return false;
  out = ownerOk ? std::move(ownerRows) : std::move(modernRows);
  if (deletions) *deletions = ownerOk ? std::move(ownerDeleted) : std::move(modernDeleted);
  return true;
}

bool ClippingStore::readVersionedFile(const std::string& path, CheckedClippingStorage<Clipping>& out,
                                      CheckedClippingStorage<CheckedClippingId>* deletions, const bool modernV4,
                                      bool* allocationFailed, const bool allowAllocation) const {
  out.resetSize();
  if (deletions) deletions->resetSize();
  HalFile f;
  if (!Storage.openFileForRead("CLIP", path, f)) {
    return false;
  }

  uint8_t version = 0;
  uint16_t count = 0;
  std::string title;
  std::string author;
  std::string storedPath;
  if (!serialization::tryReadPod(f, version) ||
      (version != LEGACY_VERSION && version != TEXT_OFFSET_VERSION && version != LAYOUT_SIGNATURE_VERSION &&
       version != TABLE_VERSION && version != VERSION) ||
      !serialization::tryReadPod(f, count) || !serialization::tryReadString(f, title, CLIPPING_TEXT_MAX) ||
      !serialization::tryReadString(f, author, CLIPPING_TEXT_MAX) ||
      !serialization::tryReadString(f, storedPath, CLIPPING_TEXT_MAX)) {
    f.close();
    LOG_ERR("CLIP", "Failed to read clipping header: %s", path.c_str());
    return false;
  }

  if (count > CLIPPING_MAX_PER_BOOK) {
    LOG_ERR("CLIP", "Clipping count %u exceeds max, file may be corrupt: %s", count, path.c_str());
    f.close();
    return false;
  }

  if (modernV4 && version != TABLE_VERSION) {
    f.close();
    return false;
  }
  const auto allocationRefused = [&]() {
    if (allocationFailed) *allocationFailed = true;
    f.close();
    return false;
  };
  if (version == VERSION) {
    uint16_t n = 0;
    if (!serialization::tryReadPod(f, n)) return false;
    if (deletions && !allowAllocation && n > deletions->capacity()) return false;
    for (uint16_t i = 0; i < n; ++i) {
      char id[65];
      if (f.read(reinterpret_cast<uint8_t*>(id), sizeof(id)) != sizeof(id) || !memchr(id, 0, sizeof(id)) || !id[0])
        return false;
      if (deletions && !deletions->push_back(CheckedClippingId(id))) return allocationRefused();
    }
  }
  if (!allowAllocation && count > out.capacity()) return false;
  if (!out.reserve(count)) return allocationRefused();
  for (uint16_t i = 0; i < count; ++i) {
    Clipping clipping;
    if (!serialization::tryReadPod(f, clipping.spineIndex) || !serialization::tryReadPod(f, clipping.startPage) ||
        !serialization::tryReadPod(f, clipping.endPage) || !serialization::tryReadPod(f, clipping.pageCount) ||
        !serialization::tryReadPod(f, clipping.startWordIndex) ||
        !serialization::tryReadPod(f, clipping.endWordIndex) || !serialization::tryReadPod(f, clipping.wordCount) ||
        !serialization::tryReadPod(f, clipping.paragraphIndex) || !serialization::tryReadPod(f, clipping.timestamp)) {
      f.close();
      LOG_ERR("CLIP", "Clipping file truncated at record %u: %s", i, path.c_str());
      return false;
    }
    if (version >= LAYOUT_SIGNATURE_VERSION && !serialization::tryReadPod(f, clipping.layoutSignature)) {
      f.close();
      LOG_ERR("CLIP", "Clipping file truncated at layout signature, record %u: %s", i, path.c_str());
      return false;
    }
    if (!modernV4 && version >= TABLE_VERSION && !serialization::tryReadPod(f, clipping.tableSelection)) {
      f.close();
      LOG_ERR("CLIP", "Clipping file truncated at table selection, record %u: %s", i, path.c_str());
      return false;
    }
    if (version == VERSION || modernV4) {
      uint8_t pending = 0;
      if (!serialization::tryReadPod(f, clipping.startOffset) || !serialization::tryReadPod(f, clipping.endOffset) ||
          !serialization::tryReadPod(f, clipping.syncRevision) || !serialization::tryReadPod(f, pending) ||
          pending > 1 || f.read(reinterpret_cast<uint8_t*>(clipping.id), sizeof(clipping.id)) != sizeof(clipping.id) ||
          !memchr(clipping.id, 0, sizeof(clipping.id)))
        return false;
      clipping.pendingUpload = pending != 0;
    }
    if (f.read(reinterpret_cast<uint8_t*>(clipping.chapterTitle), sizeof(clipping.chapterTitle)) !=
        sizeof(clipping.chapterTitle)) {
      f.close();
      LOG_ERR("CLIP", "Clipping file truncated at chapter title, record %u: %s", i, path.c_str());
      return false;
    }
    clipping.chapterTitle[sizeof(clipping.chapterTitle) - 1] = '\0';
    if (version == LEGACY_VERSION) {
      uint32_t textLen = 0;
      if (!serialization::tryReadPod(f, textLen)) {
        f.close();
        LOG_ERR("CLIP", "Clipping file truncated at text length, record %u: %s", i, path.c_str());
        return false;
      }
      clipping.textOffset = static_cast<uint32_t>(f.position());
      clipping.textLength = static_cast<uint16_t>(std::min<uint32_t>(textLen, CLIPPING_TEXT_MAX));
      if (textLen > CLIPPING_TEXT_MAX || textLen > f.size() - f.position() || (textLen > 0 && !f.seekCur(textLen))) {
        f.close();
        LOG_ERR("CLIP", "Clipping file truncated at text, record %u: %s", i, path.c_str());
        return false;
      }
    } else {
      if (!serialization::tryReadPod(f, clipping.textLength)) {
        f.close();
        LOG_ERR("CLIP", "Clipping file truncated at text length, record %u: %s", i, path.c_str());
        return false;
      }
      if (clipping.textLength > CLIPPING_TEXT_MAX) {
        f.close();
        LOG_ERR("CLIP", "Clipping text length %u exceeds max, record %u: %s", clipping.textLength, i, path.c_str());
        return false;
      }
      clipping.textOffset = static_cast<uint32_t>(f.position());
      if (clipping.textLength > f.size() - f.position() ||
          (clipping.textLength > 0 && !f.seekCur(clipping.textLength))) {
        f.close();
        LOG_ERR("CLIP", "Clipping file truncated at text, record %u: %s", i, path.c_str());
        return false;
      }
    }
    out.push_back(std::move(clipping));
  }

  const bool complete = f.position() == f.size();
  const bool closed = f.close();
  if (!complete || !closed) {
    out.clear();
    return false;
  }
  return true;
}

bool ClippingStore::writeToFile(const std::string* replacementText, const size_t replacementIndex,
                                const std::string* sourcePathOverride, const bool preserveOnRenameFailure,
                                WriteAdmission* admission) {
  if (quarantined) return false;
  if (!sourcePathOverride && Storage.exists(MOVE_PENDING)) return false;
  const std::string tmpPath = storeFilePath + ".tmp";
  const std::string backupPath = storeFilePath + ".bak";
  (void)preserveOnRenameFailure;  // All new writes obey the stronger conservative contract.
  if (storeFilePath.empty() || clippings.size() > CLIPPING_MAX_PER_BOOK || deletedIds.size() > UINT16_MAX ||
      Storage.exists(tmpPath.c_str()) || Storage.exists(backupPath.c_str()) ||
      Storage.exists((storeFilePath + ".rename.bak").c_str()) ||
      Storage.exists((storeFilePath + ".migrate.bak").c_str()))
    return false;

  // Every index, deletion and writer/verification allocation is admitted before
  // creating a candidate. Reuse this capacity for both readbacks; no fallible
  // clone or scratch allocation is permitted after publication starts.
  const uint16_t count = static_cast<uint16_t>(clippings.size());
  WriteAdmission localAdmission;
  auto& scratch = admission ? *admission : localAdmission;
  if (!scratch.reserve(count, deletedIds.size())) return false;
  auto& newTextOffsets = scratch.offsets;
  auto& newTextLengths = scratch.lengths;
  auto& checked = scratch.rows;
  auto& checkedDeletions = scratch.deletions;
  newTextOffsets.resetSize();
  newTextLengths.resetSize();

  HalFile source;
  const std::string& sourcePath = sourcePathOverride ? *sourcePathOverride : storeFilePath;
  const bool hasSource = Storage.exists(sourcePath.c_str());
  if (hasSource && !Storage.openFileForRead("CLIP", sourcePath, source)) {
    LOG_ERR("CLIP", "Failed to open clipping source for rewrite: %s", sourcePath.c_str());
    return false;
  }

  Storage.mkdir("/.crosspoint");
  Storage.mkdir(CLIPPINGS_DIR);
  HalFile f = Storage.open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  if (!f) {
    if (source) source.close();
    LOG_ERR("CLIP", "Failed to open clipping temp file for write: %s", tmpPath.c_str());
    return false;
  }
  if (!serialization::tryWritePod(f, VERSION) || !serialization::tryWritePod(f, count) ||
      !serialization::tryWriteString(f, bookTitle) || !serialization::tryWriteString(f, bookAuthor) ||
      !serialization::tryWriteString(f, bookFilePath)) {
    LOG_ERR("CLIP", "Failed to write clipping header: %s", tmpPath.c_str());
    f.close();
    if (source) source.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }

  const auto abandonUnpublished = [&]() {
    const bool closed = f.close();
    const bool sourceClosed = !source || source.close();
    // An uncertain close must retain its candidate, just like other writer
    // branches. A negative remove acknowledgement is never retried here.
    if (closed && sourceClosed) Storage.remove(tmpPath.c_str());
    return false;
  };
  const uint16_t deletionCount = static_cast<uint16_t>(deletedIds.size());
  if (!serialization::tryWritePod(f, deletionCount)) return abandonUnpublished();
  for (const auto& id : deletedIds) {
    char bytes[65] = {};
    if (id.empty() || id.size() >= sizeof(bytes)) return abandonUnpublished();
    memcpy(bytes, id.data(), id.size());
    if (f.write(reinterpret_cast<const uint8_t*>(bytes), sizeof(bytes)) != sizeof(bytes)) return abandonUnpublished();
  }

  for (uint16_t i = 0; i < count; ++i) {
    const Clipping& clipping = clippings[i];
    if (!serialization::tryWritePod(f, clipping.spineIndex) || !serialization::tryWritePod(f, clipping.startPage) ||
        !serialization::tryWritePod(f, clipping.endPage) || !serialization::tryWritePod(f, clipping.pageCount) ||
        !serialization::tryWritePod(f, clipping.startWordIndex) ||
        !serialization::tryWritePod(f, clipping.endWordIndex) || !serialization::tryWritePod(f, clipping.wordCount) ||
        !serialization::tryWritePod(f, clipping.paragraphIndex) || !serialization::tryWritePod(f, clipping.timestamp) ||
        !serialization::tryWritePod(f, clipping.layoutSignature) ||
        !serialization::tryWritePod(f, clipping.tableSelection) ||
        !serialization::tryWritePod(f, clipping.startOffset) || !serialization::tryWritePod(f, clipping.endOffset) ||
        !serialization::tryWritePod(f, clipping.syncRevision) ||
        !serialization::tryWritePod(f, static_cast<uint8_t>(clipping.pendingUpload)) ||
        f.write(reinterpret_cast<const uint8_t*>(clipping.id), sizeof(clipping.id)) != sizeof(clipping.id) ||
        f.write(reinterpret_cast<const uint8_t*>(clipping.chapterTitle), sizeof(clipping.chapterTitle)) !=
            sizeof(clipping.chapterTitle)) {
      LOG_ERR("CLIP", "Failed to write clipping record %u: %s", i, storeFilePath.c_str());
      f.close();
      if (source) source.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }

    const bool useReplacement = replacementText && i == replacementIndex;
    const uint16_t textLen = useReplacement
                                 ? static_cast<uint16_t>(std::min(replacementText->size(), CLIPPING_TEXT_MAX))
                                 : clipping.textLength;
    if (!serialization::tryWritePod(f, textLen)) {
      LOG_ERR("CLIP", "Failed to write clipping text length %u: %s", i, tmpPath.c_str());
      f.close();
      if (source) source.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }

    const uint32_t newTextOffset = static_cast<uint32_t>(f.position());
    bool wroteText = true;
    if (textLen > 0 && useReplacement) {
      wroteText = f.write(reinterpret_cast<const uint8_t*>(replacementText->data()), textLen) == textLen;
    } else if (textLen > 0) {
      wroteText = source && source.seek(clipping.textOffset) && copyBytes(source, f, textLen);
    }
    if (!wroteText) {
      LOG_ERR("CLIP", "Failed to write clipping text %u: %s", i, tmpPath.c_str());
      f.close();
      if (source) source.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }
    newTextOffsets.push_back(newTextOffset);
    newTextLengths.push_back(textLen);
  }

  if (!f.sync()) {
    LOG_ERR("CLIP", "Failed to sync clipping file: %s", tmpPath.c_str());
    f.close();
    if (source) source.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!f.close()) {
    if (source) source.close();
    return false;
  }
  if (source && !source.close()) return false;
  const auto verifySnapshot = [&](const std::string& candidate, const std::string& original) {
    ClippingFileHeader header;
    if (!readClippingFileHeader(candidate, "epub_", header) || header.title != bookTitle ||
        header.author != bookAuthor || header.path != bookFilePath ||
        !readVersionedFile(candidate, checked, &checkedDeletions, false, nullptr, false) || checked.size() != count ||
        checkedDeletions != deletedIds)
      return false;
    ClippingStore actual;
    actual.storeFilePath = candidate;
    ClippingStore expected;
    expected.storeFilePath = original;
    for (size_t i = 0; i < count; ++i) {
      const auto& a = checked[i];
      const auto& b = clippings[i];
      if (a.spineIndex != b.spineIndex || a.startPage != b.startPage || a.endPage != b.endPage ||
          a.pageCount != b.pageCount || a.startWordIndex != b.startWordIndex || a.endWordIndex != b.endWordIndex ||
          a.wordCount != b.wordCount || a.paragraphIndex != b.paragraphIndex || a.timestamp != b.timestamp ||
          a.layoutSignature != b.layoutSignature || a.tableSelection != b.tableSelection ||
          a.startOffset != b.startOffset || a.endOffset != b.endOffset || a.syncRevision != b.syncRevision ||
          a.pendingUpload != b.pendingUpload || memcmp(a.id, b.id, sizeof(a.id)) ||
          memcmp(a.chapterTitle, b.chapterTitle, sizeof(a.chapterTitle)))
        return false;
      std::string actualText, expectedText;
      if (!actual.readClippingText(a, actualText)) return false;
      if (replacementText && i == replacementIndex)
        expectedText = *replacementText;
      else if (!expected.readClippingText(b, expectedText))
        return false;
      if (actualText != expectedText) return false;
    }
    return true;
  };
  if (!verifySnapshot(tmpPath, sourcePath)) return false;

  const bool replacingDestination = Storage.exists(storeFilePath.c_str());
  if (replacingDestination && !Storage.rename(storeFilePath.c_str(), backupPath.c_str())) {
    LOG_ERR("CLIP", "Failed to back up clipping file: %s", storeFilePath.c_str());
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), storeFilePath.c_str())) {
    LOG_ERR("CLIP", "Failed to replace clipping file: %s", storeFilePath.c_str());
    quarantined = true;
    loaded = false;
    return false;  // Negative FAT acknowledgement may already have published: retain all roots.
  }
  const std::string publishedSource =
      sourcePathOverride ? *sourcePathOverride : (replacingDestination ? backupPath : sourcePath);
  if (!verifySnapshot(storeFilePath, publishedSource)) {
    quarantined = true;
    loaded = false;
    return false;
  }
  // Checked final row+tombstone publication is the commit point. Install its
  // offsets before obsolete-root cleanup, whose negative acknowledgement may
  // mean either retained backup or an already removed backup. Never roll RAM
  // back against this verified final. A retained backup still gates new writes.
  for (uint16_t i = 0; i < count; ++i) {
    clippings[i].textOffset = newTextOffsets[i];
    clippings[i].textLength = newTextLengths[i];
  }
  if (replacingDestination && !Storage.remove(backupPath.c_str()))
    LOG_ERR("CLIP", "Verified clipping commit retained uncertain backup cleanup: %s", backupPath.c_str());
  return true;
}

bool ClippingStore::hasAnyClippings() {
  std::vector<ClippedBookEntry> books;
  getAllClippedBooks(books);
  return !books.empty();
}

bool ClippingStore::getAllClippedBooks(std::vector<ClippedBookEntry>& out) {
  if (!Storage.exists(CLIPPINGS_DIR)) return true;

  const auto files = Storage.listFiles(CLIPPINGS_DIR);
  for (const auto& name : files) {
    const std::string filename = name.c_str();
    if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".bin") continue;
    ClippingFileHeader header;
    const std::string fullPath = std::string(CLIPPINGS_DIR) + "/" + name.c_str();
    if (!readClippingFileHeader(fullPath, name.c_str(), header)) continue;
    if (header.path.empty() || header.count == 0 || !Storage.exists(header.path.c_str())) continue;

    auto existing = std::find_if(out.begin(), out.end(), [&](const ClippedBookEntry& entry) {
      return entry.bookPath == header.path && entry.bookType == header.bookType;
    });
    if (existing != out.end()) {
      existing->count = std::max(existing->count, header.count);
      continue;
    }
    out.push_back({std::move(header.title), std::move(header.author), std::move(header.path),
                   std::move(header.bookType), header.count});
  }
  return true;
}

bool ClippingStore::canDeleteForFilePath(const std::string& filePath, const std::string& bookType) {
  if (bookType != "epub" || Storage.exists(MOVE_PENDING)) return false;
  const std::string path = existingStorePathForBook(filePath, bookType);
  if (path.empty()) return false;
  const std::string canonical = storeFilePathForBook(filePath, bookType);
  const std::string modern =
      std::string(CLIPPINGS_DIR) + "/" + bookType + "_" + std::to_string(std::hash<std::string>{}(filePath)) + ".bin";
  if (canonical != modern && Storage.exists(canonical.c_str()) && Storage.exists(modern.c_str())) return false;
  const auto suffixes = {".bak", ".tmp", ".rename.bak", ".migrate.bak"};
  if (std::any_of(suffixes.begin(), suffixes.end(),
                  [&](const char* suffix) { return Storage.exists((path + suffix).c_str()); }))
    return false;
  if (!Storage.exists(path.c_str()) && hasStoreRoots(path)) return false;
  return true;
}

bool ClippingStore::deleteForFilePath(const std::string& filePath, const std::string& bookType) {
  if (!canDeleteForFilePath(filePath, bookType)) return false;
  const std::string path = existingStorePathForBook(filePath, bookType);
  if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) return false;
  const std::string journal = path + ".deleted";
  return !Storage.exists(journal.c_str()) || Storage.remove(journal.c_str());
}

bool ClippingStore::canRenameForFilePath(const std::string& oldFilePath, const std::string& newFilePath,
                                         const std::string& bookType) {
  if (bookType != "epub" || Storage.exists(MOVE_PENDING) || oldFilePath.empty() || newFilePath.empty()) return false;
  if (oldFilePath == newFilePath) return true;
  if (hasStoreRoots(storeFilePathForBook(newFilePath, bookType)) ||
      hasStoreRoots(modernStorePathForBook(newFilePath, bookType)))
    return false;
  const auto source = existingStorePathForBook(oldFilePath, bookType);
  if (source.empty() || Storage.exists((source + ".deleted").c_str())) return false;
  ClippingStore validation;
  return validation.loadForBook(oldFilePath, "", "", bookType);
}

bool ClippingStore::migrateForFilePath(const std::string& oldFilePath, const std::string& newFilePath,
                                       const std::string& title, const std::string& author,
                                       const std::string& bookType) {
  RenameMigration migration;
  return beginRenameMigration(oldFilePath, newFilePath, title, author, bookType, migration, true) &&
         commitRenameMigration(migration);
}

bool ClippingStore::beginRenameMigration(const std::string& oldFilePath, const std::string& newFilePath,
                                         const std::string& title, const std::string& author,
                                         const std::string& bookType, RenameMigration& migration,
                                         const bool preserveOnRenameFailure) {
  (void)preserveOnRenameFailure;
  PreparedRename prepared;
  return prepareRenameMigration(oldFilePath, newFilePath, title, author, bookType, migration, prepared) &&
         publishPreparedRename(migration, prepared);
}

bool ClippingStore::prepareRenameMigration(const std::string& oldFilePath, const std::string& newFilePath,
                                           const std::string& title, const std::string& author,
                                           const std::string& bookType, RenameMigration& migration,
                                           PreparedRename& prepared) {
  prepared = {};
  migration = {};
  if (Storage.exists(MOVE_PENDING)) return false;
  if (bookType != "epub") {
    LOG_ERR("CLIP", "Unknown clipping book type for rename migration: %s", bookType.c_str());
    return false;
  }
  if (oldFilePath.empty() || newFilePath.empty() || oldFilePath == newFilePath) {
    prepared.ready = true;
    return true;
  }

  migration.sourcePath = existingStorePathForBook(oldFilePath, bookType);
  if (migration.sourcePath.empty()) return false;
  migration.destinationPath = storeFilePathForBook(newFilePath, bookType);
  migration.destinationBackupPath = migration.destinationPath + ".rename.bak";
  // Clippings are not merged. Manual Move refuses every retained destination
  // candidate rather than silently discarding another book's records.
  if (hasStoreRoots(migration.destinationPath) || hasStoreRoots(modernStorePathForBook(newFilePath, bookType)))
    return false;
  if (!Storage.exists(migration.sourcePath.c_str())) {
    prepared.ready = !hasStoreRoots(migration.sourcePath);
    return prepared.ready;
  }
  if (migration.sourcePath == migration.destinationPath) {
    LOG_ERR("CLIP", "Clipping storage hash collision during rename migration");
    return false;
  }

  // Raw legacy recovery bytes (including interrupted tails) cannot be moved
  // until explicit deletion acknowledgement retires the journal. A v5-only
  // tombstone queue is unaffected by this conservative availability restriction.
  if (Storage.exists((migration.sourcePath + ".deleted").c_str())) return false;
  auto& writer = prepared.writer;
  if (!writer.loadForBook(oldFilePath, title, author, bookType) ||
      !prepared.admission.reserve(writer.clippings.size(), writer.deletedIds.size()))
    return false;
  writer.bookFilePath = newFilePath;
  writer.storeFilePath = migration.destinationPath;
  prepared.ready = true;
  return true;
}

bool ClippingStore::publishPreparedRename(RenameMigration& migration, PreparedRename& prepared) {
  if (!prepared.ready || Storage.exists(MOVE_PENDING)) return false;
  prepared.ready = false;
  auto& writer = prepared.writer;
  if (!writer.loaded) return true;  // No source history, or a no-op path.
  migration.active = true;
  migration.uncertain = true;
  if (!writer.writeToFile(nullptr, SIZE_MAX, &migration.sourcePath, true, &prepared.admission)) {
    LOG_ERR("CLIP", "Failed to write transactional clipping rename: %s", migration.destinationPath.c_str());
    // Retain candidates; neither rollback nor commit is safe after a negative acknowledgement.
    return false;
  }
  migration.uncertain = false;
  return true;
}

bool ClippingStore::commitRenameMigration(RenameMigration& migration) {
  return commitRenameMigrationImpl(migration, false);
}

bool ClippingStore::commitRenameMigrationImpl(RenameMigration& migration, const bool moveOwner) {
  if (!moveOwner && Storage.exists(MOVE_PENDING)) return false;
  if (migration.uncertain) return false;
  if (!migration.active) return true;
  bool ok = true;
  if (Storage.exists(migration.sourcePath.c_str()) && !Storage.remove(migration.sourcePath.c_str())) {
    LOG_ERR("CLIP", "Failed to delete renamed clipping source: %s", migration.sourcePath.c_str());
    ok = false;
  }
  if (migration.destinationBackedUp && Storage.exists(migration.destinationBackupPath.c_str()) &&
      !Storage.remove(migration.destinationBackupPath.c_str())) {
    LOG_ERR("CLIP", "Failed to delete clipping rename backup: %s", migration.destinationBackupPath.c_str());
    ok = false;
  }
  if (ok) {
    const std::string journal = migration.sourcePath + ".deleted";
    if (Storage.exists(journal.c_str()) && !Storage.remove(journal.c_str())) ok = false;
  }
  if (ok) migration.active = false;
  return ok;
}

bool ClippingStore::rollbackRenameMigration(RenameMigration& migration) {
  if (Storage.exists(MOVE_PENDING)) return false;
  if (migration.uncertain) return false;
  if (!migration.active) return true;
  bool ok = true;
  if (Storage.exists(migration.destinationPath.c_str()) && !Storage.remove(migration.destinationPath.c_str())) {
    LOG_ERR("CLIP", "Failed to remove rolled-back clipping destination: %s", migration.destinationPath.c_str());
    ok = false;
  }
  if (migration.destinationBackedUp &&
      !Storage.rename(migration.destinationBackupPath.c_str(), migration.destinationPath.c_str())) {
    LOG_ERR("CLIP", "Failed to restore clipping rename backup: %s", migration.destinationBackupPath.c_str());
    ok = false;
  }
  if (ok) migration.active = false;
  return ok;
}

namespace {
bool validSyncId(const char (&id)[65]) {
  const char* end = static_cast<const char*>(memchr(id, 0, sizeof(id)));
  if (!end || end == id) return false;
  return std::all_of(id, end, [](unsigned char c) { return c >= 0x20 && c < 0x7f; });
}
}  // namespace

ClippingStore::AddResult ClippingStore::addClipping(uint16_t spine, uint16_t start, uint16_t end, uint16_t pages,
                                                    uint16_t first, uint16_t last, uint16_t words, const char* chapter,
                                                    uint16_t paragraph, const std::string& text, uint32_t layout,
                                                    uint32_t sourceStart, uint32_t sourceEnd) {
  if (!loaded || text.size() > CLIPPING_TEXT_MAX || ((sourceStart == UINT32_MAX) != (sourceEnd == UINT32_MAX)) ||
      (sourceStart != UINT32_MAX && sourceStart >= sourceEnd))
    return AddResult::SaveFailed;
  if (clippings.size() >= CLIPPING_MAX_PER_BOOK) return AddResult::LimitReached;
  Clipping c;
  c.spineIndex = spine;
  c.startPage = start;
  c.endPage = end;
  c.pageCount = std::max<uint16_t>(1, pages);
  c.startWordIndex = first;
  c.endWordIndex = last;
  c.wordCount = words;
  c.paragraphIndex = paragraph;
  c.layoutSignature = layout;
  c.startOffset = sourceStart;
  c.endOffset = sourceEnd;
  c.timestamp = static_cast<uint32_t>(millis() / 1000UL);
  copyBounded(c.chapterTitle, sizeof(c.chapterTitle), chapter);
  if (!clippings.push_back(c)) return AddResult::SaveFailed;
  if (!writeToFile(&text, clippings.size() - 1)) {
    clippings.pop_back();
    return AddResult::SaveFailed;
  }
  dirty = false;
  return AddResult::Added;
}

bool ClippingStore::prepareSync() {
  if (!loaded) return false;
  CheckedClippingStorage<Clipping> previous;
  if (!previous.cloneFrom(clippings)) return false;
  for (auto& c : clippings) {
    if (c.id[0]) {
      if (!validSyncId(c.id)) {
        clippings = std::move(previous);
        return false;
      }
      continue;
    }
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(bytes); ++i) {
      c.id[i * 2] = hex[bytes[i] >> 4];
      c.id[i * 2 + 1] = hex[bytes[i] & 15];
    }
    c.id[32] = 0;
    dirty = true;
  }
  if (saveToFile()) return true;
  clippings = std::move(previous);
  return false;
}

bool ClippingStore::finishUploads() {
  if (!loaded) return false;
  CheckedClippingStorage<Clipping> previous;
  if (!previous.cloneFrom(clippings)) return false;
  for (auto& c : clippings)
    if (c.pendingUpload) {
      c.pendingUpload = false;
      dirty = true;
    }
  if (saveToFile()) return true;
  clippings = std::move(previous);
  return false;
}

bool ClippingStore::nextDeletion(uint32_t& offset, char (&id)[65]) const {
  id[0] = 0;
  if (!loaded || offset % sizeof(id) || offset / sizeof(id) > deletedIds.size()) return false;
  while (offset / sizeof(id) < deletedIds.size()) {
    const auto& next = deletedIds[offset / sizeof(id)];
    offset += sizeof(id);
    if (std::any_of(clippings.begin(), clippings.end(), [&](const Clipping& c) { return next == c.id; })) continue;
    copyBounded(id, sizeof(id), next.c_str());
    return true;
  }
  return true;
}

bool ClippingStore::finishDeletions() {
  if (!loaded) return false;
  if (deletedIds.empty()) return true;
  CheckedClippingStorage<CheckedClippingId> previous;
  previous.swap(deletedIds);
  dirty = true;
  if (saveToFile()) {
    const std::string oldJournal = storeFilePath + ".deleted";
    if (Storage.exists(oldJournal.c_str()) && !Storage.remove(oldJournal.c_str())) {
      deletedIds = std::move(previous);
      dirty = true;
      return false;
    }
    return true;
  }
  deletedIds = std::move(previous);
  return false;
}

bool ClippingStore::applyRemote(const Clipping& remote, const std::string& text, const bool deleted) {
  if (!loaded || !validSyncId(remote.id) ||
      (!deleted && (text.size() > CLIPPING_TEXT_MAX || remote.pageCount == 0 || remote.startPage > remote.endPage ||
                    ((remote.startOffset == UINT32_MAX) != (remote.endOffset == UINT32_MAX)) ||
                    (remote.startOffset != UINT32_MAX && remote.startOffset >= remote.endOffset))))
    return false;
  // A local durable tombstone wins over an older server replay until acknowledged.
  if (!deleted && std::find(deletedIds.begin(), deletedIds.end(), remote.id) != deletedIds.end()) return true;
  auto found =
      std::find_if(clippings.begin(), clippings.end(), [&](const Clipping& c) { return !strcmp(c.id, remote.id); });
  const size_t index = static_cast<size_t>(found - clippings.begin());
  if (found == clippings.end() && deleted) return true;
  if (found != clippings.end() && found->syncRevision > remote.syncRevision) return true;
  if (found != clippings.end() && !deleted && found->syncRevision == remote.syncRevision && !found->pendingUpload)
    return true;
  if (found == clippings.end() && clippings.size() >= CLIPPING_MAX_PER_BOOK) return false;
  CheckedClippingStorage<Clipping> previous;
  if (!previous.cloneFrom(clippings)) return false;
  if (deleted)
    clippings.erase(found);
  else {
    Clipping c = remote;
    c.pendingUpload = false;
    c.resolvedLayoutBoundaries = 0;
    if (found == clippings.end()) {
      if (!clippings.push_back(c)) return false;
    } else
      *found = c;
  }
  if (writeToFile(deleted ? nullptr : &text, index)) {
    dirty = false;
    return true;
  }
  clippings = std::move(previous);
  return false;
}

bool ClippingStore::readClippingPreview(size_t index, char* out, size_t size, size_t& length) const {
  length = 0;
  if (!out || size == 0) return false;
  out[0] = 0;
  std::string preview;
  if (!readClippingPreview(index, preview)) return false;
  // Do not silently cut a UTF-8 sequence at the caller's byte bound.
  length = std::min(preview.size(), size - 1);
  while (length < preview.size() && length && (static_cast<unsigned char>(preview[length]) & 0xc0) == 0x80) --length;
  memcpy(out, preview.data(), length);
  out[length] = 0;
  return true;
}

bool ClippingStore::moveBook(const std::string& from, const std::string& to) {
  if (Storage.exists(MOVE_PENDING)) return false;
  if (from == to) return true;
  if (from.empty() || to.empty() || from[0] != '/' || to[0] != '/' || to.compare(0, from.size() + 1, from + "/") == 0 ||
      Storage.exists(to.c_str()))
    return false;
  const auto validBookPath = [](const std::string& path) {
    if (path.size() > CLIPPING_TEXT_MAX || path.back() == '/') return false;
    if (std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; })) return false;
    size_t at = 1;
    while (at < path.size()) {
      const auto end = path.find('/', at);
      const auto component = path.substr(at, end == std::string::npos ? end : end - at);
      if (component.empty() || component == "." || component == "..") return false;
      if (end == std::string::npos) break;
      at = end + 1;
    }
    return true;
  };
  if (!validBookPath(from) || !validBookPath(to)) return false;
  const auto protectedTree = [](const std::string& path) {
    if (path == "/" || protectedpaths::isSensitivePath(path.c_str())) return true;
    size_t end = 1;
    do {
      end = path.find('/', end);
      const std::string prefix = path.substr(0, end);
      if (protectedpaths::isSensitivePath((prefix + "/wifi.json").c_str())) return true;
      if (end != std::string::npos) ++end;
    } while (end != std::string::npos);
    return false;
  };
  if (protectedTree(from) || protectedTree(to)) return false;
  struct Item {
    std::string from, to;
    RenameMigration migration;
    ClippingStore writer;
    WriteAdmission admission;
  };
  CheckedClippingStorage<Item> items;
  // Enumerate all affected stores, including empty snapshots containing tombstones.
  if (Storage.exists(CLIPPINGS_DIR) && !FsHelpers::directoryCanBeEnumerated(CLIPPINGS_DIR)) return false;
  for (const auto& name : Storage.listFiles(CLIPPINGS_DIR)) {
    const std::string filename = name.c_str();
    if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".bin") {
      // A sidecar without its primary has no trustworthy book identity (raw
      // journals have no path at all). Do not pick a recovery winner or ignore
      // an unknown root, even for a folder move. Require explicit recovery.
      const auto bin = filename.find(".bin");
      if (bin == std::string::npos) return false;
      const auto suffix = filename.substr(bin + 4);
      if (suffix != ".tmp" && suffix != ".bak" && suffix != ".rename.bak" && suffix != ".migrate.bak" &&
          suffix != ".deleted")
        return false;
      const auto primary = std::string(CLIPPINGS_DIR) + "/" + filename.substr(0, bin + 4);
      if (!Storage.exists(primary.c_str())) return false;
      continue;  // The affected primary's normal admission checks all its roots.
    }
    ClippingFileHeader h;
    const std::string path = std::string(CLIPPINGS_DIR) + "/" + filename;
    if (!readClippingFileHeader(path, filename.c_str(), h)) return false;
    if (h.path != from && h.path.compare(0, from.size() + 1, from + "/") != 0) continue;
    if (Storage.exists((path + ".deleted").c_str())) return false;
    if (items.size() >= CLIPPING_MAX_PER_BOOK) return false;
    Item item;
    item.from = h.path;
    item.to = to + h.path.substr(from.size());
    if (!items.push_back(std::move(item))) return false;
  }
  if (!Storage.exists(from.c_str())) return false;
  if (instance.loaded &&
      (instance.bookFilePath == from || instance.bookFilePath.compare(0, from.size() + 1, from + "/") == 0)) {
    if (!instance.saveToFile()) return false;
    instance.unload();
  }
  // Deterministic source/destination refusal is read-only. It must precede
  // the encompassing durable intent; only attempted mutations may latch it.
  for (auto& item : items) {
    const auto source = existingStorePathForBook(item.from, "epub");
    if (source.empty() || hasStoreRoots(storeFilePathForBook(item.to, "epub")) ||
        hasStoreRoots(modernStorePathForBook(item.to, "epub")))
      return false;
    // Keep the validated source index and all writer/verification capacity
    // alive through the move. Admission is before the global mutation intent.
    if (!item.writer.loadForBook(item.from, "", "", "epub") ||
        !item.admission.reserve(item.writer.clippings.size(), item.writer.deletedIds.size()))
      return false;
    item.migration.sourcePath = source;
    item.migration.destinationPath = storeFilePathForBook(item.to, "epub");
    item.migration.destinationBackupPath = item.migration.destinationPath + ".rename.bak";
    item.writer.bookFilePath = item.to;
    item.writer.storeFilePath = item.migration.destinationPath;
  }
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(CLIPPINGS_DIR);
  const std::string intent = "CPMOVE1\n" + from + "\n" + to + "\n";
  HalFile marker = Storage.open(MOVE_PENDING, O_WRONLY | O_CREAT | O_EXCL);
  if (!marker || marker.write(reinterpret_cast<const uint8_t*>(intent.data()), intent.size()) != intent.size() ||
      !marker.sync() || !marker.close())
    return false;
  HalFile verify;
  if (!Storage.openFileForRead("CLIP", MOVE_PENDING, verify) || verify.size() != intent.size()) return false;
  std::string readback(intent.size(), '\0');
  if (verify.read(&readback[0], readback.size()) != static_cast<int>(readback.size()) || !verify.close() ||
      readback != intent)
    return false;
  // Prepare conservative copies before touching the book. Never roll back after
  // an unacknowledged FAT operation; retained candidates force explicit recovery.
  for (auto& item : items) {
    item.migration.active = true;
    item.migration.uncertain = true;
    if (!item.writer.writeToFile(nullptr, SIZE_MAX, &item.migration.sourcePath, true, &item.admission)) return false;
    item.migration.uncertain = false;
  }
  if (!Storage.rename(from.c_str(), to.c_str())) return false;
  for (auto& item : items)
    if (!commitRenameMigrationImpl(item.migration, true)) return false;
  return Storage.remove(MOVE_PENDING);
}
