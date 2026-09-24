#pragma once

#include <BoardConfig.h>

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <FlashcardStore.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class FlashcardDeckListActivity final : public UiListActivity {
 public:
  FlashcardDeckListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;

 protected:
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;

 private:
  void openReview(size_t index, uint16_t additionalNewCards);
  void maybePromptBackup();
  void updateSubtitle(size_t index);
  size_t nextDeckToCount = 0;

  std::vector<flashcards::DeckSummary> decks;
  std::vector<std::string> subtitles;
  std::vector<freeink::ui::ListItem> listItems;
  flashcards::Config config;
  flashcards::StudyQueue preparedQueue;
  bool hasPreparedQueue = false;
  bool countsReady = false;
};

#endif
