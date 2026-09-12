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
                                                 flashcards::DeckSummary deck, const uint16_t additionalNewCards)
    : Activity("FlashcardReview", renderer, mappedInput),
      UiAppHost(renderer),
      deck(std::move(deck)),
      additionalNewCards(additionalNewCards) {}

void FlashcardReviewActivity::onEnter() {
  Activity::onEnter();
  LOG_DBG("FLASH", "Study session opening: deck=%s cards=%lu", deck.name.c_str(),
          static_cast<unsigned long>(deck.cardCount));
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
  if (!flashcards::FlashcardStore::loadStudyQueue(deck, now, config, queue, detail, additionalNewCards)) {
    showError(ErrorKind::Deck, detail);
    return;
  }
  pendingLearningCards.reserve(queue.cards.size());

  phase = queue.dueCards.empty() ? Phase::New : Phase::Due;
  if (phase == Phase::New && queue.newCards.empty()) phase = Phase::Complete;
  duePosition = 0;
  newPosition = 0;
  LOG_DBG("FLASH", "Study session ready: deck=%s phase=%s due=%u new=%u", deck.name.c_str(), phaseName(),
          static_cast<unsigned>(queue.dueCards.size()), static_cast<unsigned>(queue.newCards.size()));
  if (phase != Phase::Complete && !loadCurrentCard()) return;
  requestUpdate();
}

