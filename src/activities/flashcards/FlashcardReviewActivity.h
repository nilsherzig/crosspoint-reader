#pragma once

#include <BoardConfig.h>

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <FlashcardStore.h>

#include <cstddef>
#include <string>

#include "activities/Activity.h"
#include "components/UiAppHost.h"

class FlashcardReviewActivity final : public Activity, private UiAppHost {
 public:
  FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, flashcards::DeckSummary deck);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Phase { Due, New, Complete, Error };
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
  void advance(bool repeat);
  bool loadCurrentCard();
  void showError(ErrorKind kind, const std::string& detail);
  std::vector<uint16_t>& currentPhaseQueue();
  const char* phaseName() const;
  const char* errorText() const;

  flashcards::DeckSummary deck;
  flashcards::Config config;
  flashcards::StudyQueue queue;
  Phase phase = Phase::Due;
  ErrorKind errorKind = ErrorKind::None;
  size_t phasePosition = 0;
  uint16_t currentCardIndex = 0;
  bool answerShown = false;
  std::string frontText;
  std::string backText;
  char progressText[32]{};
};

#endif
