#pragma once

#include <BoardConfig.h>

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <FlashcardStore.h>

#include <string>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class FlashcardStepsActivity final : public UiListActivity {
 public:
  FlashcardStepsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, flashcards::Config& config,
                         bool relearning);

  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr int MAX_MENU_ITEMS = flashcards::MAX_LEARNING_STEPS + 2;

  flashcards::Config& config;
  flashcards::LearningSteps steps;
  bool relearning;
  int visibleItemCount = 0;
  OptionPopup optionPopup;

  char rowLabels[MAX_MENU_ITEMS][24]{};
  std::string rowValues[MAX_MENU_ITEMS];
  freeink::ui::ListItem rowItems[MAX_MENU_ITEMS]{};

  int listCount() const override { return visibleItemCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;

  void rebuildRows();
  void editStep(int index);
  void addStep();
  void removeStep();
  bool persistSteps();
};

#endif
