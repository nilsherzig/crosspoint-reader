#include <StudyQueueBuilder.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

constexpr int32_t TODAY = 20000;
constexpr int64_t NOW = static_cast<int64_t>(TODAY) * 86400 + 3600;

struct QueueResult {
  std::vector<uint16_t> due;
  std::vector<uint16_t> fresh;
  uint16_t unseen = 0;
};

QueueResult buildQueues(const std::vector<flashcards::StudyCard>& cards, const uint16_t introducedToday = 0,
                        const uint16_t newCardsPerDay = 20, const uint16_t additionalNewCards = 0) {
  QueueResult result;
  result.unseen = flashcards::detail::buildStudyQueues(cards, NOW, TODAY, introducedToday, newCardsPerDay,
                                                        additionalNewCards, result.due, result.fresh);
  return result;
}

flashcards::StudyCard unseenCard(const uint32_t sourceOrder) {
  flashcards::StudyCard card;
  card.sourceOrder = sourceOrder;
  return card;
}

flashcards::StudyCard introducedCard(const uint32_t sourceOrder, const int32_t introducedDay) {
  flashcards::StudyCard card;
  card.sourceOrder = sourceOrder;
  card.introducedDay = introducedDay;
  return card;
}

flashcards::StudyCard scheduledCard(const uint32_t sourceOrder, const flashcards::CardPhase phase,
                                    const int64_t due) {
  flashcards::StudyCard card;
  card.sourceOrder = sourceOrder;
  card.phase = phase;
  card.due = due;
  card.introducedDay = TODAY;
  card.initialized = true;
  return card;
}

TEST(StudyQueueBuilderBehavior, EmptyDeckClearsReusedOutputQueues) {
  std::vector<uint16_t> due{4, 5};
  std::vector<uint16_t> fresh{6, 7};
  const std::vector<flashcards::StudyCard> cards;

  const uint16_t unseen = flashcards::detail::buildStudyQueues(cards, NOW, TODAY, 0, 20, 0, due, fresh);

  EXPECT_TRUE(due.empty());
  EXPECT_TRUE(fresh.empty());
  EXPECT_EQ(unseen, 0u);
}

TEST(StudyQueueBuilderBehavior, CountsAllUnseenButSelectsOnlyDailyQuota) {
  const std::vector<flashcards::StudyCard> cards{
      unseenCard(4), unseenCard(0), unseenCard(3), unseenCard(1), unseenCard(2)};

  const QueueResult result = buildQueues(cards, 0, 2);

  EXPECT_TRUE(result.due.empty());
  EXPECT_EQ(result.fresh, (std::vector<uint16_t>{1, 3}));
  EXPECT_EQ(result.unseen, 5u);
}

TEST(StudyQueueBuilderBehavior, DailyQuotaUsesOnlyTheRemainingAllowance) {
  const std::vector<flashcards::StudyCard> cards{unseenCard(2), unseenCard(0), unseenCard(1)};

  const QueueResult result = buildQueues(cards, 19, 20);

  EXPECT_EQ(result.fresh, (std::vector<uint16_t>{1}));
  EXPECT_EQ(result.unseen, 3u);
}

TEST(StudyQueueBuilderBehavior, AdditionalAllowanceCombinesWithRemainingDailyQuota) {
  const std::vector<flashcards::StudyCard> cards{
      unseenCard(3), unseenCard(0), unseenCard(4), unseenCard(1), unseenCard(2)};

  const QueueResult result = buildQueues(cards, 19, 20, 2);

  EXPECT_EQ(result.fresh, (std::vector<uint16_t>{1, 3, 4}));
  EXPECT_EQ(result.unseen, 5u);
}

TEST(StudyQueueBuilderBehavior, AllowancesAreCappedByAvailableUnseenCards) {
  const std::vector<flashcards::StudyCard> cards{unseenCard(1), unseenCard(0)};

  const QueueResult result = buildQueues(cards, 0, 1000, UINT16_MAX);

  EXPECT_EQ(result.fresh, (std::vector<uint16_t>{1, 0}));
  EXPECT_EQ(result.unseen, 2u);
}

TEST(StudyQueueBuilderBehavior, ExhaustedOrExceededDailyLimitDoesNotUnderflow) {
  const std::vector<flashcards::StudyCard> cards{unseenCard(0), unseenCard(1)};

  const QueueResult exhausted = buildQueues(cards, 20, 20);
  const QueueResult exceeded = buildQueues(cards, 21, 20);

  EXPECT_TRUE(exhausted.fresh.empty());
  EXPECT_TRUE(exceeded.fresh.empty());
  EXPECT_EQ(exhausted.unseen, 2u);
  EXPECT_EQ(exceeded.unseen, 2u);
}

