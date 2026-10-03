#pragma once

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <FlashcardStore.h>
#include <I18n.h>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class FlashcardSettingsActivity final : public UiListActivity {
  enum class Setting {
    NewCardsPerDay,
    LearnAheadLimit,
    DesiredRetention,
    MaximumInterval,
    LearningSteps,
    RelearningSteps,
    UndoBinding,
    FontSize,
    Display,
    Backup,
    Count
  };
  static constexpr int SETTING_COUNT = static_cast<int>(Setting::Count);

  flashcards::Config config;
  freeink::ui::ListItem rows[SETTING_COUNT]{};
  char rowValues[SETTING_COUNT][96]{};
  OptionPopup optionPopup;

  int listCount() const override { return SETTING_COUNT; }
  const char* headerTitle() const override { return tr(STR_FLASHCARDS); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void loadConfig();
  bool saveConfig(const flashcards::Config& updated);
  void formatValue(Setting setting, char* value, size_t size) const;
  void openNumericPicker(Setting setting);
  void openLearnAheadEditor();
  void openStepsEditor(bool relearning);

 public:
  FlashcardSettingsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("FlashcardSettings", renderer, input) {}
  void onEnter() override;
  void render(RenderLock&& lock) override;
};

#endif
