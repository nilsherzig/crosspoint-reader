#pragma once

#include <cstdint>
#include <vector>

#include "FlashcardStore.h"

namespace flashcards::detail {

uint16_t buildStudyQueues(const std::vector<StudyCard>& cards, int64_t now, int32_t today, uint16_t introducedToday,
                          uint16_t newCardsPerDay, uint16_t additionalNewCards, std::vector<uint16_t>& dueCards,
                          std::vector<uint16_t>& newCards);

}  // namespace flashcards::detail
