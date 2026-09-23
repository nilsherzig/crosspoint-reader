#include <ReviewCount.h>

#include <gtest/gtest.h>

#include <cstdint>

namespace {

TEST(ReviewCount, CountsEveryRatingIncludingRepeatReviews) {
  using flashcards::detail::ReviewCountEvent;
  uint32_t count = 0;
  count = flashcards::detail::countAfterReviewEvent(count, ReviewCountEvent::Review);
  count = flashcards::detail::countAfterReviewEvent(count, ReviewCountEvent::Review);
  EXPECT_EQ(count, 2u);
}

TEST(ReviewCount, UndoRemovesOneRatingAndReratingCountsOnce) {
  using flashcards::detail::ReviewCountEvent;
  uint32_t count = 9;
  count = flashcards::detail::countAfterReviewEvent(count, ReviewCountEvent::Review);
  count = flashcards::detail::countAfterReviewEvent(count, ReviewCountEvent::Undo);
  count = flashcards::detail::countAfterReviewEvent(count, ReviewCountEvent::Review);
  EXPECT_EQ(count, 10u);
}

TEST(ReviewCount, BoundsCorruptHistoryAndOverflow) {
  using flashcards::detail::ReviewCountEvent;
  EXPECT_EQ(flashcards::detail::countAfterReviewEvent(0, ReviewCountEvent::Undo), 0u);
  EXPECT_EQ(flashcards::detail::countAfterReviewEvent(UINT32_MAX, ReviewCountEvent::Review), UINT32_MAX);
}

}  // namespace
