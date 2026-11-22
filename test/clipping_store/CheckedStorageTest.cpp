#include <ClippingStore.h>
#include <HalStorage.h>
#include <Serialization.h>
#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <type_traits>

static_assert(std::is_constructible_v<CheckedClippingId, const char*>,
              "Clipping IDs must remain directly constructible from C strings");
static_assert(!std::is_convertible_v<const char*, CheckedClippingId>,
              "Clipping IDs must reject unintended implicit C-string conversions");

TEST(CheckedClippingId, DirectConstructionPreservesEmptyExactAndTruncatedIdsWithoutAllocation) {
  fake::reset();
  const auto attempts = fake::checkedAllocationAttempts;
  const CheckedClippingId empty("");
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.size(), 0u);
  EXPECT_STREQ(empty.c_str(), "");
  const std::string exactText(64, 'x');
  const CheckedClippingId exact(exactText.c_str());
  EXPECT_EQ(exact.size(), 64u);
  EXPECT_STREQ(exact.c_str(), exactText.c_str());
  EXPECT_EQ(exact.data()[64], '\0');
  const std::string longerText = exactText + "suffix";
  const CheckedClippingId truncated(longerText.c_str());
  EXPECT_EQ(truncated.size(), 64u);
  EXPECT_STREQ(truncated.c_str(), exactText.c_str());
  EXPECT_EQ(truncated.data()[64], '\0');
  EXPECT_EQ(truncated, exact);
  EXPECT_EQ(fake::checkedAllocationAttempts, attempts);
}

namespace {
std::string snapshotPath() {
  for (const auto& [p, n] : fake::files)
    if (p.ends_with(".bin")) return p;
  return "";
}
void seed(ClippingStore& s) {
  ASSERT_TRUE(s.loadForBook("/book.epub", "Book", "Author", "epub"));
  for (unsigned i = 0; i < 17; ++i) {
    Clipping c;
    snprintf(c.id, sizeof(c.id), "remote-%u", i);
    ASSERT_TRUE(s.applyRemote(c, "text-" + std::to_string(i), false));
  }
  ASSERT_EQ(s.addClipping(0, 0, 0, 1, 0, 0, 1, "chapter", 0, "local", uint32_t{1}), ClippingStore::AddResult::Added);
}
using Operation = std::function<bool(ClippingStore&)>;
const std::array<Operation, 9> operations = {[](ClippingStore& s) {
                                               return s.addClipping(0, 0, 0, 1, 0, 0, 1, "", 0, "new", uint32_t{1}) ==
                                                      ClippingStore::AddResult::Added;
                                             },
                                             [](ClippingStore& s) {
                                               return s.addClipping(0, 0, 0, 1, 0, 0, 1, "", 0, "new", uint16_t{2},
                                                                    uint32_t{1}) == ClippingStore::AddResult::Added;
                                             },
                                             [](ClippingStore& s) { return s.removeClippingAt(15); },
                                             [](ClippingStore& s) { return s.prepareSync(); },
                                             [](ClippingStore& s) { return s.finishUploads(); },
                                             [](ClippingStore& s) {
                                               s.clearAll();
                                               return s.clippingCount() == 0;
                                             },
                                             [](ClippingStore& s) {
                                               auto c = *s.clippingAt(1);
                                               ++c.syncRevision;
                                               return s.applyRemote(c, "replaced", false);
                                             },
                                             [](ClippingStore& s) {
                                               auto c = *s.clippingAt(1);
                                               return s.applyRemote(c, "", true);
                                             },
                                             [](ClippingStore& s) {
                                               Clipping c;
                                               strcpy(c.id, "new-remote");
                                               return s.applyRemote(c, "new", false);
                                             }};
}  // namespace

TEST(CheckedClippingStorage, ActualNothrowFamiliesAreObservedAndReleased) {
  fake::reset();
  const auto live = fake::liveCheckedAllocations;
  const auto attempts = fake::checkedAllocationAttempts;
  auto* scalar = new (std::nothrow) int(3);
  auto* array = new (std::nothrow) int[3];
  ASSERT_NE(scalar, nullptr);
  ASSERT_NE(array, nullptr);
  EXPECT_EQ(fake::checkedAllocationAttempts, attempts + 2);
  EXPECT_EQ(fake::liveCheckedAllocations, live + 2);
  delete scalar;
  delete[] array;
  EXPECT_EQ(fake::liveCheckedAllocations, live);
  fake::failAlloc = 0;
  auto* denied = new (std::nothrow) int;
  EXPECT_EQ(denied, nullptr);
  EXPECT_TRUE(fake::failureTriggered);
}

