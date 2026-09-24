#include <SnapshotIntegrity.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace {

void sealHistoryRecord(std::array<uint8_t, 60>& record) {
  const uint32_t checksum = flashcards::detail::crc32(record.data(), 56);
  for (size_t i = 0; i < 4; ++i) record[56 + i] = static_cast<uint8_t>(checksum >> (8 * i));
}

TEST(SnapshotIntegrity, CrcMatchesStandardVectorAndIncrementalUpdates) {
  constexpr std::array<uint8_t, 9> bytes{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  EXPECT_EQ(flashcards::detail::crc32(bytes.data(), bytes.size()), 0xCBF43926U);
  const uint32_t partial = flashcards::detail::updateCrc(0xFFFFFFFFU, bytes.data(), 4);
  EXPECT_EQ(flashcards::detail::updateCrc(partial, bytes.data() + 4, 5) ^ 0xFFFFFFFFU, 0xCBF43926U);
}

TEST(SnapshotIntegrity, ValidJournalRecordsHaveDistinctBoundaryFingerprints) {
  std::array<uint8_t, 60> first{};
  std::array<uint8_t, 60> second{};
  first[12] = 1;
  second[12] = 2;
  sealHistoryRecord(first);
  sealHistoryRecord(second);

  // Including the stored CRC produces the same residue for both records.
  EXPECT_EQ(flashcards::detail::crc32(first.data(), first.size()),
            flashcards::detail::crc32(second.data(), second.size()));
  EXPECT_NE(flashcards::detail::historyBoundaryFingerprint(first.data()),
            flashcards::detail::historyBoundaryFingerprint(second.data()));
}

TEST(SnapshotIntegrity, HeaderChecksumCoversEveryHeaderFieldButNotItsOwnTrailer) {
  std::array<uint8_t, 52> header{};
  for (size_t i = 0; i < 48; ++i) header[i] = static_cast<uint8_t>(i * 13 + 7);
  const uint32_t expected = flashcards::detail::snapshotHeaderChecksum(header.data());
  for (size_t i = 0; i < 48; ++i) {
    header[i] ^= 1;
    EXPECT_NE(flashcards::detail::snapshotHeaderChecksum(header.data()), expected) << i;
    header[i] ^= 1;
  }
  header[48] ^= 1;
  EXPECT_EQ(flashcards::detail::snapshotHeaderChecksum(header.data()), expected);
}

}  // namespace
