#pragma once

#include <cstdint>
#include <vector>

#include "FlashcardStore.h"

namespace flashcards::detail {

bool learningCardReady(int64_t due, int64_t now, uint32_t learnAheadLimitMinutes, bool baseCardsRemaining);

uint16_t buildStudyQueues(const std::vector<StudyCard>& cards, int64_t now, int32_t today, uint16_t introducedToday,
                          uint16_t newCardsPerDay, uint16_t additionalNewCards, std::vector<uint16_t>& dueCards,
                          std::vector<uint16_t>& newCards);

}  // namespace flashcards::detail
