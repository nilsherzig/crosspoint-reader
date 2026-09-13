#include <CardScheduler.h>
#include <FsrsScheduler.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr flashcards::LearningSteps DEFAULT_LEARNING{{1, 10}, 2};
constexpr flashcards::LearningSteps DEFAULT_RELEARNING{{10}, 1};
constexpr flashcards::LearningSteps THREE_LEARNING_STEPS{{1, 10, 60}, 3};
constexpr flashcards::LearningSteps TWO_RELEARNING_STEPS{{5, 30}, 2};
constexpr flashcards::LearningSteps SINGLE_LEARNING_STEP{{7}, 1};

flashcards::CardSchedulingOptions options(const flashcards::LearningSteps* learning = &DEFAULT_LEARNING,
                                          const flashcards::LearningSteps* relearning = &DEFAULT_RELEARNING,
                                          const float retention = 0.9f, const uint32_t maximumIntervalDays = 36500) {
  return flashcards::CardSchedulingOptions{retention, maximumIntervalDays, learning, relearning};
}

flashcards::CardSchedulingState initializedState(const flashcards::CardPhase phase, const uint8_t learningStep = 0,
                                                 const uint32_t elapsedDays = 10) {
  return flashcards::CardSchedulingState{{3.2602f, 4.8846316f}, phase, learningStep, elapsedDays, true};
}

void expectLearningStep(const flashcards::CardSchedulingResult& result, const flashcards::CardPhase phase,
                        const uint8_t step, const uint32_t minutes) {
  EXPECT_EQ(result.phase, phase);
  EXPECT_EQ(result.learningStep, step);
  EXPECT_EQ(result.intervalDays, 0u);
  EXPECT_GE(result.learningDelaySeconds, minutes * 60u);
  EXPECT_LT(result.learningDelaySeconds, minutes * 60u + std::min(minutes * 15u, 300u));
}

TEST(CardSchedulerBehavior, RejectsMissingLearningStepDefinitions) {
  flashcards::CardSchedulingResult result;
  flashcards::CardSchedulingState state;

  EXPECT_FALSE(flashcards::CardScheduler::next(state, flashcards::Rating::Again,
                                                options(nullptr, &DEFAULT_RELEARNING), 1, result));
  EXPECT_FALSE(flashcards::CardScheduler::next(state, flashcards::Rating::Again,
                                                options(&DEFAULT_LEARNING, nullptr), 1, result));
}

TEST(CardSchedulerBehavior, RejectsEmptyOrOversizedLearningStepDefinitions) {
  const flashcards::LearningSteps empty{};
  flashcards::LearningSteps oversized{{1, 2, 3, 4, 5, 6, 7, 8}, flashcards::MAX_LEARNING_STEPS + 1};
  flashcards::CardSchedulingResult result;
  flashcards::CardSchedulingState state;

  EXPECT_FALSE(flashcards::CardScheduler::next(state, flashcards::Rating::Again, options(&empty), 1, result));
  EXPECT_FALSE(flashcards::CardScheduler::next(state, flashcards::Rating::Again,
                                                options(&DEFAULT_LEARNING, &empty), 1, result));
  EXPECT_FALSE(flashcards::CardScheduler::next(state, flashcards::Rating::Again, options(&oversized), 1, result));
  EXPECT_FALSE(flashcards::CardScheduler::next(state, flashcards::Rating::Again,
                                                options(&DEFAULT_LEARNING, &oversized), 1, result));
}

TEST(CardSchedulerBehavior, RejectsInvalidOrUninitializedCardPhases) {
  flashcards::CardSchedulingResult result;
  for (const auto phase : {flashcards::CardPhase::Learning, flashcards::CardPhase::Review,
                           flashcards::CardPhase::Relearning}) {
    flashcards::CardSchedulingState state;
    state.phase = phase;
    EXPECT_FALSE(flashcards::CardScheduler::next(state, flashcards::Rating::Good, options(), 1, result));
  }

  auto state = initializedState(static_cast<flashcards::CardPhase>(0));
  EXPECT_FALSE(flashcards::CardScheduler::next(state, flashcards::Rating::Good, options(), 1, result));
  state.phase = static_cast<flashcards::CardPhase>(5);
  EXPECT_FALSE(flashcards::CardScheduler::next(state, flashcards::Rating::Good, options(), 1, result));
}

