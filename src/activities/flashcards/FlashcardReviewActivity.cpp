#include "FlashcardReviewActivity.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <CrossPointSettings.h>
#include <FlashcardBackupState.h>
#include <FlashcardTextLayout.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <ReviewCount.h>
#include <ReviewForecast.h>
#include <ReviewUndo.h>
#include <StudyQueueBuilder.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <limits>
#include <utility>

#include "components/UIScale.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
static constexpr StrId MONTH_NAMES[] = {
    StrId::STR_FLASHCARD_MONTH_JAN, StrId::STR_FLASHCARD_MONTH_FEB, StrId::STR_FLASHCARD_MONTH_MAR,
    StrId::STR_FLASHCARD_MONTH_APR, StrId::STR_FLASHCARD_MONTH_MAY, StrId::STR_FLASHCARD_MONTH_JUN,
    StrId::STR_FLASHCARD_MONTH_JUL, StrId::STR_FLASHCARD_MONTH_AUG, StrId::STR_FLASHCARD_MONTH_SEP,
    StrId::STR_FLASHCARD_MONTH_OCT, StrId::STR_FLASHCARD_MONTH_NOV, StrId::STR_FLASHCARD_MONTH_DEC,
};

int cardFontId(const uint8_t pointSize) {
  switch (pointSize) {
    case 12:
      return UI_12_FONT_ID;
    case 14:
      return NOTOSANS_14_FONT_ID;
    case 16:
      return NOTOSANS_16_FONT_ID;
    case 18:
      return NOTOSANS_18_FONT_ID;
    default:
      return UI_12_FONT_ID;
  }
}
}  // namespace

FlashcardReviewActivity::FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 flashcards::DeckSummary deck, const uint16_t additionalNewCards)
    : Activity("FlashcardReview", renderer, mappedInput),
      UiAppHost(renderer),
      deck(std::move(deck)),
      additionalNewCards(additionalNewCards) {}

FlashcardReviewActivity::FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 flashcards::DeckSummary deck, flashcards::Config config,
                                                 flashcards::StudyQueue queue, const uint16_t additionalNewCards)
    : Activity("FlashcardReview", renderer, mappedInput),
      UiAppHost(renderer),
      deck(std::move(deck)),
      config(std::move(config)),
      queue(std::move(queue)),
      additionalNewCards(additionalNewCards),
      preparedQueue(true) {}

void FlashcardReviewActivity::onEnter() {
  Activity::onEnter();
  LOG_DBG("FLASH", "Study session opening: deck=%s cards=%lu", deck.name.c_str(),
          static_cast<unsigned long>(deck.cardCount));
  resetUi();
  app.on(ACTION_REVEAL, &FlashcardReviewActivity::actionTrampoline, this);
  app.on(ACTION_AGAIN, &FlashcardReviewActivity::actionTrampoline, this);
  app.on(ACTION_GOOD, &FlashcardReviewActivity::actionTrampoline, this);
  app.on(ACTION_DONE, &FlashcardReviewActivity::actionTrampoline, this);
  lastRating.valid = false;
  undoIndicatorUntil = 0;
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
  currentUtcDay = static_cast<int32_t>(now / 86400);
  std::string detail;
  if (preparedQueue) {
    if (queue.snapshotDay != currentUtcDay) {
      queue.snapshotDay = currentUtcDay;
      queue.introducedToday = 0;
    }
    queue.unseenCount =
        flashcards::detail::buildStudyQueues(queue.cards, now, currentUtcDay, queue.introducedToday,
                                             config.newCardsPerDay, additionalNewCards, queue.dueCards, queue.newCards);
  } else {
    if (!flashcards::FlashcardStore::loadConfig(config, detail)) {
      showError(ErrorKind::Config, detail);
      return;
    }
    if (!flashcards::FlashcardStore::loadStudyQueue(deck, now, config, queue, detail, additionalNewCards)) {
      showError(ErrorKind::Deck, detail);
      return;
    }
  }
  backupTrackingReady = config.backupEnabled;
  pendingLearningCards.reserve(queue.cards.size());
  size_t readyDueCount = 0;
  for (const uint16_t index : queue.dueCards) {
    if (flashcards::detail::learningCardPending(queue.cards[index], now)) {
      pendingLearningCards.push_back(index);
    } else {
      queue.dueCards[readyDueCount++] = index;
    }
  }
  queue.dueCards.resize(readyDueCount);

  phase = queue.dueCards.empty() ? Phase::New : Phase::Due;
  if (phase == Phase::New && queue.newCards.empty() && pendingLearningCards.empty()) phase = Phase::Complete;
  duePosition = 0;
  newPosition = 0;
  LOG_DBG("FLASH", "Study session ready: deck=%s phase=%s due=%u new=%u", deck.name.c_str(), phaseName(),
          static_cast<unsigned>(queue.dueCards.size()), static_cast<unsigned>(queue.newCards.size()));
  if (phase != Phase::Complete && !loadCurrentCard()) return;
  requestUpdate();
}

