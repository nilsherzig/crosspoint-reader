#include "FlashcardDeckListActivity.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include "FlashcardReviewActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

FlashcardDeckListActivity::FlashcardDeckListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("FlashcardDecks", renderer, mappedInput) {}

void FlashcardDeckListActivity::onEnter() {
  if (!flashcards::FlashcardStore::scanDecks(decks)) LOG_ERR("FLASH", "Deck scan failed");

  subtitles.clear();
  listItems.clear();
  subtitles.reserve(decks.size());
  listItems.reserve(decks.size());
  for (const auto& deck : decks) {
    if (deck.valid()) {
      char count[40];
      snprintf(count, sizeof(count), tr(STR_FLASHCARD_CARD_COUNT), static_cast<unsigned>(deck.cardCount));
      subtitles.emplace_back(count);
    } else {
      LOG_ERR("FLASH", "%s: %s", deck.sourcePath.c_str(), deck.error.c_str());
      subtitles.emplace_back(tr(STR_FLASHCARD_INVALID_DECK));
    }
  }
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
