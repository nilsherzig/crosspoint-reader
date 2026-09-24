#include "FlashcardBackupActivity.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <FlashcardBackupState.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/FlashcardBackupUploader.h"

void FlashcardBackupActivity::setState(const State next) {
  RenderLock lock(*this);
  state = next;
}

void FlashcardBackupActivity::onEnter() {
  Activity::onEnter();
  std::string error;
  if (!flashcards::FlashcardStore::loadConfig(config, error) || config.backupPassword.empty() ||
      !SETTINGS.clockHasBeenSynced) {
    LOG_ERR("FLASHBK", "Backup configuration or clock unavailable: %s", error.c_str());
    setState(State::Failed);
    requestUpdate();
    return;
  }
  WiFi.mode(WIFI_STA);
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("FLASHBK", "OOM: WiFi selection");
    setState(State::Failed);
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(wifi),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void FlashcardBackupActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    setState(State::Failed);
    requestUpdate();
    return;
  }
  int64_t now = 0;
  if (!halClock.getUnixTime(now)) {
    LOG_ERR("FLASHBK", "RTC unavailable for backup timestamp");
    setState(State::Failed);
    requestUpdate();
    return;
  }
  setState(State::Uploading);
  requestUpdateAndWait();
  std::string error;
  if (!FlashcardBackupUploader::upload(config, now, onProgress, this, error)) {
    LOG_ERR("FLASHBK", "Backup failed: %s", error.c_str());
    setState(State::Failed);
    requestUpdate();
    return;
  }
  // Establish per-deck baselines at the uploaded journal state. A backup
  // launched from the deck list returns to that same activity, so waiting for
  // its next onEnter() would lose the first reviews after this backup.
  auto counter = makeUniqueNoThrow<flashcards::FlashcardBackupState>();
  std::vector<flashcards::DeckSummary> decks;
  bool baselineOk = counter && counter->load() && flashcards::FlashcardStore::scanDecks(decks);
  if (baselineOk) {
    counter->resetAfterBackup();
    flashcards::StudyQueue queue;
    for (const auto& deck : decks) {
      if (!deck.valid()) continue;
      if (!flashcards::FlashcardStore::loadStudyQueue(deck, now, config, queue, error)) {
        baselineOk = false;
        break;
      }
      counter->observe(deck.key, queue.reviewCount);
    }
    if (baselineOk) baselineOk = counter->save();
  }
  if (!baselineOk) LOG_ERR("FLASHBK", "Backup uploaded, but could not save its review baseline: %s", error.c_str());
  setState(baselineOk ? State::Complete : State::Failed);
  requestUpdate();
}

void FlashcardBackupActivity::onProgress(void* ctx, const size_t completed, const size_t total) {
  auto* self = static_cast<FlashcardBackupActivity*>(ctx);
  {
    RenderLock lock(*self);
    self->completed = completed;
    self->total = total;
  }
  self->requestUpdate(true);
}

void FlashcardBackupActivity::onExit() {
  if (WiFi.getMode() != WIFI_MODE_NULL) WiFi.disconnect(true);
  Activity::onExit();
}

void FlashcardBackupActivity::loop() {
  if (state == State::Complete || state == State::Failed) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm))
      finish();
  }
}

void FlashcardBackupActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_FLASHCARD_BACKUP));
  const int y = renderer.getScreenHeight() / 2;
  if (state == State::Connecting) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_FLASHCARD_BACKUP_CONNECTING));
  } else if (state == State::Uploading) {
    char label[64];
    snprintf(label, sizeof(label), tr(STR_FLASHCARD_BACKUP_PROGRESS), static_cast<unsigned>(completed),
             static_cast<unsigned>(total));
    renderer.drawCenteredText(UI_10_FONT_ID, y, label);
  } else {
    renderer.drawCenteredText(
        UI_10_FONT_ID, y,
        state == State::Complete ? tr(STR_FLASHCARD_BACKUP_COMPLETE) : tr(STR_FLASHCARD_BACKUP_FAILED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}

#endif
