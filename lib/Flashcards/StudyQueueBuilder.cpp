#include "StudyQueueBuilder.h"

#include <algorithm>
#include <cstddef>

namespace flashcards::detail {

bool learningCardPending(const StudyCard& card, const int64_t now) {
  return card.initialized && card.due > now &&
         (card.phase == CardPhase::Learning || card.phase == CardPhase::Relearning);
}

bool learningCardReady(const int64_t due, const int64_t now, const uint32_t learnAheadLimitMinutes,
                       const bool baseCardsRemaining) {
  return due <= now || (!baseCardsRemaining && due - now < static_cast<int64_t>(learnAheadLimitMinutes) * 60);
}

size_t nextLearningCardPosition(const std::vector<StudyCard>& cards, const std::vector<uint16_t>& pendingLearningCards,
                                const int64_t now, const uint32_t learnAheadLimitMinutes,
                                const bool baseCardsRemaining) {
  size_t earliestDuePosition = pendingLearningCards.size();
  size_t firstLearnAheadPosition = pendingLearningCards.size();
  int64_t earliestDue = INT64_MAX;
  for (size_t i = 0; i < pendingLearningCards.size(); ++i) {
    const int64_t due = cards[pendingLearningCards[i]].due;
    if (due <= now) {
      if (due < earliestDue) {
        earliestDue = due;
        earliestDuePosition = i;
      }
    } else if (firstLearnAheadPosition == pendingLearningCards.size() &&
               learningCardReady(due, now, learnAheadLimitMinutes, baseCardsRemaining)) {
      firstLearnAheadPosition = i;
    }
  }
  return earliestDuePosition < pendingLearningCards.size() ? earliestDuePosition : firstLearnAheadPosition;
}

uint16_t buildStudyQueues(const std::vector<StudyCard>& cards, const int64_t now, const int32_t today,
                          const uint16_t introducedToday, const uint16_t newCardsPerDay,
                          const uint16_t additionalNewCards, std::vector<uint16_t>& dueCards,
                          std::vector<uint16_t>& newCards) {
  dueCards.clear();
  newCards.clear();
  dueCards.reserve(cards.size());
  newCards.reserve(cards.size());

  for (uint16_t i = 0; i < cards.size(); ++i) {
    const StudyCard& card = cards[i];
    if (card.initialized) {
      if (card.due <= now || learningCardPending(card, now)) dueCards.push_back(i);
    } else if (card.introducedDay == today) {
      newCards.push_back(i);
    } else if (card.introducedDay >= 0) {
      dueCards.push_back(i);
    }
  }

  const size_t remainingFromDailyLimit =
      introducedToday >= newCardsPerDay ? 0 : static_cast<size_t>(newCardsPerDay - introducedToday);
  const size_t introducedActive = newCards.size();
  for (uint16_t i = 0; i < cards.size(); ++i) {
    if (!cards[i].initialized && cards[i].introducedDay < 0) newCards.push_back(i);
  }
  const size_t unseenCount = newCards.size() - introducedActive;
  std::sort(
      newCards.begin() + static_cast<ptrdiff_t>(introducedActive), newCards.end(),
      [&](const uint16_t left, const uint16_t right) { return cards[left].sourceOrder < cards[right].sourceOrder; });
  const size_t selectedUnseen = std::min(unseenCount, remainingFromDailyLimit + additionalNewCards);
  newCards.resize(introducedActive + selectedUnseen);

  std::sort(dueCards.begin(), dueCards.end(), [&](const uint16_t left, const uint16_t right) {
    const StudyCard& a = cards[left];
    const StudyCard& b = cards[right];
    const int64_t aDue = a.initialized ? a.due : 0;
    const int64_t bDue = b.initialized ? b.due : 0;
    return aDue != bDue ? aDue < bDue : a.sourceOrder < b.sourceOrder;
  });
  std::sort(newCards.begin(), newCards.end(), [&](const uint16_t left, const uint16_t right) {
    return cards[left].sourceOrder < cards[right].sourceOrder;
  });
  return static_cast<uint16_t>(unseenCount);
}

}  // namespace flashcards::detail
