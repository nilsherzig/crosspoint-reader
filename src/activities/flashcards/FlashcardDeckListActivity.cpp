#include "FlashcardDeckListActivity.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <CrossPointSettings.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "FlashcardReviewActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

FlashcardDeckListActivity::FlashcardDeckListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("FlashcardDecks", renderer, mappedInput) {}

void FlashcardDeckListActivity::onEnter() {
  LOG_DBG("FLASH", "Opening flashcard deck list");
  if (!flashcards::FlashcardStore::scanDecks(decks)) LOG_ERR("FLASH", "Deck scan failed");

  flashcards::Config config;
  std::string detail;
  int64_t now = 0;
  const bool clockReady = SETTINGS.clockHasBeenSynced && halClock.getUnixTime(now);
  const bool countsReady = clockReady && flashcards::FlashcardStore::loadConfig(config, detail);
  if (!clockReady) {
    LOG_DBG("FLASH", "Deck due/new counts unavailable: RTC is not synchronized");
  } else if (!countsReady) {
    LOG_ERR("FLASH", "Deck due/new counts unavailable: %s", detail.c_str());
  }

  flashcards::StudyQueue summaryQueue;
  if (countsReady) {
    for (auto& deck : decks) {
      if (!deck.valid()) continue;
      detail.clear();
      if (!flashcards::FlashcardStore::loadStudyQueue(deck, now, config, summaryQueue, detail)) {
        LOG_ERR("FLASH", "Could not count due/new cards for %s: %s", deck.name.c_str(), detail.c_str());
        continue;
      }
      deck.dueCount = static_cast<uint16_t>(summaryQueue.dueCards.size());
      deck.newCount = static_cast<uint16_t>(summaryQueue.newCards.size());
      deck.countsAvailable = true;
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
      } else {
        snprintf(count, sizeof(count), tr(STR_FLASHCARD_CARD_COUNT), static_cast<unsigned>(deck.cardCount));
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
  syncListViewport(screen, props, true);
  screen.list(props);
}

void FlashcardDeckListActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(decks.size())) return;
  LOG_DBG("FLASH", "Deck selected: index=%d name=%s status=%s", index, decks[index].name.c_str(),
          decks[index].valid() ? "ready" : "invalid");
  nav.selected = index;
  app.clearTapFlash();

  auto activity = makeUniqueNoThrow<FlashcardReviewActivity>(renderer, mappedInput, decks[index]);
  if (!activity) {
    LOG_ERR("FLASH", "OOM: FlashcardReviewActivity");
    return;
  }
  startActivityForResult(std::move(activity), {});
}

#endif
