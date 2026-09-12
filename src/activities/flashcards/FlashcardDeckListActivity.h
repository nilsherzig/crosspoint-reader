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
  std::vector<flashcards::DeckSummary> decks;
  std::vector<std::string> subtitles;
  std::vector<freeink::ui::ListItem> listItems;
};

#endif
