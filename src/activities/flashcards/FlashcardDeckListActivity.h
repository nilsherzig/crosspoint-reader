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

 protected:
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

 private:
  void openReview(size_t index, uint16_t additionalNewCards);
  void refreshDeckCounts(size_t index);
  void maybePromptBackup();
  void updateSubtitle(size_t index);

  std::vector<flashcards::DeckSummary> decks;
  std::vector<std::string> subtitles;
  std::vector<freeink::ui::ListItem> listItems;
  flashcards::Config config;
  bool countsReady = false;
};

#endif
