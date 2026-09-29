// Host benchmarks for flashcard loading:
//   1. flashcards::detail::buildStudyQueues over growing synthetic decks
//   2. FlashcardStore::loadStudyQueue (includes history replay) over growing review histories
// Measures wall time, number of heap allocations, and peak live heap bytes.
// Absolute times are host-specific; compare the ratio between sizes to see the scaling class.
//
// Storage runs against the in-memory fake in test/flashcards/stubs/HalStorage.h, so file I/O
// costs nothing here and the fake's own allocations (e.g. std::string paths) are host-only.

#include <FlashcardStore.h>
#include <StudyQueueBuilder.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "HalStorage.h"  // fake:: in-memory SD card

namespace {

// ---------------------------------------------------------------------------
// 1. Allocation counter
// ---------------------------------------------------------------------------

struct AllocStats {
  size_t allocs = 0;     // number of operator new calls
  size_t liveBytes = 0;  // bytes currently allocated and not yet freed
  size_t peakBytes = 0;  // highest liveBytes seen since the last reset
};

AllocStats stats;

// Extra bytes in front of every allocation, used to remember its size.
// Must be a multiple of the strictest alignment so the returned pointer stays aligned.
constexpr size_t HEADER = alignof(std::max_align_t);

}  // namespace

// Replacing these global functions makes EVERY `new` in this program (including the ones
// inside std::vector) go through our code. The linker picks ours over the standard library's.

void* operator new(std::size_t size) {
  // TODO(1a): Allocate `size + HEADER` bytes with std::malloc.
  //           If malloc returns nullptr, call std::abort() (operator new must never return nullptr).
  // TODO(1b): Store `size` at the start of the block:  *static_cast<size_t*>(block) = size;
  // TODO(1c): Update stats: allocs += 1, liveBytes += size, peakBytes = std::max(peakBytes, liveBytes).
  // TODO(1d): Return the address right after the header:  static_cast<char*>(block) + HEADER
  //
  // Placeholder so the file compiles and runs before you fill this in (counts nothing yet).
  // Once you add the header here, operator delete below MUST subtract it again.
  void* block = std::malloc(size + HEADER);
  if (!block) std::abort();

  *static_cast<size_t*>(block) = size;  // size in den header schreiben
  stats.allocs += 1;
  stats.liveBytes += size;
  stats.peakBytes = std::max(stats.peakBytes, stats.liveBytes);

  return static_cast<char*>(block) + HEADER;
}

void operator delete(void* ptr) noexcept {
  if (!ptr) return;
  // TODO(1e): Step back to the real block start:  char* block = static_cast<char*>(ptr) - HEADER;
  // TODO(1f): Read the stored size from the header and subtract it from stats.liveBytes.
  // TODO(1g): std::free(block)  (NOT ptr, that is not what malloc returned).

  char* block = static_cast<char*>(ptr) - HEADER;
  stats.liveBytes -= *reinterpret_cast<size_t*>(block);
  std::free(block);
}

// GCC calls this "sized" variant when it knows the size; just forward to the one above.
void operator delete(void* ptr, std::size_t) noexcept { operator delete(ptr); }

namespace {

void resetStats() {
  stats.allocs = 0;
  // Peak is measured relative to what is already allocated (e.g. the input deck).
  stats.peakBytes = stats.liveBytes;
}

// ---------------------------------------------------------------------------
// 2. Synthetic deck
// ---------------------------------------------------------------------------

constexpr int32_t TODAY = 20000;  // day number, same convention as StudyQueueBuilderBehaviorTests.cpp
constexpr int64_t NOW = static_cast<int64_t>(TODAY) * 86400 + 3600;

std::vector<flashcards::StudyCard> makeDeck(const uint32_t count) {
  std::vector<flashcards::StudyCard> cards;
  cards.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    flashcards::StudyCard card;
    card.sourceOrder = i;
    // TODO(2): Make the deck realistic: a mix of card states, e.g. for i % 10:
    //   0..5  -> reviewed card: initialized = true, phase = flashcards::CardPhase::Review,
    //            introducedDay = TODAY - 30, due = NOW + some offset (negative = overdue, positive = later).
    //            Vary the offset with i so std::sort has real work to do (not already sorted!).
    //   6     -> learning card: initialized = true, phase = flashcards::CardPhase::Learning, due = NOW + 300
    //   7..9  -> unseen card: leave the defaults (initialized = false, introducedDay = -1)
    //   Look at scheduledCard()/unseenCard() in StudyQueueBuilderBehaviorTests.cpp for examples.
    card.initialized = true;
    if (i % 10 <= 5) {
      card.phase = flashcards::CardPhase::Review;
      card.introducedDay = TODAY - 30;
      card.due = NOW + (i % 2 == 0 ? -6000 : 6000);
    } else if (i % 10 == 6) {
      card.phase = flashcards::CardPhase::Learning;
      card.due = NOW + 300;  // Learning card due in 5 minutes
    } else {
      card.initialized = false;  // Unseen card
      card.introducedDay = -1;
    }
    cards.push_back(card);
  }
  return cards;
}

