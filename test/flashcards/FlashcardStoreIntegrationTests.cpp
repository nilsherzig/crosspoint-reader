#include <FlashcardStore.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "HalStorage.h"

namespace {

constexpr int64_t NOW = 1'700'000'000;
constexpr size_t SNAPSHOT_HEADER_BYTES = 52;
constexpr char CSV_PATH[] = "/flashcards/basic.csv";
constexpr char CSV[] = "front,back\nAlpha,One\nBeta,Two\nGamma,Three\n";

std::string deckFile(const uint64_t key, const char* suffix) {
  char path[96];
  std::snprintf(path, sizeof(path), "/.crosspoint/flashcards/%016llx.%s", static_cast<unsigned long long>(key), suffix);
  return path;
}

void expectSameQueue(const flashcards::StudyQueue& actual, const flashcards::StudyQueue& expected) {
  ASSERT_EQ(actual.cards.size(), expected.cards.size());
  EXPECT_EQ(actual.introducedToday, expected.introducedToday);
  EXPECT_EQ(actual.reviewCount, expected.reviewCount);
  EXPECT_EQ(actual.firstReviewDay, expected.firstReviewDay);
  EXPECT_EQ(actual.unseenCount, expected.unseenCount);
  EXPECT_EQ(actual.dueCards, expected.dueCards);
  EXPECT_EQ(actual.newCards, expected.newCards);
  for (size_t i = 0; i < actual.cards.size(); ++i) {
    const auto& a = actual.cards[i];
    const auto& b = expected.cards[i];
    EXPECT_EQ(a.fingerprint.first, b.fingerprint.first) << i;
    EXPECT_EQ(a.fingerprint.second, b.fingerprint.second) << i;
    EXPECT_EQ(a.introducedDay, b.introducedDay) << i;
    EXPECT_EQ(a.initialized, b.initialized) << i;
    EXPECT_EQ(a.phase, b.phase) << i;
    EXPECT_EQ(a.learningStep, b.learningStep) << i;
    EXPECT_EQ(a.lastReview, b.lastReview) << i;
    EXPECT_EQ(a.due, b.due) << i;
    EXPECT_FLOAT_EQ(a.memory.stability, b.memory.stability) << i;
    EXPECT_FLOAT_EQ(a.memory.difficulty, b.memory.difficulty) << i;
  }
}

class FlashcardStoreIntegrationTest : public ::testing::Test {
 protected:
  flashcards::DeckSummary deck;
  flashcards::Config config;

  void SetUp() override {
    fake::reset();
    fake::add(CSV_PATH, CSV);
    std::vector<flashcards::DeckSummary> decks;
    ASSERT_TRUE(flashcards::FlashcardStore::scanDecks(decks));
    ASSERT_EQ(decks.size(), 1U);
    deck = decks.front();
    ASSERT_TRUE(deck.valid());
    ASSERT_EQ(deck.cardCount, 3U);
  }

  std::string historyPath() const { return deckFile(deck.key, "history"); }
  std::string snapshotPath() const { return deckFile(deck.key, "state"); }
  std::string temporaryPath() const { return deckFile(deck.key, "state.tmp"); }

  bool load(flashcards::StudyQueue& queue, const int64_t now = NOW) {
    std::string error;
    const bool ok = flashcards::FlashcardStore::loadStudyQueue(deck, now, config, queue, error);
    if (!ok) ADD_FAILURE() << error;
    return ok;
  }

  bool introduce(flashcards::StudyQueue& queue, const size_t index = 0, const int64_t now = NOW) {
    std::string error;
    const bool ok = flashcards::FlashcardStore::introduceCard(deck, queue.cards[index], now, error);
    if (!ok) ADD_FAILURE() << error;
    if (ok) {
      flashcards::FlashcardStore::noteHistoryAppend(queue);
      ++queue.introducedToday;
    }
    return ok;
  }

  bool review(flashcards::StudyQueue& queue, const size_t index = 0, const int64_t now = NOW + 1) {
    std::string error;
    const bool ok =
        flashcards::FlashcardStore::reviewCard(deck, queue.cards[index], now, flashcards::Rating::Good, config, error);
    if (!ok) ADD_FAILURE() << error;
    if (ok) {
      flashcards::FlashcardStore::noteHistoryAppend(queue);
      ++queue.reviewCount;
      if (queue.firstReviewDay < 0) queue.firstReviewDay = static_cast<int32_t>(now / 86400);
    }
    return ok;
  }

