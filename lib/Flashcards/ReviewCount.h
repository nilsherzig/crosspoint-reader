#pragma once

#include <cstdint>

namespace flashcards::detail {

enum class ReviewCountEvent : uint8_t { Review, Undo };

constexpr uint32_t countAfterReviewEvent(const uint32_t count, const ReviewCountEvent event) {
  if (event == ReviewCountEvent::Undo) return count > 0 ? count - 1 : 0;
  return count < UINT32_MAX ? count + 1 : UINT32_MAX;
}

}  // namespace flashcards::detail
