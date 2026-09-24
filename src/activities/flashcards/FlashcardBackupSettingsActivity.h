#pragma once

#include <BoardConfig.h>

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <FlashcardStore.h>

#include "activities/UiListActivity.h"

class FlashcardBackupSettingsActivity final : public UiListActivity {
  flashcards::Config config;
  freeink::ui::ListItem rows[6]{};
  char intervalText[24]{};
  bool showSaveError = false;

  int listCount() const override { return 6; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawFooter() override;
  bool save(const flashcards::Config& updated);
  void editText(int index);

 public:
  FlashcardBackupSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("FlashcardBackupSettings", renderer, mappedInput) {}
  void onEnter() override;
};

#endif
