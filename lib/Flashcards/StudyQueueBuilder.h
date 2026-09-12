#pragma once

#include "FlashcardStore.h"

#include <cstdint>
#include <vector>

namespace flashcards::detail {

void buildStudyQueues(const std::vector<StudyCard>& cards, int64_t now, int32_t today, uint16_t introducedToday,
                      uint16_t newCardsPerDay, std::vector<uint16_t>& dueCards, std::vector<uint16_t>& newCards);

}  // namespace flashcards::detail