void FlashcardReviewActivity::onExit() {
  LOG_DBG("FLASH", "Study session closing: deck=%s phase=%s", deck.name.c_str(), phaseName());
  queue = flashcards::StudyQueue{};
  pendingLearningCards = std::vector<uint16_t>{};
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

  if (phase == Phase::Waiting && millis() - lastDueCheck >= 1000) {
    lastDueCheck = millis();
    int64_t now = 0;
    if (!halClock.getUnixTime(now)) {
      showError(ErrorKind::Clock, "RTC read failed while waiting for learning card");
      return;
    }
    if (now >= waitingUntil) {
      loadCurrentCard();
      return;
    }
  }

  if (phase == Phase::Complete || phase == Phase::Error || phase == Phase::Waiting) {
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

void FlashcardReviewActivity::render(RenderLock&&) {
  [[maybe_unused]] const uint32_t startedMicros = micros();
  LOG_DBG("FLASH", "Screen render started: phase=%s answer=%s", phaseName(), answerShown ? "shown" : "hidden");
  renderer.clearScreen();
  renderUi();
  renderer.displayBuffer();
  LOG_DBG("FLASHPERF", "op=render_screen status=ok duration_us=%lu phase=%s answer=%s",
          static_cast<unsigned long>(micros() - startedMicros), phaseName(), answerShown ? "shown" : "hidden");
}

void FlashcardReviewActivity::buildScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  screen.setContentMargin(fui::Insets{theme.spaceMd, theme.spaceLg, theme.spaceMd, theme.spaceLg});
  screen.header(deck.name.c_str(), nullptr, progressText);
  screen.spacer(theme.spaceMd);

  if (phase == Phase::Complete || phase == Phase::Error || phase == Phase::Waiting) {
    fui::FooterAction done[] = {{tr(STR_DONE), ACTION_DONE}};
    screen.footer(done, 1);
    const char* message = phase == Phase::Complete  ? tr(STR_FLASHCARD_SESSION_COMPLETE)
                          : phase == Phase::Waiting ? waitingText
                                                    : errorText();
    screen.centeredText(message, theme.bodyText);
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
  const int16_t sectionGap = theme.spaceLg;
  const int16_t sectionHeight = std::max<int16_t>(0, static_cast<int16_t>(body.height - sectionGap)) / 2;
  const fui::Rect questionRect{body.x, body.y, body.width, sectionHeight};
  const fui::Rect answerRect{body.x, static_cast<int16_t>(questionRect.bottom() + sectionGap), body.width,
                             static_cast<int16_t>(body.bottom() - questionRect.bottom() - sectionGap)};

  // The question keeps the same rectangle and style before and after reveal so it never jumps between refreshes.
  fui::TextStyle questionStyle = theme.titleText;
  const int16_t questionLineHeight = screen.target().lineHeight(questionStyle.font);
  questionStyle.align = fui::TextAlign::Left;
  questionStyle.maxLines = static_cast<uint8_t>(
      std::clamp<int>(questionLineHeight > 0 ? questionRect.height / questionLineHeight : 1, 1, 255));
  screen.target().text(questionRect, frontText.c_str(), questionStyle);

  if (!answerShown) {
    screen.frame().hit(body, ACTION_REVEAL, 0, fui::InputTouch);
    return;
  }

  const int16_t separatorY = static_cast<int16_t>(questionRect.bottom() + sectionGap / 2);
  screen.target().line(fui::Point{body.x, separatorY}, fui::Point{static_cast<int16_t>(body.right() - 1), separatorY},
                       std::max<uint8_t>(theme.headerUnderline, 1), fui::Paint::solid(theme.bodyText.color));

  fui::TextStyle answerStyle = theme.bodyText;
  const int16_t answerLineHeight = screen.target().lineHeight(answerStyle.font);
  answerStyle.align = fui::TextAlign::Left;
  answerStyle.maxLines =
      static_cast<uint8_t>(std::clamp<int>(answerLineHeight > 0 ? answerRect.height / answerLineHeight : 1, 1, 255));
  screen.target().text(answerRect, backText.c_str(), answerStyle);

  const int16_t leftWidth = body.width / 2;
  screen.frame().hit(fui::Rect{body.x, body.y, leftWidth, body.height}, ACTION_AGAIN, 0, fui::InputTouch);
  screen.frame().hit(fui::Rect{static_cast<int16_t>(body.x + leftWidth), body.y,
                               static_cast<int16_t>(body.width - leftWidth), body.height},
                     ACTION_GOOD, 0, fui::InputTouch);
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
  if (phase == Phase::Complete || phase == Phase::Error || phase == Phase::Waiting || answerShown) return;
  LOG_DBG("FLASH", "Reveal requested: phase=%s source_order=%lu", phaseName(),
          static_cast<unsigned long>(queue.cards[currentCardIndex].sourceOrder));
  std::string detail;
  if (!flashcards::FlashcardStore::readCardText(deck, queue.cards[currentCardIndex], true, backText, detail)) {
    showError(ErrorKind::Deck, detail);
    return;
  }
  answerShown = true;
  LOG_DBG("FLASH", "Answer ready: source_order=%lu bytes=%u",
          static_cast<unsigned long>(queue.cards[currentCardIndex].sourceOrder),
          static_cast<unsigned>(backText.size()));
  requestUpdate();
}

void FlashcardReviewActivity::rate(const flashcards::Rating rating) {
  if (!answerShown || phase == Phase::Complete || phase == Phase::Error || phase == Phase::Waiting) return;
  LOG_DBG("FLASH", "Rating requested: phase=%s source_order=%lu rating=%s", phaseName(),
          static_cast<unsigned long>(queue.cards[currentCardIndex].sourceOrder),
          rating == flashcards::Rating::Again ? "again" : "good");
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
  advance();
}

void FlashcardReviewActivity::advance() {
  const uint16_t reviewed = currentCardIndex;
  LOG_DBG("FLASH", "Advancing session: phase=%s source=%s", phaseName(), currentFromPending ? "learning" : "base");
  if (currentFromPending) {
    pendingLearningCards.erase(pendingLearningCards.begin() + static_cast<ptrdiff_t>(currentPendingPosition));
  } else {
    ++currentPhasePosition();
  }

  const auto nextPhase = queue.cards[reviewed].phase;
  if (nextPhase == flashcards::CardPhase::Learning || nextPhase == flashcards::CardPhase::Relearning) {
    pendingLearningCards.push_back(reviewed);
  }
  currentFromPending = false;
  loadCurrentCard();
}

bool FlashcardReviewActivity::loadCurrentCard() {
  int64_t now = 0;
  if (!halClock.getUnixTime(now)) {
    showError(ErrorKind::Clock, "RTC read failed while loading card");
    return false;
  }

  size_t earliestPosition = pendingLearningCards.size();
  int64_t earliestDue = INT64_MAX;
  for (size_t i = 0; i < pendingLearningCards.size(); ++i) {
    const int64_t due = queue.cards[pendingLearningCards[i]].due;
    if (due <= now && due < earliestDue) {
      earliestDue = due;
      earliestPosition = i;
    }
  }
  if (earliestPosition < pendingLearningCards.size()) {
    currentFromPending = true;
    currentPendingPosition = earliestPosition;
    currentCardIndex = pendingLearningCards[earliestPosition];
    if (phase == Phase::Waiting) phase = Phase::Due;
    return loadCard(now);
  }
  currentFromPending = false;

  while (phase == Phase::Due || phase == Phase::New) {
    if (currentPhasePosition() < currentPhaseQueue().size()) break;
    if (phase == Phase::Due) {
      phase = Phase::New;
      LOG_DBG("FLASH", "Session phase changed: phase=%s", phaseName());
      continue;
    }
    if (!pendingLearningCards.empty()) {
      showWaiting(now);
    } else {
      phase = Phase::Complete;
      LOG_DBG("FLASH", "Study session complete: deck=%s", deck.name.c_str());
      progressText[0] = '\0';
      frontText.clear();
      backText.clear();
      requestUpdate();
    }
    return true;
  }

  if (phase == Phase::Waiting) {
    showWaiting(now);
    return true;
  }
  currentCardIndex = currentPhaseQueue()[currentPhasePosition()];
  return loadCard(now);
}

bool FlashcardReviewActivity::loadCard(const int64_t now) {
  flashcards::StudyCard& card = queue.cards[currentCardIndex];
  LOG_DBG("FLASH", "Loading card: phase=%s source=%s source_order=%lu", phaseName(),
          currentFromPending ? "learning" : "base", static_cast<unsigned long>(card.sourceOrder));
  std::string detail;
  if (!flashcards::FlashcardStore::introduceCard(deck, card, now, detail) ||
      !flashcards::FlashcardStore::readCardText(deck, card, false, frontText, detail)) {
    showError(ErrorKind::Save, detail);
    return false;
  }
  backText.clear();
  answerShown = false;
  const size_t dueRemaining = pendingLearningCards.size() + queue.dueCards.size() - duePosition;
  const size_t newRemaining = queue.newCards.size() - newPosition;
  snprintf(progressText, sizeof(progressText), tr(STR_FLASHCARD_REMAINING), static_cast<unsigned>(dueRemaining),
           static_cast<unsigned>(newRemaining));
  requestUpdate();
  return true;
}

void FlashcardReviewActivity::showWaiting(const int64_t now) {
  waitingUntil = INT64_MAX;
  for (const uint16_t index : pendingLearningCards) waitingUntil = std::min(waitingUntil, queue.cards[index].due);
  const uint32_t minutes = static_cast<uint32_t>(std::max<int64_t>(1, (waitingUntil - now + 59) / 60));
  snprintf(waitingText, sizeof(waitingText), tr(STR_FLASHCARD_NEXT_LEARNING), static_cast<unsigned>(minutes));
  phase = Phase::Waiting;
  progressText[0] = '\0';
  frontText.clear();
  backText.clear();
  answerShown = false;
  lastDueCheck = millis();
  LOG_DBG("FLASH", "Waiting for learning card: due=%lld minutes=%lu", static_cast<long long>(waitingUntil),
          static_cast<unsigned long>(minutes));
  requestUpdate();
}

void FlashcardReviewActivity::showError(const ErrorKind kind, const std::string& detail) {
  LOG_ERR("FLASH", "Session error: kind=%u detail=%s", static_cast<unsigned>(kind), detail.c_str());
  phase = Phase::Error;
  errorKind = kind;
  progressText[0] = '\0';
  requestUpdate();
}

std::vector<uint16_t>& FlashcardReviewActivity::currentPhaseQueue() {
  return phase == Phase::Due ? queue.dueCards : queue.newCards;
}

size_t& FlashcardReviewActivity::currentPhasePosition() { return phase == Phase::Due ? duePosition : newPosition; }

const char* FlashcardReviewActivity::phaseName() const {
  switch (phase) {
    case Phase::Due:
      return "due";
    case Phase::New:
      return "new";
    case Phase::Waiting:
      return "waiting";
    case Phase::Complete:
      return "complete";
    case Phase::Error:
      return "error";
    default:
      return "unknown";
  }
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
