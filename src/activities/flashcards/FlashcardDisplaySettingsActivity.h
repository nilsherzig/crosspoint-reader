#pragma once

#include <FlashcardStore.h>
#include <I18n.h>

#include "activities/UiListActivity.h"

class FlashcardDisplaySettingsActivity final : public UiListActivity {
  flashcards::Config config;
  freeink::ui::ListItem rows[2]{};

  int listCount() const override { return 2; }
  const char* headerTitle() const override { return tr(STR_FLASHCARD_DISPLAY_SETTINGS); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

 public:
  FlashcardDisplaySettingsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("FlashcardDisplaySettings", renderer, input) {}
  void onEnter() override;
};