  bool seedSnapshot() {
    flashcards::StudyQueue queue;
    if (!load(queue) || !introduce(queue) || !review(queue)) return false;
    if (!load(queue, NOW + 2)) return false;  // Rebuild the counters from the authoritative journal.
    return flashcards::FlashcardStore::saveStudySnapshot(deck, queue);
  }

  void expectMatchesFullReplay(const flashcards::StudyQueue& actual, const int64_t now = NOW + 2) {
    const auto path = snapshotPath();
    const auto saved = fake::files.find(path);
    const auto node = saved == fake::files.end() ? nullptr : saved->second;
    fake::files.erase(path);
    flashcards::StudyQueue replayed;
    if (load(replayed, now)) expectSameQueue(actual, replayed);
    if (node) fake::files[path] = node;
  }
};

TEST_F(FlashcardStoreIntegrationTest, ColdSnapshotRoundTripMatchesFullJournalReplay) {
  ASSERT_TRUE(seedSnapshot());
  EXPECT_EQ(fake::files.at(snapshotPath())->bytes.size(), SNAPSHOT_HEADER_BYTES + 3 * 48);
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  EXPECT_FALSE(restored.snapshotDirty);
  EXPECT_EQ(restored.reviewCount, 1U);
  EXPECT_EQ(restored.introducedToday, 1U);
  expectMatchesFullReplay(restored);
}

TEST_F(FlashcardStoreIntegrationTest, NewDeckSnapshotRoundTripWithoutHistory) {
  flashcards::StudyQueue initial;
  ASSERT_TRUE(load(initial));
  ASSERT_FALSE(fake::files.count(historyPath()));
  ASSERT_TRUE(flashcards::FlashcardStore::saveStudySnapshot(deck, initial));
  fake::resetIoCounters();
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored));
  EXPECT_FALSE(restored.snapshotDirty);
  EXPECT_EQ(restored.unseenCount, 3U);
  EXPECT_EQ(fake::bytesReadByPath[historyPath()], 0U);
  expectMatchesFullReplay(restored, NOW);
}

TEST_F(FlashcardStoreIntegrationTest, WarmOpenReadsOnlyJournalBoundary) {
  ASSERT_TRUE(seedSnapshot());
  fake::resetIoCounters();
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  EXPECT_LE(fake::bytesReadByPath[historyPath()], 60U);
}

TEST_F(FlashcardStoreIntegrationTest, AppendedReviewIsReplayedAfterSnapshot) {
  ASSERT_TRUE(seedSnapshot());
  flashcards::StudyQueue active;
  ASSERT_TRUE(load(active, NOW + 2));
  ASSERT_TRUE(review(active, 0, NOW + 3));
  flashcards::StudyQueue restored;
  fake::resetIoCounters();
  ASSERT_TRUE(load(restored, NOW + 4));
  EXPECT_TRUE(restored.snapshotDirty);
  EXPECT_EQ(restored.reviewCount, 2U);
  EXPECT_LE(fake::bytesReadByPath[historyPath()], 240U);
  expectMatchesFullReplay(restored, NOW + 4);
}

TEST_F(FlashcardStoreIntegrationTest, UndoAfterSnapshotRestoresPreviousSchedulingState) {
  ASSERT_TRUE(seedSnapshot());
  flashcards::StudyQueue active;
  ASSERT_TRUE(load(active, NOW + 2));
  const auto before = active.cards[0];
  ASSERT_TRUE(review(active, 0, NOW + 3));
  std::string error;
  ASSERT_TRUE(flashcards::FlashcardStore::undoReview(deck, active.cards[0], before, error)) << error;
  flashcards::FlashcardStore::noteHistoryAppend(active);
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 4));
  EXPECT_EQ(restored.reviewCount, 1U);
  expectMatchesFullReplay(restored, NOW + 4);
}

TEST_F(FlashcardStoreIntegrationTest, SnapshotRespectsNextUtcDayQuotaReset) {
  ASSERT_TRUE(seedSnapshot());
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 86400));
  EXPECT_EQ(restored.introducedToday, 0U);
  expectMatchesFullReplay(restored, NOW + 86400);
}

TEST_F(FlashcardStoreIntegrationTest, RecordCorruptionFallsBackToJournal) {
  ASSERT_TRUE(seedSnapshot());
  fake::files.at(snapshotPath())->bytes[SNAPSHOT_HEADER_BYTES + 20] ^= 0x40;
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  EXPECT_TRUE(restored.snapshotDirty);
  expectMatchesFullReplay(restored);
}

