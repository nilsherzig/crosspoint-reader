#pragma once

#include <algorithm>
#include <cstdint>

namespace flashcards::detail {

struct CardTextLayout {
  int16_t questionHeight = 0;
  int16_t gap = 0;
  int16_t answerHeight = 0;
};

inline uint8_t cardTextLines(const int height, const int lineHeight) {
  return lineHeight > 0 ? static_cast<uint8_t>(std::clamp(height / lineHeight, 0, 16)) : 0;
}

// Keep both sections inside the body and reserve at least one whole line for
// each side. A short question gives its unused space to the answer.
inline CardTextLayout cardTextLayout(const int bodyHeight, const int preferredGap, const int lineHeight,
                                     const int measuredQuestionHeight) {
  if (lineHeight <= 0 || bodyHeight < lineHeight) return {};
  if (bodyHeight < 2 * lineHeight) return {static_cast<int16_t>(bodyHeight), 0, 0};
  const int gap = std::clamp(preferredGap, 0, bodyHeight - 2 * lineHeight);
  const int available = bodyHeight - gap;
  const int questionHeight = std::clamp(measuredQuestionHeight, lineHeight, available / 2);
  return {static_cast<int16_t>(questionHeight), static_cast<int16_t>(gap),
          static_cast<int16_t>(available - questionHeight)};
}

}  // namespace flashcards::detail