TEST(CardSchedulerBehavior, PropagatesInvalidFsrsOptionsAndRatings) {
  flashcards::CardSchedulingResult result;
  const auto state = initializedState(flashcards::CardPhase::Review);

  EXPECT_FALSE(flashcards::CardScheduler::next(state, static_cast<flashcards::Rating>(2), options(), 1, result));
  EXPECT_FALSE(flashcards::CardScheduler::next(state, flashcards::Rating::Good,
                                                options(&DEFAULT_LEARNING, &DEFAULT_RELEARNING, 0.0f), 1, result));
  EXPECT_FALSE(flashcards::CardScheduler::next(state, flashcards::Rating::Good,
                                                options(&DEFAULT_LEARNING, &DEFAULT_RELEARNING, 1.0f), 1, result));
  EXPECT_FALSE(flashcards::CardScheduler::next(
      state, flashcards::Rating::Good, options(&DEFAULT_LEARNING, &DEFAULT_RELEARNING, 0.9f, 0), 1, result));
}

TEST(CardSchedulerBehavior, AgainOnNewCardStartsAtFirstLearningStep) {
  flashcards::CardSchedulingResult result;

  ASSERT_TRUE(flashcards::CardScheduler::next({}, flashcards::Rating::Again, options(&THREE_LEARNING_STEPS), 11,
                                               result));
  expectLearningStep(result, flashcards::CardPhase::Learning, 0, 1);
}

TEST(CardSchedulerBehavior, GoodOnNewCardStartsAtSecondLearningStep) {
  flashcards::CardSchedulingResult result;

  ASSERT_TRUE(flashcards::CardScheduler::next({}, flashcards::Rating::Good, options(&THREE_LEARNING_STEPS), 12,
                                               result));
  expectLearningStep(result, flashcards::CardPhase::Learning, 1, 10);
}

TEST(CardSchedulerBehavior, GoodOnNewCardWithOneStepGraduatesImmediately) {
  flashcards::CardSchedulingResult result;

  ASSERT_TRUE(flashcards::CardScheduler::next({}, flashcards::Rating::Good, options(&SINGLE_LEARNING_STEP), 13,
                                               result));
  EXPECT_EQ(result.phase, flashcards::CardPhase::Review);
  EXPECT_EQ(result.learningStep, 0);
  EXPECT_EQ(result.learningDelaySeconds, 0u);
  EXPECT_GE(result.intervalDays, 1u);
}

TEST(CardSchedulerBehavior, AgainOnLearningCardResetsToFirstStep) {
  flashcards::CardSchedulingResult result;
  const auto state = initializedState(flashcards::CardPhase::Learning, 2, 0);

  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Again,
                                               options(&THREE_LEARNING_STEPS), 14, result));
  expectLearningStep(result, flashcards::CardPhase::Learning, 0, 1);
}

TEST(CardSchedulerBehavior, GoodOnLearningCardAdvancesExactlyOneStep) {
  flashcards::CardSchedulingResult result;
  const auto state = initializedState(flashcards::CardPhase::Learning, 0, 0);

  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Good,
                                               options(&THREE_LEARNING_STEPS), 15, result));
  expectLearningStep(result, flashcards::CardPhase::Learning, 1, 10);
}

TEST(CardSchedulerBehavior, GoodOnLastLearningStepGraduates) {
  flashcards::CardSchedulingResult result;
  const auto state = initializedState(flashcards::CardPhase::Learning, 2, 0);

  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Good,
                                               options(&THREE_LEARNING_STEPS), 16, result));
  EXPECT_EQ(result.phase, flashcards::CardPhase::Review);
  EXPECT_EQ(result.learningStep, 0);
  EXPECT_EQ(result.learningDelaySeconds, 0u);
  EXPECT_GE(result.intervalDays, 1u);
}

