#include "FlashcardDisplaySettingsActivity.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <I18n.h>
#include <Logging.h>

#include "components/UITheme.h"

namespace fui = freeink::ui;

void FlashcardDisplaySettingsActivity::onEnter() {
  UiListActivity::onEnter();
  std::string error;
  if (!flashcards::FlashcardStore::loadConfig(config, error)) {
    LOG_ERR("FLASH", "Could not load display settings: %s", error.c_str());
  }
}

void FlashcardDisplaySettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  flashcards::Config updated = config;
  if (index == 0) {
    updated.showReviewCount = !updated.showReviewCount;
  } else {
    updated.showForecast = !updated.showForecast;
  }
  std::string error;
  if (!flashcards::FlashcardStore::saveConfig(updated, error)) {
    LOG_ERR("FLASH", "Could not save display settings: %s", error.c_str());
    return;
  }
  config = updated;
  requestUpdate();
}

void FlashcardDisplaySettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  rows[0].label = tr(STR_FLASHCARD_SHOW_REVIEW_COUNT);
  rows[0].value = config.showReviewCount ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rows[1].label = tr(STR_FLASHCARD_SHOW_FORECAST);
  rows[1].value = config.showForecast ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  for (int i = 0; i < listCount(); ++i) rows[i].actionValue = static_cast<int16_t>(i);
  fui::ListProps props;
  props.items = rows;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

#endif
