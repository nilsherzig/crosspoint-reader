#pragma once

#include <cstdint>

namespace flashcards::detail {

// UTC day since Unix epoch, or -1 when a projection is not meaningful.
// Count distinct cards introduced in the current deck, not repeat reviews.
inline int64_t projectedIntroductionDay(const int32_t firstReviewDay, const int32_t today, const uint32_t totalCards,
                                        const uint32_t unseenCards, const uint32_t effectiveReviews) {
  if (firstReviewDay < 0 || today < firstReviewDay || effectiveReviews == 0 || unseenCards == 0 ||
      unseenCards >= totalCards) {
    return -1;
  }
  const uint32_t introduced = totalCards - unseenCards;
  const uint64_t elapsedDays = static_cast<uint64_t>(today - firstReviewDay) + 1;
  const uint64_t remainingDays = (static_cast<uint64_t>(unseenCards) * elapsedDays + introduced - 1) / introduced;
  return static_cast<int64_t>(today) + static_cast<int64_t>(remainingDays);
}

}  // namespace flashcards::detail
