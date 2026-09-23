#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CardScheduler.h"

namespace flashcards {

enum class UndoBinding : uint8_t { TouchAndSides, Touch, BothSides, SideUp, SideDown, Disabled };

inline constexpr uint8_t CARD_FONT_POINT_SIZES[] = {12, 14, 16, 18};

constexpr bool validCardFontPointSize(const uint8_t size) {
  for (const uint8_t supported : CARD_FONT_POINT_SIZES) {
    if (size == supported) return true;
  }
  return false;
}

struct Fingerprint {
  uint64_t first = 0;
  uint64_t second = 0;

  bool operator==(const Fingerprint& other) const { return first == other.first && second == other.second; }
  bool operator<(const Fingerprint& other) const {
    return first < other.first || (first == other.first && second < other.second);
  }
};

struct Config {
  uint16_t newCardsPerDay = 20;
  uint32_t learnAheadLimitMinutes = 20;
  float desiredRetention = 0.90f;
  uint32_t maximumIntervalDays = 36500;
  LearningSteps learningSteps{{1, 10}, 2};
  LearningSteps relearningSteps{{10}, 1};
  UndoBinding undoBinding = UndoBinding::TouchAndSides;
  uint8_t fontPointSize = 12;
  bool showForecast = false;
};

struct DeckSummary {
  std::string sourcePath;
  std::string name;
  std::string error;
  uint64_t key = 0;
  uint32_t cardCount = 0;
  uint16_t dueCount = 0;
  uint16_t newCount = 0;
  uint16_t unseenCount = 0;
  bool countsAvailable = false;

  bool valid() const { return error.empty(); }
};

struct StudyCard {
  Fingerprint fingerprint;
  uint32_t sourceOrder = 0;
  uint32_t frontOffset = 0;
  uint32_t frontLength = 0;
  uint32_t backOffset = 0;
  uint32_t backLength = 0;
  MemoryState memory;
  int64_t lastReview = 0;
  int64_t due = 0;
  int32_t introducedDay = -1;
  CardPhase phase = CardPhase::New;
  uint8_t learningStep = 0;
  bool initialized = false;
};

struct StudyQueue {
  std::vector<StudyCard> cards;
  std::vector<uint16_t> dueCards;
  std::vector<uint16_t> newCards;
  uint16_t introducedToday = 0;
  uint16_t unseenCount = 0;
  uint32_t reviewCount = 0;
  int32_t firstReviewDay = -1;
};

class FlashcardStore {
 public:
  static constexpr const char* SOURCE_DIRECTORY = "/flashcards";
  static constexpr const char* CONFIG_PATH = "/flashcards/config.toml";
  static constexpr uint16_t MAX_DECKS = 32;
  static constexpr uint16_t MAX_CARDS_PER_DECK = 2000;

  static bool loadConfig(Config& config, std::string& error);
  static bool saveConfig(const Config& config, std::string& error);
  static bool scanDecks(std::vector<DeckSummary>& decks);
  static bool loadStudyQueue(const DeckSummary& deck, int64_t now, const Config& config, StudyQueue& queue,
                             std::string& error, uint16_t additionalNewCards = 0);
  static bool readCardText(const DeckSummary& deck, const StudyCard& card, bool back, std::string& text,
                           std::string& error);
  static bool introduceCard(const DeckSummary& deck, StudyCard& card, int64_t now, std::string& error);
  static bool reviewCard(const DeckSummary& deck, StudyCard& card, int64_t now, Rating rating, const Config& config,
                         std::string& error);
  static bool undoReview(const DeckSummary& deck, StudyCard& card, const StudyCard& previous, std::string& error);

  static Fingerprint fingerprint(const std::string& front, const std::string& back);
};

}  // namespace flashcards