TEST_F(FlashcardStoreIntegrationTest, TruncatedSnapshotFallsBackToJournal) {
  ASSERT_TRUE(seedSnapshot());
  fake::files.at(snapshotPath())->bytes.resize(47);
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  EXPECT_TRUE(restored.snapshotDirty);
  expectMatchesFullReplay(restored);
}

TEST_F(FlashcardStoreIntegrationTest, UnsupportedSnapshotVersionFallsBackToJournal) {
  ASSERT_TRUE(seedSnapshot());
  fake::files.at(snapshotPath())->bytes[4] = 99;
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  EXPECT_TRUE(restored.snapshotDirty);
  expectMatchesFullReplay(restored);
}

TEST_F(FlashcardStoreIntegrationTest, LegacyVersionOneSnapshotIsRebuiltFromJournal) {
  ASSERT_TRUE(seedSnapshot());
  auto& bytes = fake::files.at(snapshotPath())->bytes;
  bytes.erase(bytes.begin() + 48, bytes.begin() + SNAPSHOT_HEADER_BYTES);
  bytes[4] = 1;
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  EXPECT_TRUE(restored.snapshotDirty);
  expectMatchesFullReplay(restored);
}

TEST_F(FlashcardStoreIntegrationTest, CorruptSnapshotCountersFallBackToJournal) {
  ASSERT_TRUE(seedSnapshot());
  fake::files.at(snapshotPath())->bytes[36] ^= 0x40;
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  EXPECT_TRUE(restored.snapshotDirty);
  expectMatchesFullReplay(restored);
}

TEST_F(FlashcardStoreIntegrationTest, ChangedCsvInvalidatesSnapshotButKeepsMatchingCardHistory) {
  ASSERT_TRUE(seedSnapshot());
  fake::add(CSV_PATH, "front,back\nAlpha,One\nBeta,Two\nGamma,Three\nDelta,Four\n");
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  EXPECT_EQ(restored.cards.size(), 4U);
  EXPECT_TRUE(restored.snapshotDirty);
  expectMatchesFullReplay(restored);
}

TEST_F(FlashcardStoreIntegrationTest, TruncatedJournalInvalidatesSnapshot) {
  ASSERT_TRUE(seedSnapshot());
  fake::files.at(historyPath())->bytes.resize(60);
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  EXPECT_TRUE(restored.snapshotDirty);
  EXPECT_EQ(restored.reviewCount, 0U);
  expectMatchesFullReplay(restored);
}

TEST_F(FlashcardStoreIntegrationTest, DamagedJournalTailIsRepairedAfterSnapshot) {
  ASSERT_TRUE(seedSnapshot());
  auto& bytes = fake::files.at(historyPath())->bytes;
  const auto originalSize = bytes.size();
  bytes.push_back(0xFF);
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  EXPECT_TRUE(restored.snapshotDirty);
  EXPECT_EQ(bytes.size(), originalSize);
  expectMatchesFullReplay(restored);
}

TEST_F(FlashcardStoreIntegrationTest, FailedSnapshotWriteLeavesJournalAuthoritative) {
  ASSERT_TRUE(seedSnapshot());
  flashcards::StudyQueue active;
  ASSERT_TRUE(load(active, NOW + 2));
  ASSERT_TRUE(review(active, 0, NOW + 3));
  fake::failWritePath = temporaryPath();
  EXPECT_FALSE(flashcards::FlashcardStore::saveStudySnapshot(deck, active));
  EXPECT_TRUE(active.snapshotDirty);
  EXPECT_FALSE(fake::files.count(temporaryPath()));
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 4));
  expectMatchesFullReplay(restored, NOW + 4);
}

TEST_F(FlashcardStoreIntegrationTest, FailedSnapshotRecordWriteLeavesJournalAuthoritative) {
  ASSERT_TRUE(seedSnapshot());
  flashcards::StudyQueue active;
  ASSERT_TRUE(load(active, NOW + 2));
  ASSERT_TRUE(review(active, 0, NOW + 3));
  fake::failWrite = 1;  // Header succeeds; first card record fails.
  EXPECT_FALSE(flashcards::FlashcardStore::saveStudySnapshot(deck, active));
  EXPECT_TRUE(active.snapshotDirty);
  EXPECT_FALSE(fake::files.count(temporaryPath()));
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 4));
  expectMatchesFullReplay(restored, NOW + 4);
}

