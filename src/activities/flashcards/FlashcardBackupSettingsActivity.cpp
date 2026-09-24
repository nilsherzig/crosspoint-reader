#include "FlashcardBackupSettingsActivity.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <FlashcardBackupState.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "FlashcardBackupActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void FlashcardBackupSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  std::string error;
  if (!flashcards::FlashcardStore::loadConfig(config, error)) {
    LOG_ERR("FLASHBK", "Cannot load settings: %s", error.c_str());
    showSaveError = true;
  }
  static constexpr StrId names[] = {StrId::STR_FLASHCARD_BACKUP_REMIND, StrId::STR_FLASHCARD_BACKUP_INTERVAL,
                                    StrId::STR_FLASHCARD_BACKUP_SERVER, StrId::STR_PASSWORD,
                                    StrId::STR_FLASHCARD_BACKUP_DIRECTORY, StrId::STR_FLASHCARD_BACKUP_NOW};
  for (size_t i = 0; i < std::size(rows); ++i) {
    rows[i].label = I18N.get(names[i]);
    rows[i].actionValue = static_cast<int16_t>(i);
  }
}

const char* FlashcardBackupSettingsActivity::headerTitle() const { return tr(STR_FLASHCARD_BACKUP); }

bool FlashcardBackupSettingsActivity::save(const flashcards::Config& updated) {
  std::string error;
  if (!flashcards::FlashcardStore::saveConfig(updated, error)) {
    LOG_ERR("FLASHBK", "Cannot save backup settings: %s", error.c_str());
    showSaveError = true;
    requestUpdate();
    return false;
  }
  config = updated;
  showSaveError = false;
  requestUpdate();
  return true;
}

void FlashcardBackupSettingsActivity::editText(const int index) {
  const char* title = index == 2 ? tr(STR_FLASHCARD_BACKUP_SERVER)
                      : index == 3 ? tr(STR_PASSWORD)
                                   : tr(STR_FLASHCARD_BACKUP_DIRECTORY);
  const std::string& value = index == 2 ? config.backupServerUrl
                             : index == 3 ? config.backupPassword
                                          : config.backupDirectory;
  const size_t maxLength = index == 2 ? 127 : index == 3 ? 63 : 80;
  const InputType type = index == 2 ? InputType::Url : index == 3 ? InputType::Password : InputType::Text;
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, title, value, maxLength, type);
  if (!keyboard) {
    LOG_ERR("FLASHBK", "OOM: backup setting editor");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, index](const ActivityResult& result) {
    if (result.isCancelled) return;
    flashcards::Config updated = config;
    const auto& value = std::get<KeyboardResult>(result.data).text;
    if (index == 2) updated.backupServerUrl = value;
    if (index == 3) updated.backupPassword = value;
    if (index == 4) updated.backupDirectory = value;
    save(updated);
  });
}

void FlashcardBackupSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  if (index == 0) {
    flashcards::Config updated = config;
    updated.backupEnabled = !updated.backupEnabled;
    if (save(updated) && updated.backupEnabled) {
      auto state = makeUniqueNoThrow<flashcards::FlashcardBackupState>();
      if (!state || !state->load()) {
        LOG_ERR("FLASHBK", "Could not initialize backup reminder state");
        showSaveError = true;
      } else {
        state->resetAfterBackup();
        if (!state->save()) showSaveError = true;
      }
    }
  } else if (index == 1) {
    auto picker = makeUniqueNoThrow<IntervalSelectionActivity>(
        renderer, mappedInput, "BackupReviewInterval", StrId::STR_FLASHCARD_BACKUP_INTERVAL,
        static_cast<int>(config.backupReviewInterval), 1, 1000000, 1, 100, StrId::STR_FLASHCARD_CARD_COUNT);
    if (!picker) {
      LOG_ERR("FLASHBK", "OOM: backup interval picker");
      return;
    }
    startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        flashcards::Config updated = config;
        updated.backupReviewInterval = std::get<IntervalResult>(result.data).value;
        save(updated);
      }
    });
  } else if (index < 5) {
    editText(index);
  } else {
    auto activity = makeUniqueNoThrow<FlashcardBackupActivity>(renderer, mappedInput);
    if (!activity) {
      LOG_ERR("FLASHBK", "OOM: backup activity");
      return;
    }
    startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
  }
}

void FlashcardBackupSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  rows[0].value = config.backupEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  snprintf(intervalText, sizeof(intervalText), "%lu", static_cast<unsigned long>(config.backupReviewInterval));
  rows[1].value = intervalText;
  rows[2].value = config.backupServerUrl.c_str();
  rows[3].value = config.backupPassword.empty() ? tr(STR_NOT_SET) : "******";
  rows[4].value = config.backupDirectory.c_str();
  fui::ListProps props;
  props.items = rows;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void FlashcardBackupSettingsActivity::drawFooter() {
  UiListActivity::drawFooter();
  if (showSaveError) GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
}

#endif
