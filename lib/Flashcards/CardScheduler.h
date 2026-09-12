#pragma once

#include <array>
#include <cstdint>

#include "FsrsScheduler.h"

namespace flashcards {

constexpr uint8_t MAX_LEARNING_STEPS = 8;

enum class CardPhase : uint8_t { New = 1, Learning = 2, Review = 3, Relearning = 4 };

struct LearningSteps {
  std::array<uint16_t, MAX_LEARNING_STEPS> minutes{};
  uint8_t count = 0;
};

struct CardSchedulingState {
  MemoryState memory;
  CardPhase phase = CardPhase::New;
  uint8_t learningStep = 0;
  uint32_t elapsedDays = 0;
  bool initialized = false;
};

struct CardSchedulingOptions {
  float desiredRetention = 0.90f;
  uint32_t maximumIntervalDays = 36500;
  const LearningSteps* learningSteps = nullptr;
  const LearningSteps* relearningSteps = nullptr;
};

struct CardSchedulingResult {
  MemoryState memory;
  CardPhase phase = CardPhase::Review;
  uint8_t learningStep = 0;
  uint32_t intervalDays = 0;
  uint32_t learningDelaySeconds = 0;
};

class CardScheduler {
 public:
  static bool next(const CardSchedulingState& current, Rating rating, const CardSchedulingOptions& options,
                   uint64_t fuzzSeed, CardSchedulingResult& result);
};

}  // namespace flashcards
