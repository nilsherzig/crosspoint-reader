#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include "FlashcardStore.h"

namespace flashcards {
namespace detail {

constexpr uint32_t saturatingAdd(const uint32_t a, const uint32_t b) {
  return b > UINT32_MAX - a ? UINT32_MAX : a + b;
}

constexpr uint32_t reconcileBackupReviews(const uint32_t total, const uint32_t lastSeen, const uint32_t current) {
  return current >= lastSeen ? saturatingAdd(total, current - lastSeen)
                             : (lastSeen - current > total ? 0 : total - (lastSeen - current));
}

constexpr bool backupReminderDue(const uint32_t total, const uint32_t nextReminder, const uint32_t interval) {
  return total >= (nextReminder > interval ? nextReminder : interval);
}

}  // namespace detail

// Persisted per-deck review totals let a missed onExit (power loss) catch up
// from the history replay on the next deck-list load without double counting.
class FlashcardBackupState {
  struct Entry {
    uint64_t key = 0;
    uint32_t reviews = 0;
  };

  static constexpr size_t HEADER_SIZE = 24;
  std::array<Entry, FlashcardStore::MAX_DECKS> entries{};
  std::array<uint8_t, HEADER_SIZE + FlashcardStore::MAX_DECKS * 12> bytes{};
  uint32_t sequence = 0;
  uint32_t total = 0;
  uint32_t nextReminder = 0;
  uint16_t count = 0;
  bool dirty = false;

  bool readSlot(const char* path);

 public:
  bool load();
  void observe(uint64_t key, uint32_t reviews);
  bool save();
  bool shouldPrompt(uint32_t interval) const;
  void postpone(uint32_t interval);
  void resetAfterBackup();
};

}  // namespace flashcards