TEST(CardSchedulerBehavior, GoodReviewRemainsGraduated) {
  flashcards::CardSchedulingResult result;
  const auto state = initializedState(flashcards::CardPhase::Review, 0, 20);

  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Good, options(), 17, result));
  EXPECT_EQ(result.phase, flashcards::CardPhase::Review);
  EXPECT_EQ(result.learningStep, 0);
  EXPECT_EQ(result.learningDelaySeconds, 0u);
  EXPECT_GE(result.intervalDays, 1u);
}

TEST(CardSchedulerBehavior, AgainReviewStartsFirstRelearningStep) {
  flashcards::CardSchedulingResult result;
  const auto state = initializedState(flashcards::CardPhase::Review, 0, 20);

  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Again,
                                               options(&DEFAULT_LEARNING, &TWO_RELEARNING_STEPS), 18, result));
  expectLearningStep(result, flashcards::CardPhase::Relearning, 0, 5);
}

TEST(CardSchedulerBehavior, AgainOnRelearningCardResetsToFirstStep) {
  flashcards::CardSchedulingResult result;
  const auto state = initializedState(flashcards::CardPhase::Relearning, 1, 0);

  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Again,
                                               options(&DEFAULT_LEARNING, &TWO_RELEARNING_STEPS), 19, result));
  expectLearningStep(result, flashcards::CardPhase::Relearning, 0, 5);
}

TEST(CardSchedulerBehavior, GoodOnRelearningCardAdvancesThenGraduates) {
  flashcards::CardSchedulingResult first;
  auto state = initializedState(flashcards::CardPhase::Relearning, 0, 0);
  const auto schedulingOptions = options(&DEFAULT_LEARNING, &TWO_RELEARNING_STEPS);

  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Good, schedulingOptions, 20, first));
  expectLearningStep(first, flashcards::CardPhase::Relearning, 1, 30);

  state.memory = first.memory;
  state.learningStep = first.learningStep;
  flashcards::CardSchedulingResult second;
  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Good, schedulingOptions, 21, second));
  EXPECT_EQ(second.phase, flashcards::CardPhase::Review);
  EXPECT_EQ(second.learningDelaySeconds, 0u);
  EXPECT_GE(second.intervalDays, 1u);
}

TEST(CardSchedulerBehavior, HonorsMaximumReviewInterval) {
  flashcards::CardSchedulingResult result;
  const flashcards::CardSchedulingState state{{36500.0f, 1.0f}, flashcards::CardPhase::Review, 0, 36500, true};

  ASSERT_TRUE(flashcards::CardScheduler::next(
      state, flashcards::Rating::Good, options(&DEFAULT_LEARNING, &DEFAULT_RELEARNING, 0.9f, 2), 22, result));
  EXPECT_EQ(result.phase, flashcards::CardPhase::Review);
  EXPECT_GE(result.intervalDays, 1u);
  EXPECT_LE(result.intervalDays, 2u);
}

TEST(CardSchedulerBehavior, SameStateAndSeedProduceIdenticalSchedule) {
  const auto state = initializedState(flashcards::CardPhase::Review, 0, 20);
  flashcards::CardSchedulingResult first;
  flashcards::CardSchedulingResult second;

  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Good, options(), 123456, first));
  ASSERT_TRUE(flashcards::CardScheduler::next(state, flashcards::Rating::Good, options(), 123456, second));
  EXPECT_FLOAT_EQ(first.memory.stability, second.memory.stability);
  EXPECT_FLOAT_EQ(first.memory.difficulty, second.memory.difficulty);
  EXPECT_EQ(first.phase, second.phase);
  EXPECT_EQ(first.learningStep, second.learningStep);
  EXPECT_EQ(first.intervalDays, second.intervalDays);
  EXPECT_EQ(first.learningDelaySeconds, second.learningDelaySeconds);
}

