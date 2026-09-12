#include "FlashcardReviewActivity.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <CrossPointSettings.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace fui = freeink::ui;

FlashcardReviewActivity::FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 flashcards::DeckSummary deck)
    : Activity("FlashcardReview", renderer, mappedInput), UiAppHost(renderer), deck(std::move(deck)) {}

void FlashcardReviewActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_REVEAL, &FlashcardReviewActivity::actionTrampoline, this);
  app.on(ACTION_AGAIN, &FlashcardReviewActivity::actionTrampoline, this);
  app.on(ACTION_GOOD, &FlashcardReviewActivity::actionTrampoline, this);
  app.on(ACTION_DONE, &FlashcardReviewActivity::actionTrampoline, this);
  app.setScreen(&FlashcardReviewActivity::screenTrampoline, this);

  if (!deck.valid()) {
    showError(ErrorKind::Deck, deck.error);
    return;
  }
  int64_t now = 0;
  if (!SETTINGS.clockHasBeenSynced || !halClock.getUnixTime(now)) {
    showError(ErrorKind::Clock, "RTC has not been synchronized");
    return;
  }
  std::string detail;
  if (!flashcards::FlashcardStore::loadConfig(config, detail)) {
    showError(ErrorKind::Config, detail);
    return;
  }
  if (!flashcards::FlashcardStore::loadStudyQueue(deck, now, config, queue, detail)) {
    showError(ErrorKind::Deck, detail);
    return;
  }

  phase = queue.dueCards.empty() ? Phase::New : Phase::Due;
  if (phase == Phase::New && queue.newCards.empty()) phase = Phase::Complete;
  phasePosition = 0;
  if (phase != Phase::Complete && !loadCurrentCard()) return;
  requestUpdate();
}

void FlashcardReviewActivity::onExit() {
  queue = flashcards::StudyQueue{};
  frontText.clear();
  backText.clear();
  Activity::onExit();
}

void FlashcardReviewActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<FlashcardReviewActivity*>(user)->buildScreen(screen);
}

void FlashcardReviewActivity::actionTrampoline(const fui::ActionEvent& event, void* user) {
  static_cast<FlashcardReviewActivity*>(user)->handleAction(event.action);
}

void FlashcardReviewActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (phase == Phase::Complete || phase == Phase::Error) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      finish();
      return;
    }
  } else if (!answerShown) {
    if (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      reveal();
      return;
    }
  } else {
    if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
      rate(flashcards::Rating::Again);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      rate(flashcards::Rating::Good);
      return;
    }
  }

  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
}

void FlashcardReviewActivity::render(RenderLock&&) { renderUi(); }

void FlashcardReviewActivity::buildScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  screen.setContentMargin(fui::Insets{theme.spaceMd, theme.spaceLg, theme.spaceMd, theme.spaceLg});
  screen.header(deck.name.c_str(), nullptr, progressText);
  screen.spacer(theme.spaceMd);

  if (phase == Phase::Complete || phase == Phase::Error) {
    fui::FooterAction done[] = {{tr(STR_DONE), ACTION_DONE}};
    screen.footer(done, 1);
    screen.centeredText(phase == Phase::Complete ? tr(STR_FLASHCARD_SESSION_COMPLETE) : errorText(),
                        theme.bodyText);
    return;
  }

  if (answerShown) {
    fui::FooterAction ratings[] = {{tr(STR_FLASHCARD_AGAIN), ACTION_AGAIN}, {tr(STR_FLASHCARD_GOOD), ACTION_GOOD}};
    screen.footer(ratings, 2);
  } else {
    fui::FooterAction revealAction[] = {{tr(STR_FLASHCARD_SHOW_ANSWER), ACTION_REVEAL}};
    screen.footer(revealAction, 1);
  }

  const fui::Rect body = screen.body();
  fui::TextStyle textStyle = answerShown ? theme.bodyText : theme.titleText;
  const int16_t lineHeight = screen.target().lineHeight(textStyle.font);
  textStyle.align = fui::TextAlign::Center;
  textStyle.maxLines = static_cast<uint8_t>(std::clamp<int>(lineHeight > 0 ? body.height / lineHeight : 1, 1, 255));
  screen.target().text(body, answerShown ? backText.c_str() : frontText.c_str(), textStyle);
  if (!answerShown) screen.frame().hit(body, ACTION_REVEAL, 0, fui::InputTouch);
}