TEST(CheckedClippingStorage, EveryMutationAdmissionFaultFiresBeforeSnapshotWritesAndCanRetry) {
  for (size_t op = 0; op < operations.size(); ++op) {
    SCOPED_TRACE(op);
    size_t allocations;
    {
      fake::reset();
      ClippingStore s;
      seed(s);
      const auto before = fake::checkedAllocationAttempts;
      ASSERT_TRUE(operations[op](s));
      allocations = fake::checkedAllocationAttempts - before;
      ASSERT_GT(allocations, 0u);
      s.unload();
      EXPECT_EQ(fake::liveCheckedAllocations, 0u);
    }
    for (size_t fault = 0; fault < allocations; ++fault) {
      SCOPED_TRACE(fault);
      fake::reset();
      ClippingStore s;
      seed(s);
      const auto path = snapshotPath();
      const auto old = fake::files.at(path)->bytes;
      const auto writes = fake::writesByPath;
      std::array<Clipping, 18> rows;
      for (size_t i = 0; i < rows.size(); ++i) rows[i] = *s.clippingAt(i);
      fake::failAlloc = fault;
      EXPECT_FALSE(operations[op](s));
      ASSERT_TRUE(fake::failureTriggered);
      ASSERT_EQ(s.clippingCount(), 18u);
      EXPECT_EQ(fake::files.at(path)->bytes, old);
      EXPECT_EQ(fake::writesByPath, writes);
      EXPECT_FALSE(Storage.exists((path + ".tmp").c_str()));
      for (size_t i = 0; i < 18; ++i) {
        const auto* row = s.clippingAt(i);
        ASSERT_NE(row, nullptr);
        EXPECT_STREQ(row->id, rows[i].id);
        EXPECT_STREQ(row->chapterTitle, rows[i].chapterTitle);
        EXPECT_EQ(row->pendingUpload, rows[i].pendingUpload);
        EXPECT_EQ(row->syncRevision, rows[i].syncRevision);
        EXPECT_EQ(row->tableSelection, rows[i].tableSelection);
        EXPECT_EQ(row->startOffset, rows[i].startOffset);
        EXPECT_EQ(row->endOffset, rows[i].endOffset);
        EXPECT_EQ(row->textOffset, rows[i].textOffset);
        EXPECT_EQ(row->textLength, rows[i].textLength);
        std::string text;
        ASSERT_TRUE(s.readClippingText(i, text));
        EXPECT_EQ(text, i == 17 ? "local" : "text-" + std::to_string(i));
      }
      ASSERT_TRUE(operations[op](s));
      s.unload();
      EXPECT_EQ(fake::liveCheckedAllocations, 0u);
    }
  }
}

TEST(CheckedClippingStorage, PartialLoadFaultFiresAndReleasesEveryOwnedBlock) {
  fake::reset();
  ClippingStore s;
  seed(s);
  s.unload();
  const auto path = snapshotPath();
  const auto bytes = fake::files.at(path)->bytes;
  const auto before = fake::checkedAllocationAttempts;
  ASSERT_TRUE(s.loadForBook("/book.epub", "", "", "epub"));
  const auto allocations = fake::checkedAllocationAttempts - before;
  s.unload();
  for (size_t fault = 0; fault < allocations; ++fault) {
    SCOPED_TRACE(fault);
    fake::failureTriggered = false;
    fake::failAlloc = fault;
    EXPECT_FALSE(s.loadForBook("/book.epub", "", "", "epub"));
    ASSERT_TRUE(fake::failureTriggered);
    EXPECT_EQ(s.clippingCount(), 0u);
    EXPECT_EQ(fake::liveCheckedAllocations, 0u);
    EXPECT_FALSE(s.prepareSync());
    EXPECT_FALSE(s.saveToFile());
    EXPECT_EQ(fake::files.at(path)->bytes, bytes);
    ASSERT_TRUE(s.loadForBook("/book.epub", "", "", "epub"));
    EXPECT_EQ(s.clippingCount(), 18u);
    s.unload();
    EXPECT_EQ(fake::liveCheckedAllocations, 0u);
  }
}

