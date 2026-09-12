#include "FlashcardConfigParser.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace flashcards::detail {
namespace {

char* trim(char* value) {
  while (*value && std::isspace(static_cast<unsigned char>(*value))) ++value;
  char* end = value + strlen(value);
  while (end > value && std::isspace(static_cast<unsigned char>(end[-1]))) --end;
  *end = '\0';
  return value;
}

}  // namespace

bool parseLearningSteps(char* value, LearningSteps& output) {
  char* current = trim(value);
  if (*current++ != '[') return false;

  LearningSteps parsed;
  while (true) {
    while (std::isspace(static_cast<unsigned char>(*current))) ++current;
    if (*current == ']') {
      ++current;
      if (parsed.count == 0 || *trim(current) != '\0') return false;
      output = parsed;
      return true;
    }
    if (parsed.count >= MAX_LEARNING_STEPS || !std::isdigit(static_cast<unsigned char>(*current))) return false;

    errno = 0;
    char* end = nullptr;
    const unsigned long minutes = strtoul(current, &end, 10);
    if (errno != 0 || end == current || minutes < 1 || minutes > 10080 ||
        (parsed.count > 0 && minutes <= parsed.minutes[parsed.count - 1])) {
      return false;
    }
    parsed.minutes[parsed.count++] = static_cast<uint16_t>(minutes);
    current = end;
    while (std::isspace(static_cast<unsigned char>(*current))) ++current;
    if (*current == ',') {
      ++current;
    } else if (*current != ']') {
      return false;
    }
  }
}

}  // namespace flashcards::detail
