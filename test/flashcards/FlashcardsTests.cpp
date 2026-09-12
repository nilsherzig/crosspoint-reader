#include <CardScheduler.h>
#include <CsvReader.h>
#include <FlashcardConfigParser.h>
#include <FsrsScheduler.h>
#include <SchedulingFuzzer.h>
#include <StudyQueueBuilder.h>
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace {

struct StringInput {
  std::string value;
  size_t offset = 0;
};

int readByte(void* context) {
  auto& input = *static_cast<StringInput*>(context);
  if (input.offset >= input.value.size()) return -1;
  return static_cast<unsigned char>(input.value[input.offset++]);
}

TEST(CsvReader, ParsesRfc4180QuotingAndBom) {
  StringInput input{
      "\xEF\xBB\xBF"
      "front,back,ignored\r\n\"hello, world\",\"line 1\r\nline \"\"2\"\"\",x\r\n"};
  flashcards::CsvReader reader(readByte, &input);
  std::vector<std::string> fields;
  std::string error;

  ASSERT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::Row);
  ASSERT_EQ(fields, (std::vector<std::string>{"front", "back", "ignored"}));
  ASSERT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::Row);
  ASSERT_EQ(fields, (std::vector<std::string>{"hello, world", "line 1\r\nline \"2\"", "x"}));
  EXPECT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::End);
}

TEST(CsvReader, AcceptsBomBeforeQuotedHeader) {
  StringInput input{
      "\xEF\xBB\xBF"
      "\"front\",\"back\"\n"};
  flashcards::CsvReader reader(readByte, &input);
  std::vector<std::string> fields;
  std::string error;

  ASSERT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::Row);
  EXPECT_EQ(fields, (std::vector<std::string>{"front", "back"}));
}

TEST(CsvReader, RejectsMalformedQuote) {
  StringInput input{"front,back\nokay,bad\"quote\n"};
  flashcards::CsvReader reader(readByte, &input);
  std::vector<std::string> fields;
  std::string error;

  ASSERT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::Row);
  EXPECT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::Error);
  EXPECT_EQ(error, "Quote inside unquoted CSV field");
}

TEST(FsrsScheduler, UsesFsrs6DefaultParametersForNewCards) {
  flashcards::SchedulingResult again;
  flashcards::SchedulingResult good;
  ASSERT_TRUE(flashcards::FsrsScheduler::next(nullptr, 0, flashcards::Rating::Again, 0.9f, 36500, again));
  ASSERT_TRUE(flashcards::FsrsScheduler::next(nullptr, 0, flashcards::Rating::Good, 0.9f, 36500, good));

  EXPECT_NEAR(again.memory.stability, 0.2172f, 0.0001f);
  EXPECT_NEAR(again.memory.difficulty, 7.0114f, 0.0001f);
  EXPECT_EQ(again.intervalDays, 1U);
  EXPECT_NEAR(good.memory.stability, 3.2602f, 0.0001f);
  EXPECT_NEAR(good.memory.difficulty, 4.88463f, 0.0001f);
  EXPECT_EQ(good.intervalDays, 3U);
}

TEST(FsrsScheduler, UpdatesExistingMemoryForGoodAndAgain) {
  const flashcards::MemoryState current{3.2602f, 4.8846316f};
  flashcards::SchedulingResult good;
  flashcards::SchedulingResult again;
  ASSERT_TRUE(flashcards::FsrsScheduler::next(&current, 10, flashcards::Rating::Good, 0.9f, 36500, good));
  ASSERT_TRUE(flashcards::FsrsScheduler::next(&current, 10, flashcards::Rating::Again, 0.9f, 36500, again));

  EXPECT_NEAR(good.memory.stability, 21.79030f, 0.001f);
  EXPECT_NEAR(good.memory.difficulty, 4.86806f, 0.001f);
  EXPECT_EQ(good.intervalDays, 22U);
  EXPECT_NEAR(again.memory.stability, 1.41380f, 0.001f);
  EXPECT_NEAR(again.memory.difficulty, 7.23492f, 0.001f);
}