TEST(CheckedClippingStorage, MoveAdmissionFaultsPrecedeTheGlobalIntentAndAnySnapshotWrite) {
  size_t allocations = 0;
  for (int fault = -1; fault < static_cast<int>(allocations); ++fault) {
    SCOPED_TRACE(fault);
    fake::reset();
    fake::add("/book.epub");
    {
      ClippingStore s;
      seed(s);
      s.unload();
    }
    const auto path = snapshotPath();
    const auto bytes = fake::files.at(path)->bytes;
    const auto writes = fake::writesByPath;
    const auto before = fake::checkedAllocationAttempts;
    fake::failureTriggered = false;
    fake::failAlloc = fault;
    const bool result = ClippingStore::moveBook("/book.epub", "/moved.epub");
    if (fault < 0) {
      ASSERT_TRUE(result);
      allocations = fake::checkedAllocationAttempts - before;
      ASSERT_GT(allocations, 0u);
    } else {
      EXPECT_FALSE(result);
      ASSERT_TRUE(fake::failureTriggered);
      EXPECT_TRUE(Storage.exists("/book.epub"));
      EXPECT_FALSE(Storage.exists("/moved.epub"));
      EXPECT_FALSE(Storage.exists("/.crosspoint/clippings/move.pending"));
      EXPECT_EQ(fake::files.at(path)->bytes, bytes);
      EXPECT_EQ(fake::writesByPath, writes);
      ASSERT_TRUE(ClippingStore::moveBook("/book.epub", "/moved.epub"));
    }
    EXPECT_EQ(fake::liveCheckedAllocations, 0u);
  }
}

TEST(CheckedClippingStorage, DeletionGrowthAndAcknowledgementAdmissionFaultsPreserveQueue) {
  for (bool acknowledge : {false, true}) {
    size_t allocations = 0;
    for (int fault = -1; fault < static_cast<int>(allocations); ++fault) {
      SCOPED_TRACE(acknowledge);
      SCOPED_TRACE(fault);
      fake::reset();
      ClippingStore s;
      seed(s);
      for (int i = 0; i < 16; ++i) ASSERT_TRUE(s.removeClippingAt(0));
      const auto path = snapshotPath();
      const auto bytes = fake::files.at(path)->bytes;
      const auto writes = fake::writesByPath;
      const auto before = fake::checkedAllocationAttempts;
      fake::failureTriggered = false;
      fake::failAlloc = fault;
      const bool result = acknowledge ? s.finishDeletions() : s.removeClippingAt(0);
      if (fault < 0) {
        ASSERT_TRUE(result);
        allocations = fake::checkedAllocationAttempts - before;
        ASSERT_GT(allocations, 0u);
      } else {
        EXPECT_FALSE(result);
        ASSERT_TRUE(fake::failureTriggered);
        EXPECT_EQ(s.clippingCount(), 2u);
        EXPECT_EQ(fake::files.at(path)->bytes, bytes);
        EXPECT_EQ(fake::writesByPath, writes);
        uint32_t offset = 0;
        char id[65];
        for (unsigned i = 0; i < 16; ++i) {
          ASSERT_TRUE(s.nextDeletion(offset, id));
          EXPECT_EQ(id, "remote-" + std::to_string(i));
        }
        ASSERT_TRUE(s.nextDeletion(offset, id));
        EXPECT_EQ(id[0], 0);
        ASSERT_TRUE(acknowledge ? s.finishDeletions() : s.removeClippingAt(0));
      }
      s.unload();
      EXPECT_EQ(fake::liveCheckedAllocations, 0u);
    }
  }
}

