#pragma once

#include <BoardConfig.h>

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <FlashcardStore.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/UiAppHost.h"

class FlashcardReviewActivity final : public Activity, private UiAppHost {
 public:
  FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, flashcards::DeckSummary deck,
                          uint16_t additionalNewCards = 0);
  FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, flashcards::DeckSummary deck,
                          flashcards::Config config, flashcards::StudyQueue queue, uint16_t additionalNewCards);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Phase { Due, New, Waiting, Complete, Error };
  enum class ErrorKind { None, Clock, Config, Deck, Save };

  static constexpr freeink::ui::ActionId ACTION_REVEAL = 1;
  static constexpr freeink::ui::ActionId ACTION_AGAIN = 2;
  static constexpr freeink::ui::ActionId ACTION_GOOD = 3;
  static constexpr freeink::ui::ActionId ACTION_DONE = 4;

  static void screenTrampoline(UiScreen& screen, void* user);
  static void actionTrampoline(const freeink::ui::ActionEvent& event, void* user);

  void buildScreen(UiScreen& screen);
  void handleAction(freeink::ui::ActionId action);
  void reveal();
  void rate(flashcards::Rating rating);
  void advance();
  void finishSession();
  void undo();
  bool undoTouchEnabled() const;
  bool undoSideEnabled(MappedInputManager::Button button) const;
  bool loadCurrentCard();
  bool loadCard(int64_t now);
  void showWaiting(int64_t now);
  void showError(ErrorKind kind, const std::string& detail);
  std::vector<uint16_t>& currentPhaseQueue();
  size_t& currentPhasePosition();
  const char* phaseName() const;
  const char* errorText() const;

  flashcards::DeckSummary deck;
  flashcards::Config config;
  flashcards::StudyQueue queue;
  std::vector<uint16_t> pendingLearningCards;
  Phase phase = Phase::Due;
  ErrorKind errorKind = ErrorKind::None;
  size_t duePosition = 0;
  size_t newPosition = 0;
  size_t currentPendingPosition = 0;
  int64_t waitingUntil = 0;
  uint32_t lastDueCheck = 0;
  uint16_t currentCardIndex = 0;
  uint16_t additionalNewCards = 0;
  int32_t currentUtcDay = -1;
  bool currentFromPending = false;
  bool answerShown = false;
  bool backupTrackingReady = false;
  bool preparedQueue = false;
  struct LastRating {
    flashcards::StudyCard card;
    Phase phase = Phase::Due;
    size_t duePosition = 0;
    size_t newPosition = 0;
    size_t pendingPosition = 0;
    uint16_t cardIndex = 0;
    bool fromPending = false;
    bool valid = false;
  } lastRating;
  uint32_t undoIndicatorUntil = 0;
  std::string frontText;
  std::string backText;
  char progressText[32]{};
  char waitingText[64]{};
};

#endif