void FlashcardReviewActivity::onExit() {
  LOG_DBG("FLASH", "Study session closing: deck=%s phase=%s", deck.name.c_str(), phaseName());
  if (!flashcards::FlashcardStore::saveStudySnapshot(deck, queue)) {
    LOG_ERR("FLASH", "Could not save study snapshot for %s", deck.name.c_str());
  }
  if (backupTrackingReady) {
    // This also runs when Home replaces the activity without popping to the deck list.
    auto state = makeUniqueNoThrow<flashcards::FlashcardBackupState>();
    if (!state || !state->load()) {
      LOG_ERR("FLASH", "Could not load backup counter when closing review");
    } else {
      state->observe(deck.key, queue.reviewCount);
      if (!state->save()) LOG_ERR("FLASH", "Could not save backup counter when closing review");
    }
  }
  lastRating.valid = false;
  undoIndicatorUntil = 0;
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
    finishSession();
    return;
  }

  if (undoIndicatorUntil != 0 && static_cast<int32_t>(millis() - undoIndicatorUntil) >= 0) {
    undoIndicatorUntil = 0;
    requestUpdate();
  }

  if (lastRating.valid && phase != Phase::Error) {
    int x = 0;
    int y = 0;
    if ((undoTouchEnabled() && mappedInput.wasScreenLongPress(x, y)) ||
        (undoSideEnabled(MappedInputManager::Button::Up) &&
         mappedInput.wasLongPressed(MappedInputManager::Button::Up, 700)) ||
        (undoSideEnabled(MappedInputManager::Button::Down) &&
         mappedInput.wasLongPressed(MappedInputManager::Button::Down, 700))) {
      undo();
      return;
    }
  }

  if (phase == Phase::Waiting && millis() - lastDueCheck >= 1000) {
    lastDueCheck = millis();
    int64_t now = 0;
    if (!halClock.getUnixTime(now)) {
      showError(ErrorKind::Clock, "RTC read failed while waiting for learning card");
      return;
    }
    if (flashcards::detail::learningCardReady(waitingUntil, now, config.learnAheadLimitMinutes, false)) {
      loadCurrentCard();
      return;
    }
  }

  if (phase == Phase::Complete || phase == Phase::Error || phase == Phase::Waiting) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      finishSession();
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
  screen.header(deck.name.c_str(), nullptr, undoIndicatorUntil != 0 ? tr(STR_FLASHCARD_UNDONE) : progressText);
  screen.spacer(theme.spaceLg * 2);

  if (phase == Phase::Complete) {
    fui::FooterAction done[] = {{tr(STR_DONE), ACTION_DONE}};
    screen.footer(done, 1);

    char totalText[64]{};
    if (config.showReviewCount) {
      snprintf(totalText, sizeof(totalText), tr(STR_FLASHCARD_TOTAL_REVIEWS), static_cast<unsigned>(queue.reviewCount));
    }
    char forecastText[64]{};
    if (config.showForecast) {
      const int64_t projectedDay = flashcards::detail::projectedIntroductionDay(
          queue.firstReviewDay, currentUtcDay, static_cast<uint32_t>(queue.cards.size()), queue.unseenCount,
          queue.reviewCount);
      if (projectedDay >= 0 && projectedDay <= std::numeric_limits<time_t>::max() / 86400) {
        const time_t targetTime = static_cast<time_t>(projectedDay * 86400);
        struct tm targetDate;
        if (gmtime_r(&targetTime, &targetDate) && targetDate.tm_mon >= 0 && targetDate.tm_mon < 12) {
          snprintf(forecastText, sizeof(forecastText), tr(STR_FLASHCARD_FORECAST), I18N[MONTH_NAMES[targetDate.tm_mon]],
                   targetDate.tm_year + 1900);
        }
      }
    }
    const fui::Rect body = screen.body();
    fui::TextStyle titleStyle = theme.titleText;
    titleStyle.align = fui::TextAlign::Center;
    fui::TextStyle countStyle = theme.bodyText;
    countStyle.align = fui::TextAlign::Center;
    const int16_t titleHeight = screen.target().lineHeight(titleStyle.font);
    const int16_t countHeight = screen.target().lineHeight(countStyle.font);
    const int16_t forecastHeight = forecastText[0] != '\0' ? countHeight : 0;
    const int16_t totalHeight =
        static_cast<int16_t>(titleHeight + (config.showReviewCount ? theme.spaceMd + countHeight : 0) +
                             (forecastHeight > 0 ? theme.spaceMd + forecastHeight : 0));
    const int16_t titleY = static_cast<int16_t>(body.y + std::max(0, (body.height - totalHeight) / 2));
    screen.target().text(fui::Rect{body.x, titleY, body.width, titleHeight}, tr(STR_FLASHCARD_SESSION_COMPLETE),
                         titleStyle);
    int16_t nextY = static_cast<int16_t>(titleY + titleHeight);
    if (config.showReviewCount) {
      nextY = static_cast<int16_t>(nextY + theme.spaceMd);
      screen.target().text(fui::Rect{body.x, nextY, body.width, countHeight}, totalText, countStyle);
      nextY = static_cast<int16_t>(nextY + countHeight);
    }
    if (forecastHeight > 0) {
      nextY = static_cast<int16_t>(nextY + theme.spaceMd);
      screen.target().text(fui::Rect{body.x, nextY, body.width, forecastHeight}, forecastText, countStyle);
    }
    return;
  }
  if (phase == Phase::Error || phase == Phase::Waiting) {
    fui::FooterAction done[] = {{tr(STR_DONE), ACTION_DONE}};
    screen.footer(done, 1);
    screen.centeredText(phase == Phase::Waiting ? waitingText : errorText(), theme.bodyText);
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
  // The adapter has three font slots. Rebind the title slot only while painting
  // card text, leaving the already-drawn header/footer at the theme's UI size.
  uiTarget.setFont(fui::GfxRendererTarget::FONT_TITLE, cardFontId(config.fontPointSize));
  const int16_t lineHeight = screen.target().lineHeight(fui::GfxRendererTarget::FONT_TITLE);
  fui::TextStyle textStyle = theme.bodyText;
  textStyle.font = fui::GfxRendererTarget::FONT_TITLE;
  textStyle.bold = false;
  textStyle.align = fui::TextAlign::Left;

  const int preferredGap = theme.spaceLg;
  const int gap = std::min(preferredGap, std::max(0, body.height - 2 * lineHeight));
  const int maxQuestionHeight = std::max(0, body.height - gap) / 2;
  textStyle.maxLines = flashcards::detail::cardTextLines(maxQuestionHeight, lineHeight);
  const int measuredQuestionHeight =
      textStyle.maxLines > 0 ? fui::measureWrappedText(screen.target(), frontText.c_str(), textStyle, body.width).height
                             : 0;
  const auto layout =
      flashcards::detail::cardTextLayout(body.height, preferredGap, lineHeight, measuredQuestionHeight + theme.spaceMd);
  const fui::Rect questionRect{body.x, body.y, body.width, layout.questionHeight};
  const fui::Rect answerRect{body.x, static_cast<int16_t>(questionRect.bottom() + layout.gap), body.width,
                             layout.answerHeight};
  const auto drawCardText = [&](const fui::Rect rect, const char* text, const bool topAligned) {
    textStyle.maxLines = flashcards::detail::cardTextLines(rect.height, lineHeight);
    if (textStyle.maxLines == 0 || rect.width <= 0) return;
    int16_t nextLineY = rect.y;
    fui::layoutText(screen.target(), rect, text, textStyle, [&](const char* line, fui::Rect lineRect) {
      if (topAligned) {
        lineRect.y = nextLineY;
        nextLineY = static_cast<int16_t>(nextLineY + lineRect.height);
      }
      fui::TextStyle lineStyle = textStyle;
      lineStyle.maxLines = 1;
      screen.target().text(lineRect, line, lineStyle);
    });
  };
  drawCardText(questionRect, frontText.c_str(), false);

  if (answerShown) {
    if (layout.gap > 0 && layout.answerHeight > 0) {
      const int16_t separatorY = static_cast<int16_t>(questionRect.bottom() + layout.gap / 2);
      screen.target().fill(
          fui::Rect{body.x, separatorY, body.width, static_cast<int16_t>(std::max<uint8_t>(theme.headerUnderline, 1))},
          fui::Paint::dither(fui::Color::LightGray));
    }
    drawCardText(answerRect, backText.c_str(), true);
  }
  uiTarget.setFont(fui::GfxRendererTarget::FONT_TITLE, uiScaleSpec().titleFontId);

  if (!answerShown) {
    screen.frame().hit(body, ACTION_REVEAL, 0, fui::InputTouch);
    return;
  }
  const int16_t leftWidth = body.width / 2;
  screen.frame().hit(fui::Rect{body.x, body.y, leftWidth, body.height}, ACTION_AGAIN, 0, fui::InputTouch);
  screen.frame().hit(fui::Rect{static_cast<int16_t>(body.x + leftWidth), body.y,
                               static_cast<int16_t>(body.width - leftWidth), body.height},
                     ACTION_GOOD, 0, fui::InputTouch);
}