// ---------------------------------------------------------------------------
// 3. One measurement
// ---------------------------------------------------------------------------

struct Result {
  double medianMicros = 0;
  size_t allocs = 0;
  size_t peakBytes = 0;
  size_t dueCount = 0;
  size_t newCount = 0;
};

Result measure(const uint32_t deckSize) {
  const std::vector<flashcards::StudyCard> cards = makeDeck(deckSize);  // created BEFORE resetStats, not counted
  constexpr int RUNS = 101;                                             // odd, so the median is one real sample
  std::vector<double> samples;
  samples.reserve(RUNS);
  Result result;

  for (int run = 0; run < RUNS; ++run) {
    // Fresh output vectors per run, like a cold deck open on the device.
    std::vector<uint16_t> dueCards;
    std::vector<uint16_t> newCards;

    const size_t liveBytesBefore = stats.liveBytes;  // remember for peakBytes calculation

    resetStats();
    const auto start = std::chrono::steady_clock::now();
    // TODO(3a): Call flashcards::detail::buildStudyQueues(cards, NOW, TODAY, introducedToday = 0,
    //           newCardsPerDay = 20, additionalNewCards = 0, dueCards, newCards).
    flashcards::detail::buildStudyQueues(cards, NOW, TODAY, 0, 20, 0, dueCards, newCards);
    const auto end = std::chrono::steady_clock::now();

    // TODO(3b): Push the elapsed time in microseconds into `samples`:
    //           std::chrono::duration<double, std::micro>(end - start).count()
    samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    // TODO(3c): On the first run only (run == 0), copy stats.allocs and
    //           stats.peakBytes - <liveBytes before the call> into result.
    //           Hint: remember stats.liveBytes right after resetStats().
    //           Also store dueCards.size() / newCards.size() so you can sanity-check the deck mix.
    if (run == 0) {
      result.allocs = stats.allocs;
      result.peakBytes = stats.peakBytes - liveBytesBefore;
      result.dueCount = dueCards.size();
      result.newCount = newCards.size();
    }
  }

  // TODO(3d): Sort `samples` and take the middle element as result.medianMicros.
  //           The median ignores outliers from the OS scheduler, unlike the average.
  std::sort(samples.begin(), samples.end());
  result.medianMicros = samples[samples.size() / 2];
  return result;
}

// ---------------------------------------------------------------------------
// 4. History replay scenarios
// ---------------------------------------------------------------------------
// replayHistory() lives in the anonymous namespace of FlashcardStore.cpp, so it cannot be called
// from here. We measure the public loadStudyQueue() instead and compare scenarios:
//   reviews = 0          -> baseline: load cached cards + build queues
//   reviews = H, cold    -> baseline + full replay of H history records
//   reviews = H, warm    -> baseline + snapshot load + replay of the (empty) tail
// replay cost ~= cold(H) - cold(0)

constexpr char DECK_PATH[] = "/flashcards/bench.csv";  // must be directly inside /flashcards
constexpr int64_t HISTORY_START = 1'700'000'000;       // unix seconds of the first review

struct Scenario {
  flashcards::DeckSummary deck;
  flashcards::Config config;
  int64_t openAt = 0;  // "now" passed to loadStudyQueue in the measurement
};

std::string makeCsv(const uint32_t cardCount) {
  std::string csv = "front,back\n";
  // TODO(4a): Append one line per card, e.g. "Question 17,Answer 17\n".
  //           Every front/back pair must be unique, otherwise the deck is rejected as invalid.
  //           Tip: csv.reserve(cardCount * 32) first, then csv += ...; std::to_string(i) turns a number into text.
  csv.reserve(cardCount * 32);
  for (uint32_t i = 0; i < cardCount; ++i) {
    char buffer[30];
    snprintf(buffer, sizeof(buffer), "frage %u,antwort %u\n", i, i);
    csv += buffer;
  }
  return csv;
}

