#pragma once

#include "CardScheduler.h"

namespace flashcards::detail {

bool parseLearningSteps(char* value, LearningSteps& output);

}  // namespace flashcards::detail