TEST_F(FlashcardStoreIntegrationTest, FailedSnapshotHeaderRewriteLeavesJournalAuthoritative) {
  ASSERT_TRUE(seedSnapshot());
  flashcards::StudyQueue active;
  ASSERT_TRUE(load(active, NOW + 2));
  ASSERT_TRUE(review(active, 0, NOW + 3));
  fake::failWrite = static_cast<int>(active.cards.size()) + 1;
  EXPECT_FALSE(flashcards::FlashcardStore::saveStudySnapshot(deck, active));
  EXPECT_TRUE(active.snapshotDirty);
  EXPECT_FALSE(fake::files.count(temporaryPath()));
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 4));
  expectMatchesFullReplay(restored, NOW + 4);
}

TEST_F(FlashcardStoreIntegrationTest, FailedSnapshotRenameLeavesJournalAuthoritative) {
  ASSERT_TRUE(seedSnapshot());
  flashcards::StudyQueue active;
  ASSERT_TRUE(load(active, NOW + 2));
  ASSERT_TRUE(review(active, 0, NOW + 3));
  fake::failRename = 0;
  EXPECT_FALSE(flashcards::FlashcardStore::saveStudySnapshot(deck, active));
  EXPECT_TRUE(active.snapshotDirty);
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 4));
  EXPECT_TRUE(restored.snapshotDirty);
  expectMatchesFullReplay(restored, NOW + 4);
}

TEST_F(FlashcardStoreIntegrationTest, CleanQueueDoesNotRewriteSnapshot) {
  ASSERT_TRUE(seedSnapshot());
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  fake::writesByPath.clear();
  ASSERT_TRUE(flashcards::FlashcardStore::saveStudySnapshot(deck, restored));
  EXPECT_EQ(fake::writesByPath[temporaryPath()], 0U);
}

TEST_F(FlashcardStoreIntegrationTest, FastDeckScanDoesNotImportOrReadJournal) {
  fake::add("/flashcards/new.csv", "front,back\nFresh,Card\n");
  fake::resetIoCounters();
  std::vector<flashcards::DeckSummary> decks;
  ASSERT_TRUE(flashcards::FlashcardStore::scanDecks(decks, false));
  ASSERT_EQ(decks.size(), 2U);
  EXPECT_EQ(fake::bytesReadByPath[historyPath()], 0U);
  const auto fresh = std::find_if(decks.begin(), decks.end(), [](const auto& item) { return item.name == "new"; });
  ASSERT_NE(fresh, decks.end());
  EXPECT_FALSE(fresh->cardCountAvailable);
  EXPECT_FALSE(fake::files.count(deckFile(fresh->key, "cards")));
}

TEST_F(FlashcardStoreIntegrationTest, OverviewScanCreatesMissingSnapshotsBeforeFirstDisplay) {
  ASSERT_TRUE(seedSnapshot());
  fake::add("/flashcards/new.csv", "front,back\nFresh,Card\n");
  std::vector<flashcards::DeckSummary> decks;
  ASSERT_TRUE(flashcards::FlashcardStore::scanDecks(decks, false));
  ASSERT_EQ(decks.size(), 2U);
  const auto fresh = std::find_if(decks.begin(), decks.end(), [](const auto& item) { return item.name == "new"; });
  ASSERT_NE(fresh, decks.end());
  EXPECT_FALSE(fresh->cardCountAvailable);
  EXPECT_FALSE(fake::files.count(deckFile(fresh->key, "state")));

  flashcards::StudyQueue queue;
  std::string error;
  ASSERT_TRUE(flashcards::FlashcardStore::loadStudyQueue(*fresh, NOW + 2, config, queue, error)) << error;
  EXPECT_EQ(queue.cards.size(), 1U);
  EXPECT_EQ(queue.dueCards.size(), 0U);
  EXPECT_EQ(queue.newCards.size(), 1U);
  EXPECT_TRUE(queue.snapshotDirty);
  ASSERT_TRUE(flashcards::FlashcardStore::saveStudySnapshot(*fresh, queue));
  EXPECT_TRUE(fake::files.count(deckFile(fresh->key, "state")));

  ASSERT_TRUE(flashcards::FlashcardStore::loadStudyQueue(deck, NOW + 2, config, queue, error)) << error;
  EXPECT_EQ(queue.cards.size(), 3U);
  EXPECT_EQ(queue.dueCards.size(), 1U);
  EXPECT_EQ(queue.newCards.size(), 2U);
  EXPECT_FALSE(queue.snapshotDirty);
  ASSERT_TRUE(flashcards::FlashcardStore::saveStudySnapshot(deck, queue));

  fake::resetIoCounters();
  ASSERT_TRUE(flashcards::FlashcardStore::loadStudyQueue(*fresh, NOW + 2, config, queue, error)) << error;
  EXPECT_FALSE(queue.snapshotDirty);
  EXPECT_EQ(queue.newCards.size(), 1U);
  EXPECT_EQ(fake::bytesReadByPath[deckFile(fresh->key, "history")], 0U);
}