TEST(FsrsScheduler, AppliesShortTermStepAtZeroElapsedDays) {
  const flashcards::MemoryState current{0.2172f, 7.0114f};
  flashcards::SchedulingResult result;
  ASSERT_TRUE(flashcards::FsrsScheduler::next(&current, 0, flashcards::Rating::Good, 0.9f, 36500, result));
  EXPECT_GT(result.memory.stability, current.stability);
  EXPECT_GE(result.intervalDays, 1U);
}

TEST(StudyQueueBuilder, OrdersDueCardsAndLimitsUnseenCards) {
  constexpr int32_t today = 100;
  constexpr int64_t now = static_cast<int64_t>(today) * 86400;
  std::vector<flashcards::StudyCard> cards(6);
  cards[0].sourceOrder = 4;  // unseen
  cards[1].sourceOrder = 3;
  cards[1].initialized = true;
  cards[1].due = now - 1;
  cards[2].sourceOrder = 2;
  cards[2].introducedDay = today;
  cards[3].sourceOrder = 1;
  cards[3].initialized = true;
  cards[3].due = now + 1;
  cards[4].sourceOrder = 0;
  cards[4].introducedDay = today - 1;
  cards[5].sourceOrder = 5;  // unseen

  std::vector<uint16_t> due;
  std::vector<uint16_t> fresh;
  const uint16_t unseen = flashcards::detail::buildStudyQueues(cards, now, today, 1, 2, 0, due, fresh);

  EXPECT_EQ(due, (std::vector<uint16_t>{4, 1}));
  EXPECT_EQ(fresh, (std::vector<uint16_t>{2, 0}));
  EXPECT_EQ(unseen, 2);
}

TEST(StudyQueueBuilder, KeepsTodaysIntroducedCardsWhenDailyLimitIsExhausted) {
  constexpr int32_t today = 100;
  std::vector<flashcards::StudyCard> cards(2);
  cards[0].sourceOrder = 1;
  cards[0].introducedDay = today;
  cards[1].sourceOrder = 0;  // unseen

  std::vector<uint16_t> due;
  std::vector<uint16_t> fresh;
  flashcards::detail::buildStudyQueues(cards, static_cast<int64_t>(today) * 86400, today, 20, 20, 0, due, fresh);

  EXPECT_TRUE(due.empty());
  EXPECT_EQ(fresh, (std::vector<uint16_t>{0}));
}

TEST(StudyQueueBuilder, AddsRequestedCardsBeyondDailyLimit) {
  constexpr int32_t today = 100;
  std::vector<flashcards::StudyCard> cards(3);
  cards[0].sourceOrder = 2;
  cards[1].sourceOrder = 0;
  cards[2].sourceOrder = 1;

  std::vector<uint16_t> due;
  std::vector<uint16_t> fresh;
  flashcards::detail::buildStudyQueues(cards, static_cast<int64_t>(today) * 86400, today, 20, 20, 2, due, fresh);

  EXPECT_EQ(fresh, (std::vector<uint16_t>{1, 2}));
}

TEST(FlashcardConfigParser, ParsesBoundedMinuteArrays) {
  char valid[] = " [1, 10, 60] ";
  flashcards::LearningSteps steps;
  ASSERT_TRUE(flashcards::detail::parseLearningSteps(valid, steps));
  ASSERT_EQ(steps.count, 3);
  EXPECT_EQ(steps.minutes[0], 1);
  EXPECT_EQ(steps.minutes[1], 10);
  EXPECT_EQ(steps.minutes[2], 60);

  char descending[] = "[10, 1]";
  EXPECT_FALSE(flashcards::detail::parseLearningSteps(descending, steps));
  char empty[] = "[]";
  EXPECT_FALSE(flashcards::detail::parseLearningSteps(empty, steps));
  char tooLong[] = "[1, 2, 3, 4, 5, 6, 7, 8, 9]";
  EXPECT_FALSE(flashcards::detail::parseLearningSteps(tooLong, steps));
}