TEST(StudyQueueBuilderBehavior, TodaysIntroducedCardsRemainAvailableWithoutDailyQuota) {
  const std::vector<flashcards::StudyCard> cards{introducedCard(2, TODAY), unseenCard(0), introducedCard(1, TODAY)};

  const QueueResult result = buildQueues(cards, 20, 20);

  EXPECT_TRUE(result.due.empty());
  EXPECT_EQ(result.fresh, (std::vector<uint16_t>{2, 0}));
  EXPECT_EQ(result.unseen, 1u);
}

TEST(StudyQueueBuilderBehavior, TodaysIntroducedAndSelectedUnseenCardsShareCsvOrder) {
  const std::vector<flashcards::StudyCard> cards{introducedCard(4, TODAY), unseenCard(1), introducedCard(0, TODAY),
                                                 unseenCard(3), unseenCard(2)};

  const QueueResult result = buildQueues(cards, 1, 3);

  EXPECT_EQ(result.fresh, (std::vector<uint16_t>{2, 1, 4, 0}));
  EXPECT_EQ(result.unseen, 3u);
}

TEST(StudyQueueBuilderBehavior, PreviouslyIntroducedUninitializedCardsAreDue) {
  const std::vector<flashcards::StudyCard> cards{introducedCard(2, TODAY - 1), introducedCard(0, TODAY - 5),
                                                 unseenCard(1)};

  const QueueResult result = buildQueues(cards, 20, 20);

  EXPECT_EQ(result.due, (std::vector<uint16_t>{1, 0}));
  EXPECT_TRUE(result.fresh.empty());
  EXPECT_EQ(result.unseen, 1u);
}

TEST(StudyQueueBuilderBehavior, DueCardsSortByTimestampThenCsvOrder) {
  const std::vector<flashcards::StudyCard> cards{
      scheduledCard(5, flashcards::CardPhase::Review, NOW - 10),
      scheduledCard(2, flashcards::CardPhase::Review, NOW - 10),
      scheduledCard(9, flashcards::CardPhase::Review, NOW - 20),
      scheduledCard(1, flashcards::CardPhase::Review, NOW)};

  const QueueResult result = buildQueues(cards);

  EXPECT_EQ(result.due, (std::vector<uint16_t>{2, 1, 0, 3}));
}

TEST(StudyQueueBuilderBehavior, ReviewDueNowIsIncludedButFutureReviewIsExcluded) {
  const std::vector<flashcards::StudyCard> cards{
      scheduledCard(0, flashcards::CardPhase::Review, NOW),
      scheduledCard(1, flashcards::CardPhase::Review, NOW + 1)};

  const QueueResult result = buildQueues(cards);

  EXPECT_EQ(result.due, (std::vector<uint16_t>{0}));
}

TEST(StudyQueueBuilderBehavior, FutureLearningAndRelearningRemainVisibleOutsideLearnAheadWindow) {
  const std::vector<flashcards::StudyCard> cards{
      scheduledCard(0, flashcards::CardPhase::Learning, NOW + 86400),
      scheduledCard(1, flashcards::CardPhase::Relearning, NOW + 172800),
      scheduledCard(2, flashcards::CardPhase::Review, NOW + 60)};

  const QueueResult result = buildQueues(cards);

  EXPECT_EQ(result.due, (std::vector<uint16_t>{0, 1}));
}

TEST(StudyQueueBuilderBehavior, PendingLearningPreventsOfferToReplaceItWithMoreNewCards) {
  const std::vector<flashcards::StudyCard> cards{
      scheduledCard(0, flashcards::CardPhase::Learning, NOW + 600), unseenCard(1)};

  const QueueResult result = buildQueues(cards, 20, 20);
  const bool wouldOfferAdditionalNewCards = result.due.empty() && result.fresh.empty() && result.unseen > 0;

  EXPECT_EQ(result.due, (std::vector<uint16_t>{0}));
  EXPECT_TRUE(result.fresh.empty());
  EXPECT_EQ(result.unseen, 1u);
  EXPECT_FALSE(wouldOfferAdditionalNewCards);
}