TEST_F(FlashcardStoreIntegrationTest, OverviewScanRefreshesOutdatedSnapshotFromJournalTail) {
  ASSERT_TRUE(seedSnapshot());
  flashcards::StudyQueue queue;
  ASSERT_TRUE(load(queue, NOW + 2));
  ASSERT_TRUE(review(queue, 0, NOW + 3));
  std::string error;
  ASSERT_TRUE(flashcards::FlashcardStore::loadStudyQueue(deck, NOW + 4, config, queue, error)) << error;
  EXPECT_TRUE(queue.snapshotDirty);
  EXPECT_EQ(queue.reviewCount, 2U);
  ASSERT_TRUE(flashcards::FlashcardStore::saveStudySnapshot(deck, queue));

  fake::resetIoCounters();
  ASSERT_TRUE(flashcards::FlashcardStore::loadStudyQueue(deck, NOW + 4, config, queue, error)) << error;
  EXPECT_FALSE(queue.snapshotDirty);
  EXPECT_EQ(queue.reviewCount, 2U);
  EXPECT_LE(fake::bytesReadByPath[historyPath()], 60U);
}

TEST_F(FlashcardStoreIntegrationTest, OverviewScanRepairsInvalidSnapshot) {
  ASSERT_TRUE(seedSnapshot());
  fake::files.at(snapshotPath())->bytes[SNAPSHOT_HEADER_BYTES + 20] ^= 0x40;
  flashcards::StudyQueue queue;
  ASSERT_TRUE(load(queue, NOW + 2));
  EXPECT_TRUE(queue.snapshotDirty);
  EXPECT_EQ(queue.reviewCount, 1U);
  ASSERT_TRUE(flashcards::FlashcardStore::saveStudySnapshot(deck, queue));

  fake::resetIoCounters();
  ASSERT_TRUE(load(queue, NOW + 2));
  EXPECT_FALSE(queue.snapshotDirty);
  EXPECT_LE(fake::bytesReadByPath[historyPath()], 60U);
}

TEST_F(FlashcardStoreIntegrationTest, FailedDeckCountDoesNotPreventOtherDeckCounts) {
  fake::add("/flashcards/new.csv", "front,back\nFresh,Card\n");
  std::vector<flashcards::DeckSummary> decks;
  ASSERT_TRUE(flashcards::FlashcardStore::scanDecks(decks, false));
  ASSERT_EQ(decks.size(), 2U);
  fake::files.erase(CSV_PATH);

  flashcards::StudyQueue queue;
  std::string error;
  EXPECT_FALSE(flashcards::FlashcardStore::loadStudyQueue(deck, NOW, config, queue, error));
  EXPECT_FALSE(error.empty());
  const auto fresh = std::find_if(decks.begin(), decks.end(), [](const auto& item) { return item.name == "new"; });
  ASSERT_NE(fresh, decks.end());
  ASSERT_TRUE(flashcards::FlashcardStore::loadStudyQueue(*fresh, NOW, config, queue, error)) << error;
  EXPECT_EQ(queue.newCards.size(), 1U);
}

TEST_F(FlashcardStoreIntegrationTest, EverySingleByteSnapshotErrorFallsBackToJournal) {
  ASSERT_TRUE(seedSnapshot());
  const auto path = snapshotPath();
  const auto original = fake::files.at(path)->bytes;
  for (size_t offset = 0; offset < original.size(); ++offset) {
    auto& bytes = fake::files.at(path)->bytes;
    bytes = original;
    bytes[offset] ^= 1;
    flashcards::StudyQueue restored;
    ASSERT_TRUE(load(restored, NOW + 2)) << offset;
    EXPECT_TRUE(restored.snapshotDirty) << offset;
    EXPECT_EQ(restored.reviewCount, 1U) << offset;
    EXPECT_EQ(restored.introducedToday, 1U) << offset;
  }
}

