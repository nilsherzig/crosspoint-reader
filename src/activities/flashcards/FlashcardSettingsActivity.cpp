#include "FlashcardSettingsActivity.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>

#include "FlashcardBackupSettingsActivity.h"
#include "FlashcardDisplaySettingsActivity.h"
#include "FlashcardStepsActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
static constexpr StrId UNDO_OPTIONS[] = {StrId::STR_FLASHCARD_UNDO_TOUCH_AND_SIDES, StrId::STR_FLASHCARD_UNDO_TOUCH,
                                         StrId::STR_FLASHCARD_UNDO_BOTH_SIDES,      StrId::STR_FLASHCARD_UNDO_SIDE_UP,
                                         StrId::STR_FLASHCARD_UNDO_SIDE_DOWN,       StrId::STR_FLASHCARD_UNDO_DISABLED};

bool parseUint32Input(const std::string& text, uint32_t& value) {
  if (text.empty()) return false;
  uint64_t parsed = 0;
  for (const char character : text) {
    if (!std::isdigit(static_cast<unsigned char>(character))) return false;
    const uint64_t digit = static_cast<unsigned>(character - '0');
    if (parsed > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
    parsed = parsed * 10 + digit;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

void formatSteps(const flashcards::LearningSteps& steps, char* value, const size_t size) {
  size_t length = 0;
  value[length++] = '[';
  for (uint8_t i = 0; i < steps.count; ++i) {
    const int written =
        snprintf(value + length, size - length, "%s%u", i == 0 ? "" : ", ", static_cast<unsigned>(steps.minutes[i]));
    if (written < 0 || static_cast<size_t>(written) >= size - length) {
      value[0] = '\0';
      return;
    }
    length += static_cast<size_t>(written);
  }
  if (length + 1 >= size) {
    value[0] = '\0';
    return;
  }
  value[length++] = ']';
  value[length] = '\0';
}
}  // namespace

void FlashcardSettingsActivity::onEnter() {
  loadConfig();
  static constexpr StrId NAMES[] = {StrId::STR_FLASHCARD_NEW_CARDS_PER_DAY, StrId::STR_FLASHCARD_LEARN_AHEAD_LIMIT,
                                    StrId::STR_FLASHCARD_DESIRED_RETENTION, StrId::STR_FLASHCARD_MAXIMUM_INTERVAL,
                                    StrId::STR_FLASHCARD_LEARNING_STEPS,    StrId::STR_FLASHCARD_RELEARNING_STEPS,
                                    StrId::STR_FLASHCARD_UNDO_BINDING,      StrId::STR_FLASHCARD_FONT_SIZE,
                                    StrId::STR_FLASHCARD_DISPLAY_SETTINGS,  StrId::STR_FLASHCARD_BACKUP};
  static_assert(std::size(NAMES) == SETTING_COUNT);
  for (int i = 0; i < SETTING_COUNT; ++i) {
    rows[i].label = I18N.get(NAMES[i]);
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  UiListActivity::onEnter();
}

void FlashcardSettingsActivity::loadConfig() {
  std::string error;
  if (!flashcards::FlashcardStore::loadConfig(config, error)) {
    LOG_ERR("FLASH", "Could not load flashcard settings: %s", error.c_str());
  }
}

bool FlashcardSettingsActivity::saveConfig(const flashcards::Config& updated) {
  std::string error;
  if (!flashcards::FlashcardStore::saveConfig(updated, error)) {
    LOG_ERR("FLASH", "Could not save flashcard settings: %s", error.c_str());
    return false;
  }
  config = updated;
  return true;
}

void FlashcardSettingsActivity::formatValue(const Setting setting, char* value, const size_t size) const {
  value[0] = '\0';
  switch (setting) {
    case Setting::NewCardsPerDay:
      snprintf(value, size, tr(STR_FLASHCARD_CARD_COUNT), static_cast<unsigned>(config.newCardsPerDay));
      break;
    case Setting::LearnAheadLimit:
      snprintf(value, size, tr(STR_FLASHCARD_MINUTES_FORMAT), static_cast<unsigned>(config.learnAheadLimitMinutes));
      break;
    case Setting::DesiredRetention:
      snprintf(value, size, tr(STR_FLASHCARD_PERCENT_FORMAT),
               static_cast<unsigned>(config.desiredRetention * 100.0f + 0.5f));
      break;
    case Setting::MaximumInterval:
      snprintf(value, size, tr(STR_FLASHCARD_DAYS_FORMAT), static_cast<unsigned>(config.maximumIntervalDays));
      break;
    case Setting::LearningSteps:
      formatSteps(config.learningSteps, value, size);
      break;
    case Setting::RelearningSteps:
      formatSteps(config.relearningSteps, value, size);
      break;
    case Setting::UndoBinding: {
      const auto index = static_cast<size_t>(config.undoBinding);
      if (index < std::size(UNDO_OPTIONS)) snprintf(value, size, "%s", I18N.get(UNDO_OPTIONS[index]));
      break;
    }
    case Setting::FontSize:
      snprintf(value, size, tr(STR_FLASHCARD_POINT_SIZE_FORMAT), static_cast<unsigned>(config.fontPointSize));
      break;
    default:
      break;
  }
}

void FlashcardSettingsActivity::buildScreen(UiScreen& screen) {
  auto& theme = UITheme::getInstance();
  const auto& metrics = theme.getMetrics();
  const Rect safe = theme.getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < SETTING_COUNT; ++i) {
    formatValue(static_cast<Setting>(i), rowValues[i], sizeof(rowValues[i]));
    rows[i].value = rowValues[i][0] == '\0' ? nullptr : rowValues[i];
  }
  fui::ListProps props;
  props.items = rows;
  props.count = SETTING_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void FlashcardSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= SETTING_COUNT || optionPopup.isActive()) return;
  mappedInput.resetHomeButtonInput();
  app.clearTapFlash();
  const Setting setting = static_cast<Setting>(index);
  switch (setting) {
    case Setting::NewCardsPerDay:
    case Setting::DesiredRetention:
    case Setting::MaximumInterval:
      openNumericPicker(setting);
      break;
    case Setting::LearnAheadLimit:
      openLearnAheadEditor();
      break;
    case Setting::LearningSteps:
      openStepsEditor(false);
      break;
    case Setting::RelearningSteps:
      openStepsEditor(true);
      break;
    case Setting::FontSize: {
      char labels[std::size(flashcards::CARD_FONT_POINT_SIZES)][16];
      const char* options[std::size(flashcards::CARD_FONT_POINT_SIZES)];
      int selected = 0;
      for (size_t i = 0; i < std::size(flashcards::CARD_FONT_POINT_SIZES); ++i) {
        snprintf(labels[i], sizeof(labels[i]), tr(STR_FLASHCARD_POINT_SIZE_FORMAT),
                 static_cast<unsigned>(flashcards::CARD_FONT_POINT_SIZES[i]));
        options[i] = labels[i];
        if (flashcards::CARD_FONT_POINT_SIZES[i] == config.fontPointSize) selected = static_cast<int>(i);
      }
      optionPopup.show(tr(STR_FLASHCARD_FONT_SIZE), options, static_cast<int>(std::size(options)), selected,
                       [this](const int selectedIndex) {
                         flashcards::Config updated = config;
                         updated.fontPointSize = flashcards::CARD_FONT_POINT_SIZES[selectedIndex];
                         saveConfig(updated);
                       });
      requestUpdate();
      break;
    }
    case Setting::UndoBinding:
      optionPopup.show(StrId::STR_FLASHCARD_UNDO_BINDING, UNDO_OPTIONS, static_cast<int>(std::size(UNDO_OPTIONS)),
                       static_cast<int>(config.undoBinding), [this](const int selectedIndex) {
                         flashcards::Config updated = config;
                         updated.undoBinding = static_cast<flashcards::UndoBinding>(selectedIndex);
                         saveConfig(updated);
                       });
      requestUpdate();
      break;
    case Setting::Backup: {
      auto activity = makeUniqueNoThrow<FlashcardBackupSettingsActivity>(renderer, mappedInput);
      if (!activity) {
        LOG_ERR("FLASH", "OOM: flashcard backup settings");
        return;
      }
      startActivityForResult(std::move(activity), [this](const ActivityResult&) { loadConfig(); });
      break;
    }
    case Setting::Display: {
      auto activity = makeUniqueNoThrow<FlashcardDisplaySettingsActivity>(renderer, mappedInput);
      if (!activity) {
        LOG_ERR("FLASH", "OOM: flashcard display settings");
        return;
      }
      startActivityForResult(std::move(activity), [this](const ActivityResult&) { loadConfig(); });
      break;
    }
    default:
      break;
  }
}

void FlashcardSettingsActivity::openNumericPicker(const Setting setting) {
  int initialValue = 0;
  int minValue = 0;
  int maxValue = 0;
  int largeStep = 10;
  StrId titleId = StrId::STR_NONE_OPT;
  StrId valueFormatId = StrId::STR_NONE_OPT;
  switch (setting) {
    case Setting::NewCardsPerDay:
      initialValue = config.newCardsPerDay;
      maxValue = 1000;
      titleId = StrId::STR_FLASHCARD_NEW_CARDS_PER_DAY;
      valueFormatId = StrId::STR_FLASHCARD_CARD_COUNT;
      break;
    case Setting::DesiredRetention:
      initialValue = std::clamp(static_cast<int>(config.desiredRetention * 100.0f + 0.5f), 70, 99);
      minValue = 70;
      maxValue = 99;
      largeStep = 5;
      titleId = StrId::STR_FLASHCARD_DESIRED_RETENTION;
      valueFormatId = StrId::STR_FLASHCARD_PERCENT_FORMAT;
      break;
    case Setting::MaximumInterval:
      initialValue = static_cast<int>(config.maximumIntervalDays);
      minValue = 1;
      maxValue = 365000;
      largeStep = 100;
      titleId = StrId::STR_FLASHCARD_MAXIMUM_INTERVAL;
      valueFormatId = StrId::STR_FLASHCARD_DAYS_FORMAT;
      break;
    default:
      return;
  }
  auto picker =
      makeUniqueNoThrow<IntervalSelectionActivity>(renderer, mappedInput, "FlashcardNumericSetting", titleId,
                                                   initialValue, minValue, maxValue, 1, largeStep, valueFormatId);
  if (!picker) {
    LOG_ERR("FLASH", "OOM: flashcard numeric picker");
    return;
  }
  startActivityForResult(std::move(picker), [this, setting](const ActivityResult& result) {
    if (!result.isCancelled) {
      const uint32_t value = std::get<IntervalResult>(result.data).value;
      flashcards::Config updated = config;
      switch (setting) {
        case Setting::NewCardsPerDay:
          updated.newCardsPerDay = static_cast<uint16_t>(value);
          break;
        case Setting::DesiredRetention:
          updated.desiredRetention = static_cast<float>(value) / 100.0f;
          break;
        case Setting::MaximumInterval:
          updated.maximumIntervalDays = value;
          break;
        default:
          return;
      }
      saveConfig(updated);
    }
    requestUpdate();
  });
}

void FlashcardSettingsActivity::openLearnAheadEditor() {
  auto keyboard =
      makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FLASHCARD_LEARN_AHEAD_LIMIT),
                                               std::to_string(config.learnAheadLimitMinutes), 10, InputType::Numeric);
  if (!keyboard) {
    LOG_ERR("FLASH", "OOM: flashcard learn-ahead editor");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      uint32_t value = 0;
      const auto& text = std::get<KeyboardResult>(result.data).text;
      if (!parseUint32Input(text, value)) {
        LOG_ERR("FLASH", "Invalid learn-ahead limit entered");
      } else {
        flashcards::Config updated = config;
        updated.learnAheadLimitMinutes = value;
        saveConfig(updated);
      }
    }
    requestUpdate();
  });
}

void FlashcardSettingsActivity::openStepsEditor(const bool relearning) {
  auto editor = makeUniqueNoThrow<FlashcardStepsActivity>(renderer, mappedInput, config, relearning);
  if (!editor) {
    LOG_ERR("FLASH", "OOM: flashcard steps editor");
    return;
  }
  startActivityForResult(std::move(editor), [this](const ActivityResult&) { requestUpdate(); });
}

bool FlashcardSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void FlashcardSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

#endif