TEST(SchedulingFuzzer, UsesAnkiStyleReviewRanges) {
  EXPECT_EQ(flashcards::SchedulingFuzzer::reviewBounds(2.49f, 1, 1000).lower, 2U);
  EXPECT_EQ(flashcards::SchedulingFuzzer::reviewBounds(2.49f, 1, 1000).upper, 2U);
  EXPECT_EQ(flashcards::SchedulingFuzzer::reviewBounds(7.0f, 1, 1000).lower, 5U);
  EXPECT_EQ(flashcards::SchedulingFuzzer::reviewBounds(7.0f, 1, 1000).upper, 9U);
  EXPECT_EQ(flashcards::SchedulingFuzzer::reviewBounds(37.0f, 1, 1000).lower, 33U);
  EXPECT_EQ(flashcards::SchedulingFuzzer::reviewBounds(37.0f, 1, 1000).upper, 41U);
}

TEST(SchedulingFuzzer, IsDeterministicAndBoundsLearningDelay) {
  const uint32_t first = flashcards::SchedulingFuzzer::fuzzReviewInterval(17.0f, 1, 1000, 1234);
  EXPECT_EQ(first, flashcards::SchedulingFuzzer::fuzzReviewInterval(17.0f, 1, 1000, 1234));
  EXPECT_GE(first, 14U);
  EXPECT_LE(first, 20U);

  const uint32_t learning = flashcards::SchedulingFuzzer::fuzzLearningDelay(600, 1234);
  EXPECT_GE(learning, 600U);
  EXPECT_LT(learning, 750U);
}

TEST(CardScheduler, AdvancesThroughDefaultLearningSteps) {
  const flashcards::LearningSteps learning{{1, 10}, 2};
  const flashcards::LearningSteps relearning{{10}, 1};
  const flashcards::CardSchedulingOptions options{0.9f, 36500, &learning, &relearning};
  flashcards::CardSchedulingState state;
  flashcards::CardSchedulingResult result;

  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Again, options, 1, result));
  EXPECT_EQ(result.phase, flashcards::CardPhase::Learning);
  EXPECT_EQ(result.learningStep, 0);
  EXPECT_GE(result.learningDelaySeconds, 60U);
  EXPECT_LT(result.learningDelaySeconds, 75U);

  state = {result.memory, result.phase, result.learningStep, 0, true};
  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Good, options, 2, result));
  EXPECT_EQ(result.phase, flashcards::CardPhase::Learning);
  EXPECT_EQ(result.learningStep, 1);
  EXPECT_GE(result.learningDelaySeconds, 600U);
  EXPECT_LT(result.learningDelaySeconds, 750U);

  state = {result.memory, result.phase, result.learningStep, 0, true};
  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Good, options, 3, result));
  EXPECT_EQ(result.phase, flashcards::CardPhase::Review);
  EXPECT_EQ(result.learningDelaySeconds, 0U);
  EXPECT_GE(result.intervalDays, 1U);
}

TEST(CardScheduler, SendsFailedReviewsThroughRelearning) {
  const flashcards::LearningSteps learning{{1, 10}, 2};
  const flashcards::LearningSteps relearning{{10}, 1};
  const flashcards::CardSchedulingOptions options{0.9f, 36500, &learning, &relearning};
  const flashcards::CardSchedulingState review{{3.2602f, 4.8846316f}, flashcards::CardPhase::Review, 0, 10, true};
  flashcards::CardSchedulingResult result;

  ASSERT_TRUE(flashcards::CardScheduler::next(review, flashcards::Rating::Again, options, 4, result));
  EXPECT_EQ(result.phase, flashcards::CardPhase::Relearning);
  EXPECT_EQ(result.learningStep, 0);
  EXPECT_GE(result.learningDelaySeconds, 600U);
  EXPECT_LT(result.learningDelaySeconds, 750U);

  const flashcards::CardSchedulingState relearn{result.memory, result.phase, result.learningStep, 0, true};
  ASSERT_TRUE(flashcards::CardScheduler::next(relearn, flashcards::Rating::Good, options, 5, result));
  EXPECT_EQ(result.phase, flashcards::CardPhase::Review);
  EXPECT_EQ(result.learningDelaySeconds, 0U);
  EXPECT_GE(result.intervalDays, 1U);
}

}  // namespace
