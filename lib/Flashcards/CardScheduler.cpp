#include "CardScheduler.h"

#include "SchedulingFuzzer.h"

namespace flashcards {
namespace {
constexpr uint64_t LEARNING_FUZZ_SALT = 0xB43A2D7E59C18F61ULL;
constexpr uint64_t REVIEW_FUZZ_SALT = 0x6C8E9CF570932BD5ULL;

bool validSteps(const LearningSteps* steps) {
  return steps && steps->count > 0 && steps->count <= MAX_LEARNING_STEPS;
}

void scheduleLearningStep(const LearningSteps& steps, const uint8_t index, const CardPhase phase, const uint64_t seed,
                          CardSchedulingResult& result) {
  result.phase = phase;
  result.learningStep = index;
  result.intervalDays = 0;
  const uint32_t seconds = static_cast<uint32_t>(steps.minutes[index]) * 60U;
  result.learningDelaySeconds = SchedulingFuzzer::fuzzLearningDelay(seconds, seed ^ LEARNING_FUZZ_SALT);
}

void scheduleReview(const SchedulingResult& fsrs, const CardSchedulingOptions& options, const uint64_t seed,
                    CardSchedulingResult& result) {
  result.phase = CardPhase::Review;
  result.learningStep = 0;
  result.learningDelaySeconds = 0;
  result.intervalDays = SchedulingFuzzer::fuzzReviewInterval(fsrs.intervalDaysExact, 1,
                                                             options.maximumIntervalDays, seed ^ REVIEW_FUZZ_SALT);
}

}  // namespace

bool CardScheduler::next(const CardSchedulingState& current, const Rating rating,
                         const CardSchedulingOptions& options, const uint64_t fuzzSeed,
                         CardSchedulingResult& result) {
  if (!validSteps(options.learningSteps) || !validSteps(options.relearningSteps)) return false;
  if (current.phase < CardPhase::New || current.phase > CardPhase::Relearning) return false;
  if (current.phase != CardPhase::New && !current.initialized) return false;

  SchedulingResult fsrs;
  const MemoryState* memory = current.initialized ? &current.memory : nullptr;
  if (!FsrsScheduler::next(memory, current.elapsedDays, rating, options.desiredRetention,
                           options.maximumIntervalDays, fsrs)) {
    return false;
  }
  result.memory = fsrs.memory;

  switch (current.phase) {
    case CardPhase::New:
      if (rating == Rating::Again) {
        scheduleLearningStep(*options.learningSteps, 0, CardPhase::Learning, fuzzSeed, result);
      } else if (options.learningSteps->count > 1) {
        scheduleLearningStep(*options.learningSteps, 1, CardPhase::Learning, fuzzSeed, result);
      } else {
        scheduleReview(fsrs, options, fuzzSeed, result);
      }
      return true;

    case CardPhase::Learning:
      if (rating == Rating::Again) {
        scheduleLearningStep(*options.learningSteps, 0, CardPhase::Learning, fuzzSeed, result);
      } else if (current.learningStep + 1 < options.learningSteps->count) {
        scheduleLearningStep(*options.learningSteps, current.learningStep + 1, CardPhase::Learning, fuzzSeed, result);
      } else {
        scheduleReview(fsrs, options, fuzzSeed, result);
      }
      return true;

    case CardPhase::Review:
      if (rating == Rating::Again) {
        scheduleLearningStep(*options.relearningSteps, 0, CardPhase::Relearning, fuzzSeed, result);
      } else {
        scheduleReview(fsrs, options, fuzzSeed, result);
      }
      return true;

    case CardPhase::Relearning:
      if (rating == Rating::Again) {
        scheduleLearningStep(*options.relearningSteps, 0, CardPhase::Relearning, fuzzSeed, result);
      } else if (current.learningStep + 1 < options.relearningSteps->count) {
        scheduleLearningStep(*options.relearningSteps, current.learningStep + 1, CardPhase::Relearning, fuzzSeed,
                             result);
      } else {
        scheduleReview(fsrs, options, fuzzSeed, result);
      }
      return true;
  }
  return false;
}

}  // namespace flashcards
