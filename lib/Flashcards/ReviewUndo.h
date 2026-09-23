#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace flashcards::detail {

// Reverse the pending-queue change made by a rating. The caller retains the
// original position and whether the rated card returned to learning.
inline void restorePendingAfterUndo(std::vector<uint16_t>& pending, const uint16_t cardIndex,
                                    const size_t previousPosition, const bool wasPending,
                                    const bool returnedToPending) {
  if (returnedToPending) pending.pop_back();
  if (wasPending) pending.insert(pending.begin() + static_cast<ptrdiff_t>(previousPosition), cardIndex);
}

}  // namespace flashcards::detail