TEST(CheckedClippingStorage, LegacyQueueBeyondWireLimitIsRetainedReadOnlyForExternalRecovery) {
  fake::reset();
  ClippingStore s;
  seed(s);
  s.unload();
  const auto path = snapshotPath();
  // A legacy v4 with one valid row suffices: the oversized journal must never
  // be truncated or acknowledged, regardless of its complete-prefix contents.
  HalFile f = Storage.open(path.c_str(), O_WRONLY | O_TRUNC);
  ASSERT_TRUE(serialization::tryWritePod(f, uint8_t{4}));
  ASSERT_TRUE(serialization::tryWritePod(f, uint16_t{0}));
  for (const char* text : {"Book", "Author", "/book.epub"}) ASSERT_TRUE(serialization::tryWriteString(f, text));
  ASSERT_TRUE(f.close());
  std::string entry(65, '\0');
  entry.replace(0, 7, "deleted");
  std::string raw;
  raw.reserve(65536u * 65u);
  for (unsigned i = 0; i < 65536; ++i) raw += entry;
  fake::add(path + ".deleted", raw);
  const auto primary = fake::files.at(path)->bytes;
  EXPECT_FALSE(s.loadForBook("/book.epub", "", "", "epub"));
  EXPECT_EQ(s.clippingCount(), 0u);
  EXPECT_EQ(fake::liveCheckedAllocations, 0u);
  EXPECT_FALSE(s.prepareSync());
  EXPECT_FALSE(s.saveToFile());
  EXPECT_FALSE(s.finishDeletions());
  s.unload();
  EXPECT_EQ(fake::files.at(path)->bytes, primary);
  EXPECT_EQ(fake::files.at(path + ".deleted")->bytes, std::vector<uint8_t>(raw.begin(), raw.end()));
}

TEST(CheckedClippingStorage, LegacyQueueBeyondLiveCapacityIsPreservedThroughV5AndAcknowledgement) {
  fake::reset();
  ClippingStore s;
  seed(s);
  s.unload();
  const auto path = snapshotPath();
  HalFile f = Storage.open(path.c_str(), O_WRONLY | O_TRUNC);
  ASSERT_TRUE(serialization::tryWritePod(f, uint8_t{4}));
  ASSERT_TRUE(serialization::tryWritePod(f, uint16_t{0}));
  for (const char* text : {"Book", "Author", "/book.epub"}) ASSERT_TRUE(serialization::tryWriteString(f, text));
  ASSERT_TRUE(f.close());
  std::string raw;
  for (unsigned i = 0; i < 1025; ++i) {
    char id[65]{};
    snprintf(id, sizeof(id), "deleted-%u", i);
    raw.append(id, sizeof(id));
  }
  raw += "interrupted";
  fake::add(path + ".deleted", raw);
  const auto legacy = fake::files.at(path)->bytes;
  const auto sweepLoad = [&]() {
    size_t allocations;
    {
      ClippingStore probe;
      const auto before = fake::checkedAllocationAttempts;
      EXPECT_TRUE(probe.loadForBook("/book.epub", "", "", "epub"));
      allocations = fake::checkedAllocationAttempts - before;
    }
    EXPECT_EQ(fake::liveCheckedAllocations, 0u);
    const auto original = fake::files.at(path)->bytes;
    for (size_t fault = 0; fault < allocations; ++fault) {
      SCOPED_TRACE(fault);
      fake::failureTriggered = false;
      fake::failAlloc = fault;
      EXPECT_FALSE(s.loadForBook("/book.epub", "", "", "epub"));
      ASSERT_TRUE(fake::failureTriggered);
      EXPECT_EQ(s.clippingCount(), 0u);
      EXPECT_EQ(fake::liveCheckedAllocations, 0u);
      EXPECT_FALSE(s.finishDeletions());
      EXPECT_FALSE(s.saveToFile());
      EXPECT_EQ(fake::files.at(path)->bytes, original);
      EXPECT_EQ(fake::files.at(path + ".deleted")->bytes, std::vector<uint8_t>(raw.begin(), raw.end()));
    }
  };
  sweepLoad();
  ASSERT_TRUE(s.loadForBook("/book.epub", "", "", "epub"));
  EXPECT_EQ(fake::files.at(path)->bytes, legacy);
  ASSERT_TRUE(s.prepareSync());
  s.unload();
  sweepLoad();
  ASSERT_TRUE(s.loadForBook("/book.epub", "", "", "epub"));
  uint32_t offset = 0;
  char id[65];
  for (unsigned i = 0; i < 1025; ++i) {
    ASSERT_TRUE(s.nextDeletion(offset, id));
    EXPECT_EQ(id, "deleted-" + std::to_string(i));
  }
  ASSERT_TRUE(s.nextDeletion(offset, id));
  EXPECT_EQ(id[0], 0);
  EXPECT_EQ(fake::files.at(path + ".deleted")->bytes, std::vector<uint8_t>(raw.begin(), raw.end()));
  ASSERT_TRUE(s.finishDeletions());
  EXPECT_FALSE(Storage.exists((path + ".deleted").c_str()));
  s.unload();
  EXPECT_EQ(fake::liveCheckedAllocations, 0u);
}