// Builds the fake SD card state for one scenario. Everything here happens BEFORE measuring.
bool setupScenario(const uint32_t cardCount, const uint32_t reviewCount, const bool withSnapshot, Scenario& out) {
  fake::reset();  // empty in-memory SD card
  fake::add(DECK_PATH, makeCsv(cardCount));

  std::vector<flashcards::DeckSummary> decks;
  if (!flashcards::FlashcardStore::scanDecks(decks) || decks.size() != 1 || !decks.front().valid()) {
    std::printf("setup failed: deck scan (%s)\n", decks.empty() ? "no deck" : decks.front().error.c_str());
    return false;
  }
  out.deck = decks.front();

  std::string error;
  flashcards::StudyQueue queue;
  // TODO(4b): Load the queue once: FlashcardStore::loadStudyQueue(out.deck, HISTORY_START, out.config, queue, error).
  //           This first load imports the CSV into the card cache, so the measurement later only reads the cache.
  //           On failure: print error.c_str() and return false.
  if (!flashcards::FlashcardStore::loadStudyQueue(out.deck, HISTORY_START, out.config, queue, error)) {
    std::printf("setup failed: first load (%s)\n", error.c_str());
    return false;
  }

  // TODO(4c): Write `reviewCount` history records through the real API, so the file format is always valid:
  //             for i in 0..reviewCount:
  //               card = queue.cards[i % queue.cards.size()]
  //               now  = HISTORY_START + i * 60              (one review per minute, strictly increasing)
  //               FlashcardStore::reviewCard(out.deck, card, now, flashcards::Rating::Good, out.config, error)
  //               FlashcardStore::noteHistoryAppend(queue)   (keeps queue.historyBytes in sync with the file)
  //           reviewCard introduces unseen cards itself (that appends one extra Introduction record).
  //           Mix in some flashcards::Rating::Again (e.g. every 7th review) for realism.
  for (uint32_t i = 0; i < reviewCount; ++i) {
    flashcards::StudyCard& card = queue.cards[i % queue.cards.size()];
    const int64_t now = HISTORY_START + static_cast<int64_t>(i) * 60;
    const flashcards::Rating rating = i % 7 == 0 ? flashcards::Rating::Again : flashcards::Rating::Good;
    if (!flashcards::FlashcardStore::reviewCard(out.deck, card, now, rating, out.config, error)) {
      std::printf("setup failed: review %u (%s)\n", i, error.c_str());
      return false;
    }
    flashcards::FlashcardStore::noteHistoryAppend(queue);
  }
  out.openAt = HISTORY_START + static_cast<int64_t>(reviewCount) * 60 + 60;

  if (withSnapshot) {
    // TODO(4d): Warm scenario: reload into a FRESH StudyQueue at out.openAt (replays the full journal and
    //           records how many history bytes are covered), then FlashcardStore::saveStudySnapshot(out.deck, fresh).
    //           The next load then restores the snapshot and replays only records written after it.
    //           Same pattern as seedSnapshot() in FlashcardStoreIntegrationTests.cpp.
    flashcards::StudyQueue fresh;
    if (!flashcards::FlashcardStore::loadStudyQueue(out.deck, out.openAt, out.config, fresh, error) ||
        !flashcards::FlashcardStore::saveStudySnapshot(out.deck, fresh)) {
      std::printf("setup failed: snapshot (%s)\n", error.c_str());
      return false;
    }
  }
  return true;
}

struct LoadResult {
  double medianMicros = 0;
  size_t allocs = 0;
  size_t peakBytes = 0;
  uint32_t reviewCount = 0;  // queue.reviewCount after the load, sanity check
  bool ok = false;
};

