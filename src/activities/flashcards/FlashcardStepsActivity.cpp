#include "FlashcardStepsActivity.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

#include "MappedInputManager.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

FlashcardStepsActivity::FlashcardStepsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               flashcards::Config& config, const bool relearning)
    : UiListActivity(relearning ? "FlashcardRelearningSteps" : "FlashcardLearningSteps", renderer, mappedInput),
      config(config),
      steps(relearning ? config.relearningSteps : config.learningSteps),
      relearning(relearning) {}

void FlashcardStepsActivity::onEnter() {
  rebuildRows();
  UiListActivity::onEnter();
}

void FlashcardStepsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

bool FlashcardStepsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

const char* FlashcardStepsActivity::headerTitle() const {
  return I18N.get(relearning ? StrId::STR_FLASHCARD_RELEARNING_STEPS : StrId::STR_FLASHCARD_LEARNING_STEPS);
}

void FlashcardStepsActivity::rebuildRows() {
  visibleItemCount = steps.count;
  for (uint8_t i = 0; i < steps.count; ++i) {
    snprintf(rowLabels[i], sizeof(rowLabels[i]), tr(STR_FLASHCARD_STEP), static_cast<unsigned>(i + 1));
    char value[32];
    snprintf(value, sizeof(value), tr(STR_FLASHCARD_MINUTES_FORMAT), static_cast<unsigned>(steps.minutes[i]));
    rowValues[i] = value;
    rowItems[i].label = rowLabels[i];
    rowItems[i].value = rowValues[i].c_str();
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }

  const int addIndex = visibleItemCount;
  if (steps.count < flashcards::MAX_LEARNING_STEPS) {
    rowItems[addIndex].label = tr(STR_FLASHCARD_ADD_STEP);
    rowItems[addIndex].value = nullptr;
    rowItems[addIndex].actionValue = static_cast<int16_t>(addIndex);
    ++visibleItemCount;
  }
  if (steps.count > 1) {
    const int removeIndex = visibleItemCount;
    rowItems[removeIndex].label = tr(STR_FLASHCARD_REMOVE_STEP);
    rowItems[removeIndex].value = nullptr;
    rowItems[removeIndex].actionValue = static_cast<int16_t>(removeIndex);
    ++visibleItemCount;
  }
  for (int i = visibleItemCount; i < MAX_MENU_ITEMS; ++i) {
    rowItems[i] = {};
    rowValues[i].clear();
  }
}

void FlashcardStepsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (uint8_t i = 0; i < steps.count; ++i) rowItems[i].value = rowValues[i].c_str();

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(visibleItemCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void FlashcardStepsActivity::activateIndex(const int index) {
  if (index < 0 || index >= visibleItemCount || optionPopup.isActive()) return;
  nav.selected = index;
  app.clearTapFlash();

  if (index < steps.count) {
    editStep(index);
  } else if (index == steps.count && steps.count < flashcards::MAX_LEARNING_STEPS) {
    addStep();
  } else {
    removeStep();
  }
}

void FlashcardStepsActivity::editStep(const int index) {
  const int minValue = index == 0 ? 1 : static_cast<int>(steps.minutes[index - 1]) + 1;
  const int maxValue = index + 1 < steps.count ? static_cast<int>(steps.minutes[index + 1]) - 1 : 10080;
  if (minValue > maxValue) {
    LOG_ERR("FLASH", "No valid range for flashcard step %d", index + 1);
    return;
  }

  auto picker = makeUniqueNoThrow<IntervalSelectionActivity>(
      renderer, mappedInput, "FlashcardStep", StrId::STR_FLASHCARD_STEP_VALUE, steps.minutes[index], minValue,
      maxValue, 1, 10, StrId::STR_FLASHCARD_MINUTES_FORMAT);
  if (!picker) {
    LOG_ERR("FLASH", "OOM: flashcard step picker");
    return;
  }
  startActivityForResult(std::move(picker), [this, index](const ActivityResult& result) {
    if (!result.isCancelled) {
      const uint16_t oldValue = steps.minutes[index];
      steps.minutes[index] = static_cast<uint16_t>(std::get<IntervalResult>(result.data).value);
      if (!persistSteps()) steps.minutes[index] = oldValue;
    }
    rebuildRows();
    requestUpdate();
  });
}

void FlashcardStepsActivity::addStep() {
  if (steps.count == 0 || steps.count >= flashcards::MAX_LEARNING_STEPS || steps.minutes[steps.count - 1] >= 10080) {
    return;
  }
  const flashcards::LearningSteps previous = steps;
  const uint8_t oldCount = steps.count;
  steps.minutes[oldCount] = static_cast<uint16_t>(steps.minutes[oldCount - 1] + 1);
  steps.count = static_cast<uint8_t>(oldCount + 1);
  if (!persistSteps()) steps = previous;
  rebuildRows();
  requestUpdate();
}

void FlashcardStepsActivity::removeStep() {
  if (steps.count <= 1) return;

  std::vector<std::string> options;
  options.reserve(steps.count);
  for (uint8_t i = 0; i < steps.count; ++i) {
    char label[32];
    snprintf(label, sizeof(label), tr(STR_FLASHCARD_STEP), static_cast<unsigned>(i + 1));
    options.emplace_back(label);
  }
  optionPopup.show(StrId::STR_FLASHCARD_REMOVE_STEP, options, steps.count - 1, [this](const int index) {
    if (index < 0 || index >= steps.count || steps.count <= 1) return;
    const flashcards::LearningSteps previous = steps;
    for (uint8_t i = static_cast<uint8_t>(index); i + 1 < steps.count; ++i) {
      steps.minutes[i] = steps.minutes[i + 1];
    }
    --steps.count;
    if (!persistSteps()) steps = previous;
    rebuildRows();
    requestUpdate();
  });
  requestUpdate();
}

bool FlashcardStepsActivity::persistSteps() {
  flashcards::Config updated = config;
  if (relearning) {
    updated.relearningSteps = steps;
  } else {
    updated.learningSteps = steps;
  }
  std::string error;
  if (!flashcards::FlashcardStore::saveConfig(updated, error)) {
    LOG_ERR("FLASH", "Could not save flashcard settings: %s", error.c_str());
    return false;
  }
  config = updated;
  return true;
}

#endif