TEST(StudyQueueBuilderBehavior, PendingClassificationRequiresInitializedFutureLearningPhase) {
  flashcards::StudyCard card = scheduledCard(0, flashcards::CardPhase::Learning, NOW + 1);
  EXPECT_TRUE(flashcards::detail::learningCardPending(card, NOW));

  card.phase = flashcards::CardPhase::Relearning;
  EXPECT_TRUE(flashcards::detail::learningCardPending(card, NOW));

  card.phase = flashcards::CardPhase::Review;
  EXPECT_FALSE(flashcards::detail::learningCardPending(card, NOW));

  card.phase = flashcards::CardPhase::Learning;
  card.due = NOW;
  EXPECT_FALSE(flashcards::detail::learningCardPending(card, NOW));

  card.due = NOW + 1;
  card.initialized = false;
  EXPECT_FALSE(flashcards::detail::learningCardPending(card, NOW));
}

TEST(StudyQueueBuilderBehavior, EmptyPendingQueueHasNoSelectableLearningCard) {
  const std::vector<flashcards::StudyCard> cards;
  const std::vector<uint16_t> pending;

  EXPECT_EQ(flashcards::detail::nextLearningCardPosition(cards, pending, NOW, 20, false), pending.size());
}

TEST(StudyQueueBuilderBehavior, BaseCardsBlockLearnAheadButNotActuallyDueLearningCards) {
  std::vector<flashcards::StudyCard> cards(2);
  cards[0].due = NOW + 60;
  cards[1].due = NOW - 1;

  const std::vector<uint16_t> futureOnly{0};
  EXPECT_EQ(flashcards::detail::nextLearningCardPosition(cards, futureOnly, NOW, 20, true), futureOnly.size());

  const std::vector<uint16_t> withDueCard{0, 1};
  EXPECT_EQ(flashcards::detail::nextLearningCardPosition(cards, withDueCard, NOW, 20, true), 1u);
}

TEST(StudyQueueBuilderBehavior, LearnAheadSkipsCardsOutsideWindowAndKeepsEligibleOrder) {
  std::vector<flashcards::StudyCard> cards(3);
  cards[0].due = NOW + 1200;
  cards[1].due = NOW + 600;
  cards[2].due = NOW + 60;
  const std::vector<uint16_t> pending{0, 1, 2};

  EXPECT_EQ(flashcards::detail::nextLearningCardPosition(cards, pending, NOW, 20, false), 1u);
}

TEST(StudyQueueBuilderBehavior, LearnAheadLimitBoundaryIsStrict) {
  std::vector<flashcards::StudyCard> cards(2);
  cards[0].due = NOW + 1200;
  cards[1].due = NOW + 1199;

  const std::vector<uint16_t> boundaryOnly{0};
  EXPECT_EQ(flashcards::detail::nextLearningCardPosition(cards, boundaryOnly, NOW, 20, false), boundaryOnly.size());

  const std::vector<uint16_t> insideWindow{0, 1};
  EXPECT_EQ(flashcards::detail::nextLearningCardPosition(cards, insideWindow, NOW, 20, false), 1u);
}

TEST(StudyQueueBuilderBehavior, ActuallyDueCardPreemptsEarlierQueuedLearnAheadCard) {
  std::vector<flashcards::StudyCard> cards(3);
  cards[0].due = NOW + 60;
  cards[1].due = NOW - 1;
  cards[2].due = NOW - 10;
  const std::vector<uint16_t> pending{0, 1, 2};

  EXPECT_EQ(flashcards::detail::nextLearningCardPosition(cards, pending, NOW, 20, false), 2u);
}

TEST(StudyQueueBuilderBehavior, EqualDueTimestampsPreservePendingQueueOrder) {
  std::vector<flashcards::StudyCard> cards(2);
  cards[0].due = NOW - 1;
  cards[1].due = NOW - 1;
  const std::vector<uint16_t> pending{1, 0};

  EXPECT_EQ(flashcards::detail::nextLearningCardPosition(cards, pending, NOW, 20, false), 0u);
}

TEST(StudyQueueBuilderBehavior, ZeroLearnAheadLimitRequiresTimestampToBeDue) {
  std::vector<flashcards::StudyCard> cards(2);
  cards[0].due = NOW + 1;
  cards[1].due = NOW;

  const std::vector<uint16_t> futureOnly{0};
  EXPECT_EQ(flashcards::detail::nextLearningCardPosition(cards, futureOnly, NOW, 0, false), futureOnly.size());

  const std::vector<uint16_t> withDueCard{0, 1};
  EXPECT_EQ(flashcards::detail::nextLearningCardPosition(cards, withDueCard, NOW, 0, false), 1u);
}

}  // namespace