TEST_F(FlashcardStoreIntegrationTest, LongHistoryDoesNotSlowWarmOpen) {
  ASSERT_TRUE(seedSnapshot());
  flashcards::StudyQueue active;
  ASSERT_TRUE(load(active, NOW + 2));
  for (int i = 0; i < 500; ++i) ASSERT_TRUE(review(active, 0, NOW + 3 + i));
  flashcards::StudyQueue replayed;
  ASSERT_TRUE(load(replayed, NOW + 504));
  ASSERT_EQ(replayed.reviewCount, 501U);
  ASSERT_TRUE(flashcards::FlashcardStore::saveStudySnapshot(deck, replayed));

  fake::resetIoCounters();
  flashcards::StudyQueue warm;
  ASSERT_TRUE(load(warm, NOW + 504));
  EXPECT_FALSE(warm.snapshotDirty);
  EXPECT_EQ(warm.reviewCount, 501U);
  EXPECT_LE(fake::bytesReadByPath[historyPath()], 60U);
  expectMatchesFullReplay(warm, NOW + 504);
}

TEST_F(FlashcardStoreIntegrationTest, ValidSameLengthJournalReplacementWithDifferentTailInvalidatesSnapshot) {
  ASSERT_TRUE(seedSnapshot());
  flashcards::StudyQueue active;
  ASSERT_TRUE(load(active, NOW + 2));
  ASSERT_TRUE(review(active, 0, NOW + 3));
  auto& bytes = fake::files.at(historyPath())->bytes;
  const std::vector<uint8_t> replacement(bytes.end() - 60, bytes.end());
  bytes.resize(60);
  bytes.insert(bytes.end(), replacement.begin(), replacement.end());

  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 4));
  EXPECT_TRUE(restored.snapshotDirty);
  expectMatchesFullReplay(restored, NOW + 4);
}

TEST_F(FlashcardStoreIntegrationTest, MissingHistoryInvalidatesSnapshot) {
  ASSERT_TRUE(seedSnapshot());
  fake::files.erase(historyPath());
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 2));
  EXPECT_TRUE(restored.snapshotDirty);
  EXPECT_EQ(restored.reviewCount, 0U);
  EXPECT_EQ(restored.unseenCount, 3U);
  expectMatchesFullReplay(restored);
}

TEST_F(FlashcardStoreIntegrationTest, RemovedCardsRemainInReviewTotalsAfterReimportAndSnapshot) {
  ASSERT_TRUE(seedSnapshot());
  fake::add(CSV_PATH, "front,back\nBeta,Two\nGamma,Three\n");
  flashcards::StudyQueue active;
  ASSERT_TRUE(load(active, NOW + 2));
  ASSERT_EQ(active.cards.size(), 2U);
  ASSERT_EQ(active.reviewCount, 1U);
  ASSERT_TRUE(flashcards::FlashcardStore::saveStudySnapshot(deck, active));
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored, NOW + 3));
  EXPECT_FALSE(restored.snapshotDirty);
  EXPECT_EQ(restored.reviewCount, 1U);
  expectMatchesFullReplay(restored, NOW + 3);
}

TEST_F(FlashcardStoreIntegrationTest, MaximumSizeDeckSnapshotRestoresAllCards) {
  std::string csv = "front,back\n";
  for (int i = 0; i < flashcards::FlashcardStore::MAX_CARDS_PER_DECK; ++i) {
    csv += "Question " + std::to_string(i) + ",Answer " + std::to_string(i) + "\n";
  }
  fake::add(CSV_PATH, csv);
  flashcards::StudyQueue active;
  ASSERT_TRUE(load(active));
  ASSERT_EQ(active.cards.size(), flashcards::FlashcardStore::MAX_CARDS_PER_DECK);
  ASSERT_TRUE(flashcards::FlashcardStore::saveStudySnapshot(deck, active));
  fake::resetIoCounters();
  flashcards::StudyQueue restored;
  ASSERT_TRUE(load(restored));
  EXPECT_FALSE(restored.snapshotDirty);
  EXPECT_EQ(restored.cards.size(), flashcards::FlashcardStore::MAX_CARDS_PER_DECK);
  EXPECT_EQ(restored.newCards.size(), config.newCardsPerDay);
  EXPECT_EQ(fake::bytesReadByPath[historyPath()], 0U);
  expectMatchesFullReplay(restored, NOW);
}

}  // namespace
