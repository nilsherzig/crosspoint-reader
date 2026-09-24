#include <FlashcardBackupState.h>
#include <gtest/gtest.h>

TEST(FlashcardBackupCounter, AccumulatesReviewsAcrossDecksWithoutDoubleCounting) {
  uint32_t total = 0;
  total = flashcards::detail::reconcileBackupReviews(total, 100, 103);
  total = flashcards::detail::reconcileBackupReviews(total, 20, 22);
  EXPECT_EQ(total, 5U);
  EXPECT_EQ(flashcards::detail::reconcileBackupReviews(total, 103, 103), total);
}

TEST(FlashcardBackupCounter, UndoDecreasesNetReviews) {
  EXPECT_EQ(flashcards::detail::reconcileBackupReviews(5, 103, 102), 4U);
  EXPECT_EQ(flashcards::detail::reconcileBackupReviews(1, 103, 100), 0U);
}

TEST(FlashcardBackupCounter, DecliningPostponesAnotherFullInterval) {
  constexpr uint32_t interval = 500;
  EXPECT_FALSE(flashcards::detail::backupReminderDue(499, 0, interval));
  EXPECT_TRUE(flashcards::detail::backupReminderDue(505, 0, interval));
  const uint32_t postponed = flashcards::detail::saturatingAdd(505, interval);
  EXPECT_FALSE(flashcards::detail::backupReminderDue(1004, postponed, interval));
  EXPECT_TRUE(flashcards::detail::backupReminderDue(1005, postponed, interval));
}

TEST(FlashcardBackupCounter, SaturatesOnExcessiveReviewCounts) {
  EXPECT_EQ(flashcards::detail::reconcileBackupReviews(UINT32_MAX - 1, 0, 10), UINT32_MAX);
  EXPECT_EQ(flashcards::detail::saturatingAdd(UINT32_MAX - 1, 500), UINT32_MAX);
}
