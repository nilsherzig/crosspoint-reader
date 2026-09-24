#include "FlashcardBackupState.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace flashcards {
namespace {
constexpr char MODULE[] = "FLASHBK";
constexpr char DIRECTORY[] = "/.crosspoint/flashcards";
constexpr char SLOT_A[] = "/.crosspoint/flashcards/backup-0.bin";
constexpr char SLOT_B[] = "/.crosspoint/flashcards/backup-1.bin";
constexpr uint32_t MAGIC = 0x314B4246;  // FBK1

void put32(uint8_t* dst, uint32_t value) {
  for (int i = 0; i < 4; ++i) dst[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t get32(const uint8_t* src) {
  return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

void put64(uint8_t* dst, uint64_t value) {
  put32(dst, static_cast<uint32_t>(value));
  put32(dst + 4, static_cast<uint32_t>(value >> 32));
}

uint64_t get64(const uint8_t* src) { return static_cast<uint64_t>(get32(src)) | (static_cast<uint64_t>(get32(src + 4)) << 32); }

uint32_t checksum(const uint8_t* data, size_t length) {
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < length; ++i) {
    if (i >= 20 && i < 24) continue;
    hash = (hash ^ data[i]) * 16777619U;
  }
  return hash;
}
}  // namespace

bool FlashcardBackupState::readSlot(const char* path) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return false;
  const uint64_t size = file.fileSize64();
  if (size < HEADER_SIZE || size > bytes.size() || file.read(bytes.data(), static_cast<size_t>(size)) != size ||
      get32(bytes.data()) != MAGIC || get32(bytes.data() + 20) != checksum(bytes.data(), size)) {
    LOG_ERR(MODULE, "Invalid backup counter slot: %s", path);
    return false;
  }
  const uint16_t entryCount = static_cast<uint16_t>(bytes[8] | (bytes[9] << 8));
  if (entryCount > entries.size() || size != HEADER_SIZE + entryCount * 12 ||
      (sequence != 0 && get32(bytes.data() + 4) <= sequence))
    return false;
  sequence = get32(bytes.data() + 4);
  count = entryCount;
  total = get32(bytes.data() + 12);
  nextReminder = get32(bytes.data() + 16);
  for (uint16_t i = 0; i < count; ++i) {
    entries[i].key = get64(bytes.data() + HEADER_SIZE + i * 12);
    entries[i].reviews = get32(bytes.data() + HEADER_SIZE + i * 12 + 8);
  }
  return true;
}

bool FlashcardBackupState::load() {
  const bool aExists = Storage.exists(SLOT_A);
  const bool bExists = Storage.exists(SLOT_B);
  if (!aExists && !bExists) return true;
  bool valid = false;
  if (aExists) valid = readSlot(SLOT_A);
  if (bExists) valid = readSlot(SLOT_B) || valid;
  if (!valid) LOG_ERR(MODULE, "No valid backup counter slot");
  return valid;
}

void FlashcardBackupState::observe(const uint64_t key, const uint32_t reviews) {
  for (uint16_t i = 0; i < count; ++i) {
    if (entries[i].key != key) continue;
    if (entries[i].reviews == reviews) return;
    total = detail::reconcileBackupReviews(total, entries[i].reviews, reviews);
    entries[i].reviews = reviews;
    dirty = true;
    return;
  }
  if (count < entries.size()) {
    entries[count++] = {key, reviews};
    dirty = true;
  } else {
    LOG_ERR(MODULE, "Too many decks for backup counter");
  }
}

bool FlashcardBackupState::save() {
  if (!dirty) return true;
  if (!Storage.exists(DIRECTORY) && !Storage.mkdir(DIRECTORY, true)) {
    LOG_ERR(MODULE, "Could not create backup counter directory");
    return false;
  }
  const uint32_t nextSequence = sequence + 1;
  const char* slot = (nextSequence & 1) ? SLOT_A : SLOT_B;
  const size_t length = HEADER_SIZE + count * 12;
  put32(bytes.data(), MAGIC);
  put32(bytes.data() + 4, nextSequence);
  bytes[8] = static_cast<uint8_t>(count);
  bytes[9] = static_cast<uint8_t>(count >> 8);
  bytes[10] = bytes[11] = 0;
  put32(bytes.data() + 12, total);
  put32(bytes.data() + 16, nextReminder);
  for (uint16_t i = 0; i < count; ++i) {
    put64(bytes.data() + HEADER_SIZE + i * 12, entries[i].key);
    put32(bytes.data() + HEADER_SIZE + i * 12 + 8, entries[i].reviews);
  }
  put32(bytes.data() + 20, checksum(bytes.data(), length));
  HalFile file;
  if (!Storage.openFileForWrite(MODULE, slot, file) || file.write(bytes.data(), length) != length || !file.close()) {
    LOG_ERR(MODULE, "Could not save backup counter");
    return false;
  }
  sequence = nextSequence;
  dirty = false;
  return true;
}

bool FlashcardBackupState::shouldPrompt(const uint32_t interval) const {
  return count > 0 && detail::backupReminderDue(total, nextReminder, interval);
}

void FlashcardBackupState::postpone(const uint32_t interval) {
  nextReminder = detail::saturatingAdd(total, interval);
  dirty = true;
}

void FlashcardBackupState::resetAfterBackup() {
  count = 0;
  total = 0;
  nextReminder = 0;
  dirty = true;
}

}  // namespace flashcards