LoadResult measureLoad(const Scenario& scenario) {
  constexpr int RUNS = 21;
  std::vector<double> samples;
  samples.reserve(RUNS);
  LoadResult result;

  for (int run = 0; run < RUNS; ++run) {
    flashcards::StudyQueue queue;  // fresh per run: a deck open starts with an empty queue
    std::string error;

    // TODO(5a): Same pattern as measure(): remember stats.liveBytes, resetStats(), take start time,
    //           call FlashcardStore::loadStudyQueue(scenario.deck, scenario.openAt, scenario.config, queue, error),
    //           take end time, push the microseconds into `samples`.
    //           Careful: `error` is a std::string. Only touch it AFTER `end`, otherwise its allocation is measured.
    // TODO(5b): On run 0 copy allocs, peakBytes, queue.reviewCount and ok into result.
    //           If the load fails, print error.c_str() and return result (ok = false).
    const size_t liveBytesBefore = stats.liveBytes;
    resetStats();
    const auto start = std::chrono::steady_clock::now();
    const bool ok =
        flashcards::FlashcardStore::loadStudyQueue(scenario.deck, scenario.openAt, scenario.config, queue, error);
    const auto end = std::chrono::steady_clock::now();
    samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());

    if (run == 0) {
      result.allocs = stats.allocs;
      result.peakBytes = stats.peakBytes - liveBytesBefore;
      result.reviewCount = queue.reviewCount;
      result.ok = ok;
    }
    if (!ok) {
      std::printf("load failed: %s\n", error.c_str());
      result.ok = false;
      return result;
    }
  }

  // TODO(5c): Median of samples, like in measure().
  std::sort(samples.begin(), samples.end());
  result.medianMicros = samples[samples.size() / 2];
  return result;
}

}  // namespace

int main() {
  std::printf("sizeof(StudyCard) = %zu bytes\n\n", sizeof(flashcards::StudyCard));

  // Doubling sizes: compare each row's time with the previous one.
  //   ~2x -> O(n), ~2.1-2.2x -> O(n log n), ~4x -> O(n^2)
  // Card indices are uint16_t, so decks are limited to 65535 cards.
  constexpr uint32_t SIZES[] = {500, 1000, 2000, 4000, 8000, 16000, 32000, 64000};

  std::printf("%8s %12s %8s %8s %12s %12s %8s %8s\n", "cards", "median_us", "ratio", "allocs", "peak_bytes",
              "peakBytesScale", "due cards", "new cards");
  double previous = 0;
  for (const uint32_t size : SIZES) {
    const Result r = measure(size);
    const double ratio = previous > 0 ? r.medianMicros / previous : 0;
    const double bytesRatio = size > 0 ? static_cast<double>(r.peakBytes) / size : 0;
    std::printf("%8u %12.1f %8.2f %8zu %12zu %12.2f %8zu %8zu\n", size, r.medianMicros, ratio, r.allocs, r.peakBytes,
                bytesRatio, r.dueCount, r.newCount);
    previous = r.medianMicros;
  }

  // History replay. Decks are capped at FlashcardStore::MAX_CARDS_PER_DECK (2000) cards.
  // One history record is 60 bytes; 36500 reviews ~ one year at 100 reviews/day ~ 2.2 MB.
  constexpr uint32_t CARDS = 2000;
  constexpr uint32_t REVIEWS[] = {0, 1000, 2000, 4000, 8000, 16000, 36500};

  std::printf("\nloadStudyQueue, %u cards\n", CARDS);
  std::printf("%8s %6s %12s %8s %8s %12s %8s\n", "reviews", "mode", "median_us", "ratio", "allocs", "peak_bytes",
              "replayed");
  for (const bool warm : {false, true}) {
    // TODO(6): For each entry of REVIEWS:
    //            Scenario scenario;
    //            if (!setupScenario(CARDS, reviews, warm, scenario)) return 1;
    //            const LoadResult r = measureLoad(scenario);
    //            if (!r.ok) return 1;
    //          then print one row like the table above (mode: warm ? "warm" : "cold").
    //          Ratio: compare with the previous row of the same mode. Skip it for reviews = 0,
    //          the first doubling (1000 -> 2000) is the first meaningful ratio.
    //          `replayed` = r.reviewCount; it should equal `reviews` in both modes.
    double previousLoad = 0;
    for (const uint32_t reviews : REVIEWS) {
      Scenario scenario;
      if (!setupScenario(CARDS, reviews, warm, scenario)) return 1;
      const LoadResult r = measureLoad(scenario);
      if (!r.ok) return 1;
      const double loadRatio = previousLoad > 0 && reviews > 1000 ? r.medianMicros / previousLoad : 0;
      std::printf("%8u %6s %12.1f %8.2f %8zu %12zu %8u\n", reviews, warm ? "warm" : "cold", r.medianMicros, loadRatio,
                  r.allocs, r.peakBytes, r.reviewCount);
      previousLoad = r.medianMicros;
    }
  }
  return 0;
}
