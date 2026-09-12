#include "FsrsScheduler.h"

#include <algorithm>
#include <cmath>

namespace flashcards {
namespace {
// Default FSRS-6 parameters from fsrs-rs 5.2.0. The scheduling equations are
// adapted from that BSD-3-Clause project; see FSRS-LICENSE.txt.
constexpr float W[] = {0.2172f, 1.1771f, 3.2602f, 16.1507f, 7.0114f, 0.57f,   2.0966f,
                       0.0069f, 1.5261f, 0.112f,  1.0178f,  1.849f,  0.1133f, 0.3127f,
                       2.2934f, 0.2191f, 3.0004f, 0.7536f,  0.3332f, 0.1437f, 0.2f};
constexpr float DECAY = -W[20];
constexpr float FACTOR = 0.6935088f;  // pow(0.9, 1 / DECAY) - 1
constexpr float MIN_STABILITY = 0.001f;
constexpr float MAX_STABILITY = 36500.0f;

float clampDifficulty(const float difficulty) { return std::clamp(difficulty, 1.0f, 10.0f); }

float initialStability(const Rating rating) {
  const size_t index = static_cast<size_t>(rating) - 1;
  return std::clamp(W[index], MIN_STABILITY, MAX_STABILITY);
}

float initialDifficultyValue(const float rating) {
  const float value = W[4] - std::exp(W[5] * (rating - 1.0f)) + 1.0f;
  return clampDifficulty(value);
}

float initialDifficulty(const Rating rating) { return initialDifficultyValue(static_cast<float>(rating)); }

float retrievability(const float elapsedDays, const float stability) {
  return std::pow(1.0f + FACTOR * elapsedDays / stability, DECAY);
}

float nextDifficulty(const float difficulty, const Rating rating) {
  const float delta = -W[6] * (static_cast<float>(rating) - 3.0f);
  const float dampened = (10.0f - difficulty) * delta / 9.0f;
  const float changed = difficulty + dampened;
  return clampDifficulty(W[7] * initialDifficultyValue(4.0f) + (1.0f - W[7]) * changed);
}

float nextRecallStability(const float difficulty, const float stability, const float retrievabilityValue) {
  return stability * (1.0f + std::exp(W[8]) * (11.0f - difficulty) * std::pow(stability, -W[9]) *
                                 (std::exp((1.0f - retrievabilityValue) * W[10]) - 1.0f));
}

float nextForgetStability(const float difficulty, const float stability, const float retrievabilityValue) {
  const float longTerm = W[11] * std::pow(difficulty, -W[12]) * (std::pow(stability + 1.0f, W[13]) - 1.0f) *
                         std::exp((1.0f - retrievabilityValue) * W[14]);
  const float minimumAfterLapse = stability / std::exp(W[17] * W[18]);
  return std::min(longTerm, minimumAfterLapse);
}

float nextShortTermStability(const float stability, const Rating rating) {
  const float increase = std::exp(W[17] * (static_cast<float>(rating) - 3.0f + W[18])) * std::pow(stability, -W[19]);
  return stability * (rating >= Rating::Good ? std::max(increase, 1.0f) : increase);
}

uint32_t intervalFor(const float stability, const float retention, const uint32_t maximumIntervalDays) {
  const float days = stability / FACTOR * (std::pow(retention, 1.0f / DECAY) - 1.0f);
  const float bounded = std::clamp(days, 1.0f, static_cast<float>(maximumIntervalDays));
  return static_cast<uint32_t>(std::floor(bounded + 0.5f));
}
}  // namespace

bool FsrsScheduler::next(const MemoryState* current, const uint32_t elapsedDays, const Rating rating,
                         const float desiredRetention, const uint32_t maximumIntervalDays, SchedulingResult& result) {
  if ((rating != Rating::Again && rating != Rating::Good) || desiredRetention <= 0.0f || desiredRetention >= 1.0f ||
      maximumIntervalDays == 0) {
    return false;
  }

  if (current &&
      (!std::isfinite(current->stability) || current->stability <= 0.0f || !std::isfinite(current->difficulty))) {
    return false;
  }
  if (!current) {
    result.memory = MemoryState{initialStability(rating), initialDifficulty(rating)};
  } else {
    const float stability = std::clamp(current->stability, MIN_STABILITY, MAX_STABILITY);
    const float difficulty = clampDifficulty(current->difficulty);
    if (elapsedDays == 0) {
      result.memory = MemoryState{nextShortTermStability(stability, rating), nextDifficulty(difficulty, rating)};
    } else {
      const float recall = retrievability(static_cast<float>(elapsedDays), stability);
      result.memory.stability = rating == Rating::Again ? nextForgetStability(difficulty, stability, recall)
                                                        : nextRecallStability(difficulty, stability, recall);
      result.memory.difficulty = nextDifficulty(difficulty, rating);
    }
  }

  if (!std::isfinite(result.memory.stability) || !std::isfinite(result.memory.difficulty)) return false;
  result.memory.stability = std::clamp(result.memory.stability, MIN_STABILITY, MAX_STABILITY);
  result.memory.difficulty = clampDifficulty(result.memory.difficulty);
  result.intervalDays = intervalFor(result.memory.stability, desiredRetention, maximumIntervalDays);
  return true;
}

}  // namespace flashcards
