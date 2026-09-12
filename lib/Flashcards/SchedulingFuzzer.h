#pragma once

#include <cstdint>

namespace flashcards {

struct FuzzBounds {
  uint32_t lower = 1;
  uint32_t upper = 1;
};

class SchedulingFuzzer {
 public:
  static FuzzBounds reviewBounds(float intervalDays, uint32_t minimumDays, uint32_t maximumDays);
  static uint32_t fuzzReviewInterval(float intervalDays, uint32_t minimumDays, uint32_t maximumDays, uint64_t seed);
  static uint32_t fuzzLearningDelay(uint32_t delaySeconds, uint64_t seed);
};

}  // namespace flashcards
