#include "FlashcardDeckListActivity.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <CrossPointSettings.h>
#include <FlashcardBackupState.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "FlashcardBackupActivity.h"
#include "FlashcardReviewActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

FlashcardDeckListActivity::FlashcardDeckListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("FlashcardDecks", renderer, mappedInput) {}

void FlashcardDeckListActivity::onEnter() {
  LOG_DBG("FLASH", "Opening flashcard deck list");
  if (!flashcards::FlashcardStore::scanDecks(decks, false)) LOG_ERR("FLASH", "Deck scan failed");

  std::string detail;
  int64_t now = 0;
  const bool clockReady = SETTINGS.clockHasBeenSynced && halClock.getUnixTime(now);
  countsReady = clockReady && flashcards::FlashcardStore::loadConfig(config, detail);
  if (!clockReady) {
    LOG_DBG("FLASH", "Deck due/new counts unavailable: RTC is not synchronized");
  } else if (!countsReady) {
    LOG_ERR("FLASH", "Deck due/new counts unavailable: %s", detail.c_str());
  }

  flashcards::StudyQueue summaryQueue;
  if (countsReady) {
    for (auto& deck : decks) {
      if (!deck.valid()) continue;
      if (!flashcards::FlashcardStore::loadStudyQueue(deck, now, config, summaryQueue, detail)) {
        LOG_ERR("FLASH", "Could not count due/new cards for %s: %s", deck.name.c_str(), detail.c_str());
        continue;
      }
      deck.cardCount = static_cast<uint32_t>(summaryQueue.cards.size());
      deck.cardCountAvailable = true;
      deck.dueCount = static_cast<uint16_t>(summaryQueue.dueCards.size());
      deck.newCount = static_cast<uint16_t>(summaryQueue.newCards.size());
      deck.unseenCount = summaryQueue.unseenCount;
      deck.countsAvailable = true;
      if (!flashcards::FlashcardStore::saveStudySnapshot(deck, summaryQueue)) {
        LOG_ERR("FLASH", "Could not save study snapshot for %s", deck.name.c_str());
      }
    }
  }

  subtitles.clear();
  listItems.clear();
  subtitles.reserve(decks.size());
  listItems.reserve(decks.size());
  for (const auto& deck : decks) {
    if (deck.valid()) {
      char count[80];
      if (deck.countsAvailable) {
        snprintf(count, sizeof(count), tr(STR_FLASHCARD_DECK_COUNTS), static_cast<unsigned>(deck.cardCount),
                 static_cast<unsigned>(deck.dueCount), static_cast<unsigned>(deck.newCount));
      } else if (deck.cardCountAvailable) {
        snprintf(count, sizeof(count), tr(STR_FLASHCARD_CARD_COUNT), static_cast<unsigned>(deck.cardCount));
      } else {
        count[0] = '\0';
      }
      subtitles.emplace_back(count);
    } else {
      LOG_ERR("FLASH", "%s: %s", deck.sourcePath.c_str(), deck.error.c_str());
      subtitles.emplace_back(tr(STR_FLASHCARD_INVALID_DECK));
    }
  }
  LOG_DBG("FLASH", "Building deck list: decks=%u", static_cast<unsigned>(decks.size()));
  for (size_t i = 0; i < decks.size(); ++i) {
    fui::ListItem item;
    item.label = decks[i].name.c_str();
    item.subtitle = subtitles[i].c_str();
    item.icon = listIconFor(UIIcon::Bookmark, 32);
    item.actionValue = static_cast<int16_t>(i);
    listItems.push_back(item);
  }
  UiListActivity::onEnter();
}

int FlashcardDeckListActivity::listCount() const { return static_cast<int>(decks.size()); }

const char* FlashcardDeckListActivity::headerTitle() const { return tr(STR_FLASHCARDS); }

void FlashcardDeckListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (listItems.empty()) {
    screen.centeredText(tr(STR_FLASHCARD_NO_DECKS));
    return;
  }

  fui::ListProps props;
  props.items = listItems.data();
  props.count = static_cast<uint16_t>(listItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}

void FlashcardDeckListActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(decks.size())) return;
  LOG_DBG("FLASH", "Deck selected: index=%d name=%s status=%s", index, decks[index].name.c_str(),
          decks[index].valid() ? "ready" : "invalid");
  nav.selected = index;
  app.clearTapFlash();

  auto& deck = decks[index];
  hasPreparedQueue = false;
  if (deck.valid() && countsReady) {
    int64_t now = 0;
    std::string detail;
    if (halClock.getUnixTime(now) &&
        flashcards::FlashcardStore::loadStudyQueue(deck, now, config, preparedQueue, detail)) {
      deck.cardCount = static_cast<uint32_t>(preparedQueue.cards.size());
      deck.cardCountAvailable = true;
      deck.dueCount = static_cast<uint16_t>(preparedQueue.dueCards.size());
      deck.newCount = static_cast<uint16_t>(preparedQueue.newCards.size());
      deck.unseenCount = preparedQueue.unseenCount;
      deck.countsAvailable = true;
      hasPreparedQueue = true;
      updateSubtitle(index);
    } else if (!detail.empty()) {
      LOG_ERR("FLASH", "Could not load %s: %s", deck.name.c_str(), detail.c_str());
    }
  }
  if (deck.valid() && deck.countsAvailable && deck.dueCount == 0 && deck.newCount == 0 && deck.unseenCount > 0) {
    const int initial = std::min<int>(deck.unseenCount, config.newCardsPerDay > 0 ? config.newCardsPerDay : 10);
    auto picker = makeUniqueNoThrow<IntervalSelectionActivity>(
        renderer, mappedInput, "FlashcardAdditionalNew", StrId::STR_FLASHCARD_LEARN_MORE, initial, 1, deck.unseenCount,
        1, 10, StrId::STR_FLASHCARD_CARD_COUNT);
    if (!picker) {
      LOG_ERR("FLASH", "OOM: additional-new-card picker");
      return;
    }
    startActivityForResult(std::move(picker), [this, index](const ActivityResult& result) {
      if (!result.isCancelled) {
        openReview(index, static_cast<uint16_t>(std::get<IntervalResult>(result.data).value));
      } else {
        preparedQueue = flashcards::StudyQueue{};
        hasPreparedQueue = false;
        requestUpdate();
      }
    });
    return;
  }
  openReview(index, 0);
}

void FlashcardDeckListActivity::openReview(const size_t index, const uint16_t additionalNewCards) {
  auto activity =
      hasPreparedQueue
          ? makeUniqueNoThrow<FlashcardReviewActivity>(renderer, mappedInput, decks[index], config,
                                                       std::move(preparedQueue), additionalNewCards)
          : makeUniqueNoThrow<FlashcardReviewActivity>(renderer, mappedInput, decks[index], additionalNewCards);
  hasPreparedQueue = false;
  if (!activity) {
    LOG_ERR("FLASH", "OOM: FlashcardReviewActivity");
    return;
  }
  startActivityForResult(std::move(activity), [this, index](const ActivityResult& result) {
    auto& deck = decks[index];
    if (const auto* counts = std::get_if<FlashcardCountsResult>(&result.data)) {
      deck.dueCount = counts->due;
      deck.newCount = counts->fresh;
      deck.unseenCount = counts->unseen;
      deck.countsAvailable = true;
    } else {
      deck.countsAvailable = false;
    }
    updateSubtitle(index);
    requestUpdate();
    if (!result.isCancelled) maybePromptBackup();
  });
}

void FlashcardDeckListActivity::maybePromptBackup() {
  if (!config.backupEnabled || config.backupPassword.empty()) return;
  auto state = makeUniqueNoThrow<flashcards::FlashcardBackupState>();
  if (!state || !state->load()) {
    LOG_ERR("FLASH", "Could not check backup reminder");
    return;
  }
  if (!state->shouldPrompt(config.backupReviewInterval)) return;
  auto prompt = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_FLASHCARD_BACKUP),
                                                        tr(STR_FLASHCARD_BACKUP_PROMPT));
  if (!prompt) {
    LOG_ERR("FLASH", "OOM: backup reminder");
    return;
  }
  startActivityForResult(std::move(prompt), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      auto state = makeUniqueNoThrow<flashcards::FlashcardBackupState>();
      if (!state || !state->load()) {
        LOG_ERR("FLASH", "Could not postpone backup reminder");
        return;
      }
      state->postpone(config.backupReviewInterval);
      if (!state->save()) LOG_ERR("FLASH", "Could not save backup reminder postponement");
      return;
    }
    auto activity = makeUniqueNoThrow<FlashcardBackupActivity>(renderer, mappedInput);
    if (!activity) {
      LOG_ERR("FLASH", "OOM: flashcard backup activity");
      return;
    }
    startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
  });
}

void FlashcardDeckListActivity::updateSubtitle(const size_t index) {
  if (index >= decks.size() || index >= subtitles.size() || index >= listItems.size()) return;
  RenderLock lock(*this);
  const auto& deck = decks[index];
  char count[80];
  if (deck.countsAvailable) {
    snprintf(count, sizeof(count), tr(STR_FLASHCARD_DECK_COUNTS), static_cast<unsigned>(deck.cardCount),
             static_cast<unsigned>(deck.dueCount), static_cast<unsigned>(deck.newCount));
  } else if (deck.cardCountAvailable) {
    snprintf(count, sizeof(count), tr(STR_FLASHCARD_CARD_COUNT), static_cast<unsigned>(deck.cardCount));
  } else {
    count[0] = '\0';
  }
  subtitles[index] = count;
  listItems[index].subtitle = subtitles[index].c_str();
}

#endif
