#include <FlashcardTextLayout.h>

#include <gtest/gtest.h>

namespace {

TEST(FlashcardTextLayout, ShortQuestionLeavesSpaceForLongAnswer) {
  const auto layout = flashcards::detail::cardTextLayout(320, 16, 32, 32 + 8);
  EXPECT_EQ(layout.questionHeight, 40);
  EXPECT_EQ(layout.gap, 16);
  EXPECT_EQ(layout.answerHeight, 264);
}

TEST(FlashcardTextLayout, LongQuestionKeepsHalfForAnswer) {
  const auto layout = flashcards::detail::cardTextLayout(320, 16, 32, 400);
  EXPECT_EQ(layout.questionHeight, 152);
  EXPECT_EQ(layout.answerHeight, 152);
  EXPECT_EQ(layout.questionHeight + layout.gap + layout.answerHeight, 320);
}

TEST(FlashcardTextLayout, TinyBodyNeverDrawsPastItsBounds) {
  const auto oneLine = flashcards::detail::cardTextLayout(32, 16, 32, 100);
  EXPECT_EQ(oneLine.questionHeight, 32);
  EXPECT_EQ(oneLine.answerHeight, 0);
  const auto noLine = flashcards::detail::cardTextLayout(31, 16, 32, 100);
  EXPECT_EQ(noLine.questionHeight, 0);
  EXPECT_EQ(noLine.answerHeight, 0);
  const auto twoLines = flashcards::detail::cardTextLayout(64, 16, 32, 100);
  EXPECT_EQ(twoLines.questionHeight, 32);
  EXPECT_EQ(twoLines.gap, 0);
  EXPECT_EQ(twoLines.answerHeight, 32);
}

TEST(FlashcardTextLayout, LineCountNeverOverflowsASection) {
  EXPECT_EQ(flashcards::detail::cardTextLines(0, 32), 0);
  EXPECT_EQ(flashcards::detail::cardTextLines(31, 32), 0);
  EXPECT_EQ(flashcards::detail::cardTextLines(32, 32), 1);
  EXPECT_EQ(flashcards::detail::cardTextLines(250, 32), 7);
  EXPECT_EQ(flashcards::detail::cardTextLines(1000, 32), 16);
  EXPECT_EQ(flashcards::detail::cardTextLines(320, 0), 0);
}

}  // namespace