TEST(FsrsSchedulerBehavior, RejectsUnsupportedRatingsRetentionAndMaximumInterval) {
  flashcards::SchedulingResult result;

  EXPECT_FALSE(flashcards::FsrsScheduler::next(nullptr, 0, static_cast<flashcards::Rating>(2), 0.9f, 36500, result));
  EXPECT_FALSE(flashcards::FsrsScheduler::next(nullptr, 0, flashcards::Rating::Good, 0.0f, 36500, result));
  EXPECT_FALSE(flashcards::FsrsScheduler::next(nullptr, 0, flashcards::Rating::Good, 1.0f, 36500, result));
  EXPECT_FALSE(flashcards::FsrsScheduler::next(nullptr, 0, flashcards::Rating::Good, 0.9f, 0, result));
}

TEST(FsrsSchedulerBehavior, RejectsNonFiniteOrNonPositiveMemoryState) {
  flashcards::SchedulingResult result;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float infinity = std::numeric_limits<float>::infinity();

  for (const flashcards::MemoryState state : {flashcards::MemoryState{0.0f, 5.0f},
                                               flashcards::MemoryState{-1.0f, 5.0f},
                                               flashcards::MemoryState{nan, 5.0f},
                                               flashcards::MemoryState{5.0f, nan},
                                               flashcards::MemoryState{5.0f, infinity}}) {
    EXPECT_FALSE(flashcards::FsrsScheduler::next(&state, 1, flashcards::Rating::Good, 0.9f, 36500, result));
  }
}

TEST(FsrsSchedulerBehavior, ClampsFiniteMemoryAndOutputToSupportedRanges) {
  const flashcards::MemoryState state{50000.0f, -100.0f};
  flashcards::SchedulingResult result;

  ASSERT_TRUE(flashcards::FsrsScheduler::next(&state, 30, flashcards::Rating::Good, 0.9f, 100, result));
  EXPECT_GT(result.memory.stability, 0.0f);
  EXPECT_LE(result.memory.stability, 36500.0f);
  EXPECT_GE(result.memory.difficulty, 1.0f);
  EXPECT_LE(result.memory.difficulty, 10.0f);
  EXPECT_GE(result.intervalDaysExact, 1.0f);
  EXPECT_LE(result.intervalDaysExact, 100.0f);
  EXPECT_GE(result.intervalDays, 1u);
  EXPECT_LE(result.intervalDays, 100u);
}

TEST(FsrsSchedulerBehavior, HigherDesiredRetentionProducesNoLongerInterval) {
  const flashcards::MemoryState state{30.0f, 5.0f};
  flashcards::SchedulingResult lowerRetention;
  flashcards::SchedulingResult higherRetention;

  ASSERT_TRUE(flashcards::FsrsScheduler::next(&state, 30, flashcards::Rating::Good, 0.80f, 36500,
                                               lowerRetention));
  ASSERT_TRUE(flashcards::FsrsScheduler::next(&state, 30, flashcards::Rating::Good, 0.95f, 36500,
                                               higherRetention));
  EXPECT_LT(higherRetention.intervalDaysExact, lowerRetention.intervalDaysExact);
}

TEST(FsrsSchedulerBehavior, AgainAfterElapsedTimeIsHarderAndLessStableThanGood) {
  const flashcards::MemoryState state{30.0f, 5.0f};
  flashcards::SchedulingResult again;
  flashcards::SchedulingResult good;

  ASSERT_TRUE(flashcards::FsrsScheduler::next(&state, 30, flashcards::Rating::Again, 0.9f, 36500, again));
  ASSERT_TRUE(flashcards::FsrsScheduler::next(&state, 30, flashcards::Rating::Good, 0.9f, 36500, good));
  EXPECT_GT(again.memory.difficulty, good.memory.difficulty);
  EXPECT_LT(again.memory.stability, good.memory.stability);
  EXPECT_LT(again.intervalDaysExact, good.intervalDaysExact);
}

}  // namespace