void FlashcardReviewActivity::handleAction(const fui::ActionId action) {
  app.clearTapFlash();
  if (action == ACTION_DONE) {
    finish();
  } else if (action == ACTION_REVEAL) {
    reveal();
  } else if (action == ACTION_AGAIN) {
    rate(flashcards::Rating::Again);
  } else if (action == ACTION_GOOD) {
    rate(flashcards::Rating::Good);
  }
}

void FlashcardReviewActivity::reveal() {
  if (phase == Phase::Complete || phase == Phase::Error || answerShown) return;
  std::string detail;
  if (!flashcards::FlashcardStore::readCardText(deck, queue.cards[currentCardIndex], true, backText, detail)) {
    showError(ErrorKind::Deck, detail);
    return;
  }
  answerShown = true;
  requestUpdate();
}

void FlashcardReviewActivity::rate(const flashcards::Rating rating) {
  if (!answerShown || phase == Phase::Complete || phase == Phase::Error) return;
  int64_t now = 0;
  if (!halClock.getUnixTime(now)) {
    showError(ErrorKind::Clock, "RTC read failed during review");
    return;
  }
  std::string detail;
  if (!flashcards::FlashcardStore::reviewCard(deck, queue.cards[currentCardIndex], now, rating, config, detail)) {
    showError(ErrorKind::Save, detail);
    return;
  }
  advance(rating == flashcards::Rating::Again);
}

void FlashcardReviewActivity::advance(const bool repeat) {
  std::vector<uint16_t>& active = currentPhaseQueue();
  const uint16_t reviewed = currentCardIndex;
  ++phasePosition;
  if (repeat) {
    if (active.size() == active.capacity() && phasePosition > 0) {
      active.erase(active.begin(), active.begin() + static_cast<ptrdiff_t>(phasePosition));
      phasePosition = 0;
    }
    active.push_back(reviewed);
  }
  loadCurrentCard();
}

bool FlashcardReviewActivity::loadCurrentCard() {
  while (phase == Phase::Due || phase == Phase::New) {
    std::vector<uint16_t>& active = currentPhaseQueue();
    if (phasePosition < active.size()) break;
    if (phase == Phase::Due) {
      phase = Phase::New;
      phasePosition = 0;
      continue;
    }
    phase = Phase::Complete;
    progressText[0] = '\0';
    frontText.clear();
    backText.clear();
    requestUpdate();
    return true;
  }

  currentCardIndex = currentPhaseQueue()[phasePosition];
  flashcards::StudyCard& card = queue.cards[currentCardIndex];
  int64_t now = 0;
  std::string detail;
  if (!halClock.getUnixTime(now)) {
    showError(ErrorKind::Clock, "RTC read failed while loading card");
    return false;
  }
  if (!flashcards::FlashcardStore::introduceCard(deck, card, now, detail) ||
      !flashcards::FlashcardStore::readCardText(deck, card, false, frontText, detail)) {
    showError(ErrorKind::Save, detail);
    return false;
  }
  backText.clear();
  answerShown = false;
  const size_t dueRemaining = phase == Phase::Due ? currentPhaseQueue().size() - phasePosition : 0;
  const size_t newRemaining = phase == Phase::New ? currentPhaseQueue().size() - phasePosition : queue.newCards.size();
  snprintf(progressText, sizeof(progressText), tr(STR_FLASHCARD_REMAINING), static_cast<unsigned>(dueRemaining),
           static_cast<unsigned>(newRemaining));
  requestUpdate();
  return true;
}

void FlashcardReviewActivity::showError(const ErrorKind kind, const std::string& detail) {
  LOG_ERR("FLASH", "%s", detail.c_str());
  phase = Phase::Error;
  errorKind = kind;
  progressText[0] = '\0';
  requestUpdate();
}

std::vector<uint16_t>& FlashcardReviewActivity::currentPhaseQueue() {
  return phase == Phase::Due ? queue.dueCards : queue.newCards;
}

const char* FlashcardReviewActivity::errorText() const {
  switch (errorKind) {
    case ErrorKind::Clock:
      return tr(STR_FLASHCARD_CLOCK_REQUIRED);
    case ErrorKind::Config:
      return tr(STR_FLASHCARD_INVALID_CONFIG);
    case ErrorKind::Save:
      return tr(STR_FLASHCARD_SAVE_FAILED);
    case ErrorKind::Deck:
    case ErrorKind::None:
    default:
      return tr(STR_FLASHCARD_INVALID_DECK);
  }
}

#endif