void FlashcardReviewActivity::handleAction(const fui::ActionId action) {
  app.clearTapFlash();
  if (action == ACTION_DONE) {
    finishSession();
  } else if (action == ACTION_REVEAL) {
    reveal();
  } else if (action == ACTION_AGAIN) {
    rate(flashcards::Rating::Again);
  } else if (action == ACTION_GOOD) {
    rate(flashcards::Rating::Good);
  }
}

void FlashcardReviewActivity::finishSession() {
  ActivityResult result;
  result.isCancelled = phase != Phase::Complete;
  int64_t now = 0;
  if (phase != Phase::Error && !queue.cards.empty() && halClock.getUnixTime(now)) {
    const int32_t today = static_cast<int32_t>(now / 86400);
    const uint16_t introducedToday = queue.snapshotDay == today ? queue.introducedToday : 0;
    const uint16_t unseen = flashcards::detail::buildStudyQueues(
        queue.cards, now, today, introducedToday, config.newCardsPerDay, 0, queue.dueCards, queue.newCards);
    result.data = FlashcardCountsResult{static_cast<uint16_t>(queue.dueCards.size()),
                                        static_cast<uint16_t>(queue.newCards.size()), unseen};
  }
  setResult(std::move(result));
  finish();
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
  const LastRating previous{queue.cards[currentCardIndex],
                            phase,
                            duePosition,
                            newPosition,
                            currentPendingPosition,
                            currentCardIndex,
                            currentFromPending,
                            true};
  std::string detail;
  if (!flashcards::FlashcardStore::reviewCard(deck, queue.cards[currentCardIndex], now, rating, config, detail)) {
    showError(ErrorKind::Save, detail);
    return;
  }
  flashcards::FlashcardStore::noteHistoryAppend(queue);
  queue.reviewCount =
      flashcards::detail::countAfterReviewEvent(queue.reviewCount, flashcards::detail::ReviewCountEvent::Review);
  if (queue.firstReviewDay < 0) queue.firstReviewDay = static_cast<int32_t>(now / 86400);
  lastRating = previous;
  undoIndicatorUntil = 0;
  advance();
}

