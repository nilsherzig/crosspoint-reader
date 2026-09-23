#include <ReviewForecast.h>

#include <gtest/gtest.h>

namespace {

TEST(ReviewForecast, ProjectsFromDistinctIntroductionsNotRepeatRatings) {
  // 20 of 100 cards introduced across 10 UTC days: roughly 40 more days.
  EXPECT_EQ(flashcards::detail::projectedIntroductionDay(100, 109, 100, 80, 150), 149);
}

TEST(ReviewForecast, FirstDayCountsAsOneDayAndRoundsUp) {
  EXPECT_EQ(flashcards::detail::projectedIntroductionDay(100, 100, 10, 7, 3), 103);
}

TEST(ReviewForecast, DoesNotProjectWithoutEvidenceOrRemainingCards) {
  EXPECT_EQ(flashcards::detail::projectedIntroductionDay(-1, 100, 10, 7, 3), -1);
  EXPECT_EQ(flashcards::detail::projectedIntroductionDay(101, 100, 10, 7, 3), -1);
  EXPECT_EQ(flashcards::detail::projectedIntroductionDay(100, 100, 10, 7, 0), -1);
  EXPECT_EQ(flashcards::detail::projectedIntroductionDay(100, 100, 10, 10, 1), -1);
  EXPECT_EQ(flashcards::detail::projectedIntroductionDay(100, 100, 10, 0, 1), -1);
}

}  // namespace
