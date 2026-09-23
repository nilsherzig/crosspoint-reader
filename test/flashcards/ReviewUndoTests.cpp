#include <ReviewUndo.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

TEST(ReviewUndo, RestoresBaseCardAfterLearningRating) {
  std::vector<uint16_t> pending{3, 7, 5};
  flashcards::detail::restorePendingAfterUndo(pending, 5, 0, false, true);
  EXPECT_EQ(pending, (std::vector<uint16_t>{3, 7}));
}

TEST(ReviewUndo, RestoresPendingCardAtOriginalPosition) {
  std::vector<uint16_t> pending{3, 7, 5};
  flashcards::detail::restorePendingAfterUndo(pending, 4, 1, true, true);
  EXPECT_EQ(pending, (std::vector<uint16_t>{3, 4, 7}));
}

TEST(ReviewUndo, RestoresGraduatedPendingCard) {
  std::vector<uint16_t> pending{3, 7};
  flashcards::detail::restorePendingAfterUndo(pending, 4, 1, true, false);
  EXPECT_EQ(pending, (std::vector<uint16_t>{3, 4, 7}));
}

TEST(ReviewUndo, LeavesUnrelatedPendingCardsUntouchedAfterGraduation) {
  std::vector<uint16_t> pending{3, 7};
  flashcards::detail::restorePendingAfterUndo(pending, 4, 0, false, false);
  EXPECT_EQ(pending, (std::vector<uint16_t>{3, 7}));
}

}  // namespace