bool FlashcardReviewActivity::undoTouchEnabled() const {
  return config.undoBinding == flashcards::UndoBinding::TouchAndSides ||
         config.undoBinding == flashcards::UndoBinding::Touch;
}

bool FlashcardReviewActivity::undoSideEnabled(const MappedInputManager::Button button) const {
  return config.undoBinding == flashcards::UndoBinding::TouchAndSides ||
         config.undoBinding == flashcards::UndoBinding::BothSides ||
         (button == MappedInputManager::Button::Up && config.undoBinding == flashcards::UndoBinding::SideUp) ||
         (button == MappedInputManager::Button::Down && config.undoBinding == flashcards::UndoBinding::SideDown);
}

void FlashcardReviewActivity::undo() {
  if (!lastRating.valid || phase == Phase::Error) return;
  const auto ratedPhase = queue.cards[lastRating.cardIndex].phase;
  std::string detail;
  if (!flashcards::FlashcardStore::undoReview(deck, queue.cards[lastRating.cardIndex], lastRating.card, detail)) {
    showError(ErrorKind::Save, detail);
    return;
  }
  flashcards::detail::restorePendingAfterUndo(
      pendingLearningCards, lastRating.cardIndex, lastRating.pendingPosition, lastRating.fromPending,
      ratedPhase == flashcards::CardPhase::Learning || ratedPhase == flashcards::CardPhase::Relearning);
  flashcards::FlashcardStore::noteHistoryAppend(queue);
  queue.reviewCount =
      flashcards::detail::countAfterReviewEvent(queue.reviewCount, flashcards::detail::ReviewCountEvent::Undo);
  phase = lastRating.phase;
  duePosition = lastRating.duePosition;
  newPosition = lastRating.newPosition;
  currentFromPending = lastRating.fromPending;
  currentPendingPosition = lastRating.pendingPosition;
  currentCardIndex = lastRating.cardIndex;
  lastRating.valid = false;
  undoIndicatorUntil = millis() + 1500;
  int64_t now = 0;
  if (!halClock.getUnixTime(now)) {
    showError(ErrorKind::Clock, "RTC read failed while undoing review");
    return;
  }
  if (!loadCard(now)) return;
  LOG_DBG("FLASH", "Last rating undone: source_order=%lu",
          static_cast<unsigned long>(queue.cards[currentCardIndex].sourceOrder));
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
  currentUtcDay = static_cast<int32_t>(now / 86400);

  const bool baseCardsRemaining = duePosition < queue.dueCards.size() || newPosition < queue.newCards.size();
  const size_t learningPosition = flashcards::detail::nextLearningCardPosition(
      queue.cards, pendingLearningCards, now, config.learnAheadLimitMinutes, baseCardsRemaining);
  if (learningPosition < pendingLearningCards.size()) {
    currentFromPending = true;
    currentPendingPosition = learningPosition;
    currentCardIndex = pendingLearningCards[learningPosition];
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
  const bool newlyIntroduced = card.introducedDay < 0;
  if (!flashcards::FlashcardStore::introduceCard(deck, card, now, detail)) {
    showError(ErrorKind::Save, detail);
    return false;
  }
  if (newlyIntroduced) {
    flashcards::FlashcardStore::noteHistoryAppend(queue);
    const int32_t today = static_cast<int32_t>(now / 86400);
    if (queue.snapshotDay != today) {
      queue.snapshotDay = today;
      queue.introducedToday = 0;
    }
    if (queue.introducedToday < UINT16_MAX) ++queue.introducedToday;
  }
  if (newlyIntroduced && queue.unseenCount > 0) --queue.unseenCount;
  if (!flashcards::FlashcardStore::readCardText(deck, card, false, frontText, detail)) {
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
