#include "SchedulingFuzzer.h"

#include <algorithm>
#include <cmath>

namespace flashcards {
namespace {

uint64_t mix(uint64_t value) {
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31);
}

uint32_t selectInclusive(const uint32_t lower, const uint32_t upper, const uint64_t seed) {
  const uint64_t count = static_cast<uint64_t>(upper) - lower + 1;
  return lower + static_cast<uint32_t>(mix(seed) % count);
}

float reviewDelta(const float intervalDays) {
  if (intervalDays < 2.5f) return 0.0f;
  float delta = 1.0f;
  delta += 0.15f * std::max(0.0f, std::min(intervalDays, 7.0f) - 2.5f);
  delta += 0.10f * std::max(0.0f, std::min(intervalDays, 20.0f) - 7.0f);
  delta += 0.05f * std::max(0.0f, intervalDays - 20.0f);
  return delta;
}

}  // namespace

FuzzBounds SchedulingFuzzer::reviewBounds(const float intervalDays, const uint32_t minimumDays,
                                           const uint32_t maximumDays) {
  const uint32_t minimum = std::min(minimumDays, maximumDays);
  const float bounded = std::clamp(intervalDays, static_cast<float>(minimum), static_cast<float>(maximumDays));
  const float delta = reviewDelta(bounded);
  uint32_t lower = static_cast<uint32_t>(std::round(bounded - delta));
  uint32_t upper = static_cast<uint32_t>(std::round(bounded + delta));
  lower = std::clamp(lower, minimum, maximumDays);
  upper = std::clamp(upper, minimum, maximumDays);
  if (upper == lower && upper > 2 && upper < maximumDays) ++upper;
  return FuzzBounds{lower, upper};
}

uint32_t SchedulingFuzzer::fuzzReviewInterval(const float intervalDays, const uint32_t minimumDays,
                                               const uint32_t maximumDays, const uint64_t seed) {
  const FuzzBounds bounds = reviewBounds(intervalDays, minimumDays, maximumDays);
  return selectInclusive(bounds.lower, bounds.upper, seed);
}

uint32_t SchedulingFuzzer::fuzzLearningDelay(const uint32_t delaySeconds, const uint64_t seed) {
  const uint32_t extraRange = std::min<uint32_t>(delaySeconds / 4, 300);
  if (extraRange == 0) return delaySeconds;
  return delaySeconds + selectInclusive(0, extraRange - 1, seed);
}

}  // namespace flashcards
