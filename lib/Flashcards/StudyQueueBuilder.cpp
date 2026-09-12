#include "StudyQueueBuilder.h"

#include <algorithm>
#include <cstddef>

namespace flashcards::detail {

void buildStudyQueues(const std::vector<StudyCard>& cards, const int64_t now, const int32_t today,
                      const uint16_t introducedToday, const uint16_t newCardsPerDay,
                      std::vector<uint16_t>& dueCards, std::vector<uint16_t>& newCards) {
  dueCards.clear();
  newCards.clear();
  dueCards.reserve(cards.size());
  newCards.reserve(cards.size());

  for (uint16_t i = 0; i < cards.size(); ++i) {
    const StudyCard& card = cards[i];
    if (card.initialized) {
      if (card.due <= now) dueCards.push_back(i);
    } else if (card.introducedDay == today) {
      newCards.push_back(i);
    } else if (card.introducedDay >= 0) {
      dueCards.push_back(i);
    }
  }

  const uint16_t remaining =
      introducedToday >= newCardsPerDay ? 0 : static_cast<uint16_t>(newCardsPerDay - introducedToday);
  const size_t introducedActive = newCards.size();
  for (uint16_t i = 0; i < cards.size(); ++i) {
    if (!cards[i].initialized && cards[i].introducedDay < 0) newCards.push_back(i);
  }
  std::sort(newCards.begin() + static_cast<ptrdiff_t>(introducedActive), newCards.end(),
            [&](const uint16_t left, const uint16_t right) {
              return cards[left].sourceOrder < cards[right].sourceOrder;
            });
  newCards.resize(introducedActive + std::min<size_t>(remaining, newCards.size() - introducedActive));

  std::sort(dueCards.begin(), dueCards.end(), [&](const uint16_t left, const uint16_t right) {
    const StudyCard& a = cards[left];
    const StudyCard& b = cards[right];
    const int64_t aDue = a.initialized ? a.due : 0;
    const int64_t bDue = b.initialized ? b.due : 0;
    return aDue != bDue ? aDue < bDue : a.sourceOrder < b.sourceOrder;
  });
  std::sort(newCards.begin(), newCards.end(),
            [&](const uint16_t left, const uint16_t right) { return cards[left].sourceOrder < cards[right].sourceOrder; });
}

}  // namespace flashcards::detail
