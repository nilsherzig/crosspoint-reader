#include "FlashcardStore.h"

#include "CsvReader.h"

#include <BufferedFile.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace flashcards {
namespace {
constexpr char MODULE[] = "FLASH";
constexpr char CACHE_DIRECTORY[] = "/.crosspoint/flashcards";
constexpr uint32_t CACHE_MAGIC = 0x31444346;  // FCD1
constexpr uint16_t CACHE_VERSION = 1;
constexpr size_t CACHE_HEADER_SIZE = 36;
constexpr size_t CACHE_RECORD_SIZE = 36;
constexpr uint32_t HISTORY_MAGIC = 0x31484346;  // FCH1
constexpr uint16_t HISTORY_VERSION = 1;
constexpr size_t HISTORY_RECORD_SIZE = 60;
constexpr size_t BLOOM_BYTES = 8192;
constexpr size_t IO_BUFFER_BYTES = 1024;
constexpr size_t MAX_CARD_TEXT_BYTES = 16384;

struct CacheHeader {
  uint64_t sourceSize = 0;
  uint16_t fatDate = 0;
  uint16_t fatTime = 0;
  uint32_t cardCount = 0;
  uint32_t recordsBytes = 0;
  uint32_t textBytes = 0;
  uint32_t payloadCrc = 0;
};

enum class HistoryType : uint8_t { Introduction = 1, Review = 2 };

struct HistoryEvent {
  HistoryType type = HistoryType::Introduction;
  Rating rating = Rating::Again;
  Fingerprint fingerprint;
  MemoryState memory;
  int64_t timestamp = 0;
  int64_t due = 0;
  int32_t introducedDay = -1;
};

uint16_t getU16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1]) << 8;
}

uint32_t getU32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 |
         static_cast<uint32_t>(data[2]) << 16 | static_cast<uint32_t>(data[3]) << 24;
}

uint64_t getU64(const uint8_t* data) {
  return static_cast<uint64_t>(getU32(data)) | static_cast<uint64_t>(getU32(data + 4)) << 32;
}

int64_t getI64(const uint8_t* data) { return static_cast<int64_t>(getU64(data)); }

float getFloat(const uint8_t* data) {
  const uint32_t bits = getU32(data);
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

void putU16(uint8_t* data, const uint16_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
}

void putU32(uint8_t* data, const uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
  data[2] = static_cast<uint8_t>(value >> 16);
  data[3] = static_cast<uint8_t>(value >> 24);
}

void putU64(uint8_t* data, const uint64_t value) {
  putU32(data, static_cast<uint32_t>(value));
  putU32(data + 4, static_cast<uint32_t>(value >> 32));
}

void putFloat(uint8_t* data, const float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  putU32(data, bits);
}

uint32_t updateCrc(uint32_t crc, const uint8_t* data, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc;
}

uint32_t crc32(const uint8_t* data, const size_t length) { return updateCrc(0xFFFFFFFFU, data, length) ^ 0xFFFFFFFFU; }

bool readExact(serialization::BufferedFileReader& reader, void* data, const size_t length) {
  return reader.read(data, length) == length;
}

bool writeExact(HalFile& file, const void* data, const size_t length) {
  return file.write(data, length) == length;
}

void encodeHeader(const CacheHeader& header, uint8_t* data) {
  memset(data, 0, CACHE_HEADER_SIZE);
  putU32(data, CACHE_MAGIC);
  putU16(data + 4, CACHE_VERSION);
  putU16(data + 6, CACHE_HEADER_SIZE);
  putU64(data + 8, header.sourceSize);
  putU16(data + 16, header.fatDate);
  putU16(data + 18, header.fatTime);
  putU32(data + 20, header.cardCount);
  putU32(data + 24, header.recordsBytes);
  putU32(data + 28, header.textBytes);
  putU32(data + 32, header.payloadCrc);
}

bool decodeHeader(const uint8_t* data, CacheHeader& header) {
  if (getU32(data) != CACHE_MAGIC || getU16(data + 4) != CACHE_VERSION ||
      getU16(data + 6) != CACHE_HEADER_SIZE) {
    return false;
  }
  header.sourceSize = getU64(data + 8);
  header.fatDate = getU16(data + 16);
  header.fatTime = getU16(data + 18);
  header.cardCount = getU32(data + 20);
  header.recordsBytes = getU32(data + 24);
  header.textBytes = getU32(data + 28);
  header.payloadCrc = getU32(data + 32);
  return header.cardCount <= FlashcardStore::MAX_CARDS_PER_DECK &&
         header.recordsBytes == header.cardCount * CACHE_RECORD_SIZE;
}

void encodeCardRecord(const StudyCard& card, uint8_t* data) {
  putU64(data, card.fingerprint.first);
  putU64(data + 8, card.fingerprint.second);
  putU32(data + 16, card.sourceOrder);
  putU32(data + 20, card.frontOffset);
  putU32(data + 24, card.frontLength);
  putU32(data + 28, card.backOffset);
  putU32(data + 32, card.backLength);
}

void decodeCardRecord(const uint8_t* data, StudyCard& card) {
  card.fingerprint.first = getU64(data);
  card.fingerprint.second = getU64(data + 8);
  card.sourceOrder = getU32(data + 16);
  card.frontOffset = getU32(data + 20);
  card.frontLength = getU32(data + 24);
  card.backOffset = getU32(data + 28);
  card.backLength = getU32(data + 32);
}

void encodeHistoryEvent(const HistoryEvent& event, uint8_t* data) {
  memset(data, 0, HISTORY_RECORD_SIZE);
  putU32(data, HISTORY_MAGIC);
  putU16(data + 4, HISTORY_VERSION);
  putU16(data + 6, HISTORY_RECORD_SIZE);
  data[8] = static_cast<uint8_t>(event.type);
  data[9] = event.type == HistoryType::Review ? static_cast<uint8_t>(event.rating) : 0;
  putU64(data + 12, event.fingerprint.first);
  putU64(data + 20, event.fingerprint.second);
  putFloat(data + 28, event.memory.stability);
  putFloat(data + 32, event.memory.difficulty);
  putU64(data + 36, static_cast<uint64_t>(event.timestamp));
  putU64(data + 44, static_cast<uint64_t>(event.due));
  putU32(data + 52, static_cast<uint32_t>(event.introducedDay));
  putU32(data + 56, crc32(data, 56));
}

bool decodeHistoryEvent(const uint8_t* data, HistoryEvent& event) {
  if (getU32(data) != HISTORY_MAGIC || getU16(data + 4) != HISTORY_VERSION ||
      getU16(data + 6) != HISTORY_RECORD_SIZE || getU32(data + 56) != crc32(data, 56)) {
    return false;
  }
  event.type = static_cast<HistoryType>(data[8]);
  event.rating = static_cast<Rating>(data[9]);
  if ((event.type != HistoryType::Introduction && event.type != HistoryType::Review) ||
      (event.type == HistoryType::Review && event.rating != Rating::Again && event.rating != Rating::Good)) {
    return false;
  }
  event.fingerprint.first = getU64(data + 12);
  event.fingerprint.second = getU64(data + 20);
  event.memory.stability = getFloat(data + 28);
  event.memory.difficulty = getFloat(data + 32);
  event.timestamp = getI64(data + 36);
  event.due = getI64(data + 44);
  event.introducedDay = static_cast<int32_t>(getU32(data + 52));
  if (event.timestamp < 0 || event.due < 0 || event.introducedDay < 0) return false;
  return event.type != HistoryType::Review ||
         (std::isfinite(event.memory.stability) && event.memory.stability > 0.0f &&
          std::isfinite(event.memory.difficulty) && event.memory.difficulty >= 1.0f &&
          event.memory.difficulty <= 10.0f);

}

uint64_t fnvUpdate(uint64_t hash, const uint8_t* data, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

uint64_t deckKey(const char* name) {
  uint64_t hash = 1469598103934665603ULL;
  while (*name) {
    const uint8_t c = static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(*name++)));
    hash = fnvUpdate(hash, &c, 1);
  }
  return hash;
}

void makeCachePath(const uint64_t key, char* path, const size_t size) {
  snprintf(path, size, "%s/%016llx.cards", CACHE_DIRECTORY, static_cast<unsigned long long>(key));
}

void makeHistoryPath(const uint64_t key, char* path, const size_t size) {
  snprintf(path, size, "%s/%016llx.history", CACHE_DIRECTORY, static_cast<unsigned long long>(key));
}

bool hasCsvExtension(const char* name) {
  const size_t length = strlen(name);
  return length > 4 && name[length - 4] == '.' && std::tolower(static_cast<unsigned char>(name[length - 3])) == 'c' &&
         std::tolower(static_cast<unsigned char>(name[length - 2])) == 's' &&
         std::tolower(static_cast<unsigned char>(name[length - 1])) == 'v';
}

bool isValidUtf8(const std::string& text) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
  size_t i = 0;
  while (i < text.size()) {
    const uint8_t first = bytes[i++];
    if (first == 0) return false;
    if (first < 0x80) continue;
    uint8_t continuationCount = 0;
    uint32_t codepoint = 0;
    if (first >= 0xC2 && first <= 0xDF) {
      continuationCount = 1;
      codepoint = first & 0x1F;
    } else if (first >= 0xE0 && first <= 0xEF) {
      continuationCount = 2;
      codepoint = first & 0x0F;
    } else if (first >= 0xF0 && first <= 0xF4) {
      continuationCount = 3;
      codepoint = first & 0x07;
    } else {
      return false;
    }
    if (i + continuationCount > text.size()) return false;
    for (uint8_t j = 0; j < continuationCount; ++j) {
      const uint8_t next = bytes[i++];
      if ((next & 0xC0) != 0x80) return false;
      codepoint = (codepoint << 6) | (next & 0x3F);
    }
    if ((continuationCount == 2 && codepoint < 0x800) || (continuationCount == 3 && codepoint < 0x10000) ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) {
      return false;
    }
  }
  return true;
}

std::string deckDisplayName(const char* fileName) {
  const size_t length = strlen(fileName);
  return std::string(fileName, length - 4);
}

bool getSourceMetadata(const char* path, uint64_t& size, uint16_t& date, uint16_t& time) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return false;
  size = file.fileSize64();
  return file.getModifyDateTime(date, time);
}

bool readCacheHeader(const char* path, CacheHeader& header) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return false;
  uint8_t data[CACHE_HEADER_SIZE];
  if (file.read(data, sizeof(data)) != static_cast<int>(sizeof(data)) || !decodeHeader(data, header)) return false;
  const uint64_t expected = CACHE_HEADER_SIZE + static_cast<uint64_t>(header.recordsBytes) + header.textBytes;
  return file.fileSize64() == expected;
}

bool validateCachePayload(const char* path, const CacheHeader& header) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file) || !file.seek(CACHE_HEADER_SIZE)) return false;
  serialization::BufferedFileReader reader(file, IO_BUFFER_BYTES);
  auto buffer = makeUniqueNoThrow<uint8_t[]>(IO_BUFFER_BYTES);
  if (!buffer) {
    LOG_ERR(MODULE, "OOM: cache validation buffer");
    return false;
  }
  uint64_t remaining = static_cast<uint64_t>(header.recordsBytes) + header.textBytes;
  uint32_t crc = 0xFFFFFFFFU;
  while (remaining > 0) {
    const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, IO_BUFFER_BYTES));
    if (reader.read(buffer.get(), chunk) != chunk) return false;
    crc = updateCrc(crc, buffer.get(), chunk);
    remaining -= chunk;
  }
  return (crc ^ 0xFFFFFFFFU) == header.payloadCrc;
}

bool duplicateExists(const char* recordsPath, const uint32_t count, const Fingerprint& fingerprint) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, recordsPath, file)) return true;
  serialization::BufferedFileReader reader(file, IO_BUFFER_BYTES);
  uint8_t data[CACHE_RECORD_SIZE];
  StudyCard card;
  for (uint32_t i = 0; i < count; ++i) {
    if (!readExact(reader, data, sizeof(data))) return true;
    decodeCardRecord(data, card);
    if (card.fingerprint == fingerprint) return true;
  }
  return false;
}

bool copyInto(serialization::BufferedFileWriter& output, const char* inputPath, uint32_t& crc) {
  HalFile input;
  if (!Storage.openFileForRead(MODULE, inputPath, input)) return false;
  serialization::BufferedFileReader reader(input, IO_BUFFER_BYTES);
  auto buffer = makeUniqueNoThrow<uint8_t[]>(IO_BUFFER_BYTES);
  if (!buffer) {
    LOG_ERR(MODULE, "OOM: cache copy buffer");
    return false;
  }
  uint64_t remaining = input.fileSize64();
  while (remaining > 0) {
    const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, IO_BUFFER_BYTES));
    if (reader.read(buffer.get(), chunk) != chunk) return false;
    output.write(buffer.get(), chunk);
    crc = updateCrc(crc, buffer.get(), chunk);
    remaining -= chunk;
  }
  return true;
}

bool importDeck(const char* sourcePath, const uint64_t key, const uint64_t sourceSize, const uint16_t fatDate,
                const uint16_t fatTime, CacheHeader& imported, std::string& error) {
  char cachePath[96];
  char recordsPath[96];
  char textPath[96];
  char finalPath[96];
  makeCachePath(key, cachePath, sizeof(cachePath));
  snprintf(recordsPath, sizeof(recordsPath), "%s/%016llx.records.tmp", CACHE_DIRECTORY,
           static_cast<unsigned long long>(key));
  snprintf(textPath, sizeof(textPath), "%s/%016llx.text.tmp", CACHE_DIRECTORY, static_cast<unsigned long long>(key));
  snprintf(finalPath, sizeof(finalPath), "%s/%016llx.cards.tmp", CACHE_DIRECTORY,
           static_cast<unsigned long long>(key));
  Storage.remove(recordsPath);
  Storage.remove(textPath);
  Storage.remove(finalPath);

  HalFile source;
  HalFile records;
  HalFile text;
  if (!Storage.openFileForRead(MODULE, sourcePath, source) ||
      !Storage.openFileForWrite(MODULE, recordsPath, records) || !Storage.openFileForWrite(MODULE, textPath, text)) {
    error = "Could not open import files";
    return false;
  }

  auto bloom = makeUniqueNoThrow<uint8_t[]>(BLOOM_BYTES);
  if (!bloom) {
    LOG_ERR(MODULE, "OOM: %u-byte duplicate filter", static_cast<unsigned>(BLOOM_BYTES));
    error = "Not enough memory to import deck";
    records.close();
    text.close();
    Storage.remove(recordsPath);
    Storage.remove(textPath);
    return false;
  }
  memset(bloom.get(), 0, BLOOM_BYTES);

  serialization::BufferedFileReader sourceReader(source, IO_BUFFER_BYTES);
  serialization::BufferedFileWriter recordWriter(records, IO_BUFFER_BYTES);
  serialization::BufferedFileWriter textWriter(text, IO_BUFFER_BYTES);
  const auto readCsvByte = [](void* context) -> int {
    uint8_t byte;
    return static_cast<serialization::BufferedFileReader*>(context)->read(&byte, 1) == 1 ? byte : -1;
  };
  CsvReader csv(readCsvByte, &sourceReader);
  std::vector<std::string> fields;
  fields.reserve(64);
  std::string csvError;
  size_t frontIndex = std::numeric_limits<size_t>::max();
  size_t backIndex = std::numeric_limits<size_t>::max();
  uint32_t cardCount = 0;
  uint32_t rowNumber = 0;
  bool ok = true;

  while (ok) {
    const CsvReader::Result result = csv.readRow(fields, csvError);
    if (result == CsvReader::Result::End) break;
    ++rowNumber;
    if (result == CsvReader::Result::Error) {
      error = "Row " + std::to_string(rowNumber) + ": " + csvError;
      ok = false;
      break;
    }

    if (rowNumber == 1) {
      for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i] == "front") {
          if (frontIndex != std::numeric_limits<size_t>::max()) ok = false;
          frontIndex = i;
        } else if (fields[i] == "back") {
          if (backIndex != std::numeric_limits<size_t>::max()) ok = false;
          backIndex = i;
        }
      }
      if (!ok || frontIndex == std::numeric_limits<size_t>::max() || backIndex == std::numeric_limits<size_t>::max()) {
        error = "CSV header must contain one front and one back column";
        ok = false;
      }
      continue;
    }

    bool emptyRow = true;
    for (const auto& field : fields) emptyRow &= field.empty();
    if (emptyRow) continue;
    if (frontIndex >= fields.size() || backIndex >= fields.size() || fields[frontIndex].empty() || fields[backIndex].empty()) {
      error = "Row " + std::to_string(rowNumber) + ": front and back are required";
      ok = false;
      break;
    }
    if (fields[frontIndex].size() > MAX_CARD_TEXT_BYTES || fields[backIndex].size() > MAX_CARD_TEXT_BYTES) {
      error = "Row " + std::to_string(rowNumber) + ": card text exceeds 16384 bytes";
      ok = false;
      break;
    }
    if (!isValidUtf8(fields[frontIndex]) || !isValidUtf8(fields[backIndex])) {
      error = "Row " + std::to_string(rowNumber) + ": card text is not valid UTF-8";
      ok = false;
      break;
    }
    if (cardCount >= FlashcardStore::MAX_CARDS_PER_DECK) {
      error = "Deck exceeds 2000 cards";
      ok = false;
      break;
    }

    StudyCard card;
    card.fingerprint = FlashcardStore::fingerprint(fields[frontIndex], fields[backIndex]);
    const uint32_t bitA = static_cast<uint32_t>(card.fingerprint.first) % (BLOOM_BYTES * 8);
    const uint32_t bitB = static_cast<uint32_t>(card.fingerprint.first >> 32) % (BLOOM_BYTES * 8);
    const uint32_t bitC = static_cast<uint32_t>(card.fingerprint.second) % (BLOOM_BYTES * 8);
    const auto isBitSet = [&](const uint32_t bit) { return (bloom[bit / 8] & (1U << (bit % 8))) != 0; };
    if (isBitSet(bitA) && isBitSet(bitB) && isBitSet(bitC)) {
      if (!recordWriter.flush() || duplicateExists(recordsPath, cardCount, card.fingerprint)) {
        error = "Row " + std::to_string(rowNumber) + ": duplicate front/back pair";
        ok = false;
        break;
      }
    }
    bloom[bitA / 8] |= static_cast<uint8_t>(1U << (bitA % 8));
    bloom[bitB / 8] |= static_cast<uint8_t>(1U << (bitB % 8));
    bloom[bitC / 8] |= static_cast<uint8_t>(1U << (bitC % 8));

    card.sourceOrder = cardCount;
    card.frontOffset = static_cast<uint32_t>(textWriter.position());
    card.frontLength = fields[frontIndex].size();
    textWriter.write(fields[frontIndex].data(), fields[frontIndex].size());
    card.backOffset = static_cast<uint32_t>(textWriter.position());
    card.backLength = fields[backIndex].size();
    textWriter.write(fields[backIndex].data(), fields[backIndex].size());
    uint8_t record[CACHE_RECORD_SIZE];
    encodeCardRecord(card, record);
    recordWriter.write(record, sizeof(record));
    ++cardCount;
  }

  if (rowNumber == 0) {
    error = "CSV is empty";
    ok = false;
  }
  ok &= recordWriter.flush() && textWriter.flush();
  const uint32_t textBytes = static_cast<uint32_t>(textWriter.position());
  records.close();
  text.close();

  uint16_t finalDate = 0;
  uint16_t finalTime = 0;
  if (!source.getModifyDateTime(finalDate, finalTime) || source.fileSize64() != sourceSize || finalDate != fatDate ||
      finalTime != fatTime) {
    error = "CSV changed during import; try again";
    ok = false;
  }
  if (!ok) {
    Storage.remove(recordsPath);
    Storage.remove(textPath);
    return false;
  }

  CacheHeader header{sourceSize, fatDate, fatTime, cardCount, cardCount * CACHE_RECORD_SIZE, textBytes, 0};
  HalFile finalFile;
  if (!Storage.openFileForWrite(MODULE, finalPath, finalFile)) {
    error = "Could not create deck cache";
    Storage.remove(recordsPath);
    Storage.remove(textPath);
    return false;
  }
  uint8_t headerData[CACHE_HEADER_SIZE];
  encodeHeader(header, headerData);
  bool copied = writeExact(finalFile, headerData, sizeof(headerData));
  uint32_t payloadCrc = 0xFFFFFFFFU;
  {
    serialization::BufferedFileWriter finalWriter(finalFile, IO_BUFFER_BYTES);
    copied &= copyInto(finalWriter, recordsPath, payloadCrc);
    copied &= copyInto(finalWriter, textPath, payloadCrc);
    copied &= finalWriter.flush();
  }
  header.payloadCrc = payloadCrc ^ 0xFFFFFFFFU;
  encodeHeader(header, headerData);
  copied &= finalFile.seek(0) && writeExact(finalFile, headerData, sizeof(headerData));
  finalFile.flush();
  finalFile.close();
  Storage.remove(recordsPath);
  Storage.remove(textPath);
  if (!copied) {
    error = "Short write while creating deck cache";
    Storage.remove(finalPath);
    return false;
  }

  Storage.remove(cachePath);
  if (!Storage.rename(finalPath, cachePath)) {
    error = "Could not install deck cache";
    Storage.remove(finalPath);
    return false;
  }
  imported = header;
  return true;
}

bool ensureImported(const char* sourcePath, const uint64_t key, CacheHeader& header, std::string& error) {
  uint64_t sourceSize = 0;
  uint16_t fatDate = 0;
  uint16_t fatTime = 0;
  if (!getSourceMetadata(sourcePath, sourceSize, fatDate, fatTime)) {
    error = "Could not read CSV metadata";
    return false;
  }
  char cachePath[96];
  makeCachePath(key, cachePath, sizeof(cachePath));
  if (readCacheHeader(cachePath, header) && header.sourceSize == sourceSize && header.fatDate == fatDate &&
      header.fatTime == fatTime) {
    return true;
  }
  LOG_INF(MODULE, "Importing %s", sourcePath);
  return importDeck(sourcePath, key, sourceSize, fatDate, fatTime, header, error);
}

char* trim(char* value) {
  while (*value && std::isspace(static_cast<unsigned char>(*value))) ++value;
  char* end = value + strlen(value);
  while (end > value && std::isspace(static_cast<unsigned char>(end[-1]))) --end;
  *end = '\0';
  return value;
}

bool parseUnsigned(const char* value, uint32_t& output) {
  if (!*value || *value == '-') return false;
  errno = 0;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (errno != 0 || end == value || *trim(end) != '\0' || parsed > UINT32_MAX) return false;
  output = static_cast<uint32_t>(parsed);
  return true;
}

bool parseFloat(const char* value, float& output) {
  errno = 0;
  char* end = nullptr;
  const float parsed = strtof(value, &end);
  if (errno != 0 || end == value || *trim(end) != '\0' || !std::isfinite(parsed)) return false;
  output = parsed;
  return true;
}

StudyCard* findCard(std::vector<StudyCard>& cards, const Fingerprint& fingerprint) {
  const auto position = std::lower_bound(cards.begin(), cards.end(), fingerprint,
                                         [](const StudyCard& card, const Fingerprint& value) {
                                           return card.fingerprint < value;
                                         });
  return position != cards.end() && position->fingerprint == fingerprint ? &*position : nullptr;
}

bool appendHistory(const uint64_t key, const HistoryEvent& event, std::string& error) {
  char path[96];
  makeHistoryPath(key, path, sizeof(path));
  HalFile history;
  if (!Storage.openFileForAppend(MODULE, path, history)) {
    error = "Could not open review history";
    return false;
  }
  uint8_t data[HISTORY_RECORD_SIZE];
  encodeHistoryEvent(event, data);
  if (!writeExact(history, data, sizeof(data))) {
    LOG_ERR(MODULE, "Short history write");
    error = "Could not save review";
    return false;
  }
  history.flush();
  return true;
}

bool replayHistory(const uint64_t key, const int32_t today, StudyQueue& queue, std::string& error) {
  char path[96];
  makeHistoryPath(key, path, sizeof(path));
  if (!Storage.exists(path)) return true;

  HalFile history;
  if (!Storage.openFileForRead(MODULE, path, history)) {
    error = "Could not read review history";
    return false;
  }
  serialization::BufferedFileReader reader(history, IO_BUFFER_BYTES);
  uint8_t data[HISTORY_RECORD_SIZE];
  size_t validBytes = 0;
  bool damagedTail = false;
  while (reader.read(data, sizeof(data)) == sizeof(data)) {
    HistoryEvent event;
    if (!decodeHistoryEvent(data, event)) {
      damagedTail = true;
      break;
    }
    validBytes += sizeof(data);
    if (event.type == HistoryType::Introduction && event.introducedDay == today &&
        queue.introducedToday < UINT16_MAX) {
      ++queue.introducedToday;
    }
    StudyCard* card = findCard(queue.cards, event.fingerprint);
    if (!card) continue;
    card->introducedDay = event.introducedDay;
    if (event.type == HistoryType::Review) {
      card->memory = event.memory;
      card->lastReview = event.timestamp;
      card->due = event.due;
      card->initialized = true;
    }
  }
  if (validBytes != history.fileSize64()) damagedTail = true;
  if (!damagedTail) return true;

  LOG_ERR(MODULE, "Discarding damaged history tail at %u", static_cast<unsigned>(validBytes));
  history.close();
  HalFile writable;
  if (!Storage.openFileForAppend(MODULE, path, writable) || !writable.truncate(validBytes)) {
    error = "Review history is damaged and could not be repaired";
    return false;
  }
  return true;
}

bool loadCards(const char* path, const CacheHeader& header, StudyQueue& queue, std::string& error) {
  if (!validateCachePayload(path, header)) {
    error = "Deck cache is damaged; reopen the deck to reimport it";
    return false;
  }
  HalFile cache;
  if (!Storage.openFileForRead(MODULE, path, cache) || !cache.seek(CACHE_HEADER_SIZE)) {
    error = "Could not open deck cache";
    return false;
  }
  queue.cards.clear();
  queue.cards.reserve(header.cardCount);
  serialization::BufferedFileReader reader(cache, IO_BUFFER_BYTES);
  uint8_t data[CACHE_RECORD_SIZE];
  for (uint32_t i = 0; i < header.cardCount; ++i) {
    if (!readExact(reader, data, sizeof(data))) {
      error = "Deck cache ended unexpectedly";
      return false;
    }
    StudyCard card;
    decodeCardRecord(data, card);
    if (card.frontLength > MAX_CARD_TEXT_BYTES || card.backLength > MAX_CARD_TEXT_BYTES ||
        static_cast<uint64_t>(card.frontOffset) + card.frontLength > header.textBytes ||
        static_cast<uint64_t>(card.backOffset) + card.backLength > header.textBytes) {
      error = "Deck cache contains invalid card offsets";
      return false;
    }
    queue.cards.push_back(card);
  }
  std::sort(queue.cards.begin(), queue.cards.end(),
            [](const StudyCard& left, const StudyCard& right) { return left.fingerprint < right.fingerprint; });
  return true;
}

}  // namespace

Fingerprint FlashcardStore::fingerprint(const std::string& front, const std::string& back) {
  uint64_t first = 1469598103934665603ULL;
  uint64_t second = 1099511628211ULL;
  uint8_t length[8];
  putU64(length, front.size());
  first = fnvUpdate(first, length, sizeof(length));
  first = fnvUpdate(first, reinterpret_cast<const uint8_t*>(front.data()), front.size());
  putU64(length, back.size());
  first = fnvUpdate(first, length, sizeof(length));
  first = fnvUpdate(first, reinterpret_cast<const uint8_t*>(back.data()), back.size());

  second = fnvUpdate(second, reinterpret_cast<const uint8_t*>(back.data()), back.size());
  putU64(length, front.size() ^ (back.size() << 1));
  second = fnvUpdate(second, length, sizeof(length));
  second = fnvUpdate(second, reinterpret_cast<const uint8_t*>(front.data()), front.size());
  return Fingerprint{first, second};
}

bool FlashcardStore::loadConfig(Config& config, std::string& error) {
  config = Config{};
  error.clear();
  if (!Storage.exists(CONFIG_PATH)) return true;

  HalFile file;
  if (!Storage.openFileForRead(MODULE, CONFIG_PATH, file)) {
    error = "Could not read config.toml";
    return false;
  }
  serialization::BufferedFileReader reader(file, 256);
  char line[160];
  size_t length = 0;
  uint16_t lineNumber = 1;
  while (true) {
    uint8_t byte = 0;
    const int value = reader.read(&byte, 1) == 1 ? byte : -1;
    if (value >= 0 && value != '\n' && value != '\r') {
      if (length + 1 >= sizeof(line)) {
        error = "config.toml line " + std::to_string(lineNumber) + " is too long";
        return false;
      }
      line[length++] = static_cast<char>(value);
      continue;
    }
    if (length > 0) {
      line[length] = '\0';
      if (char* comment = strchr(line, '#')) *comment = '\0';
      char* content = trim(line);
      if (*content) {
        char* equals = strchr(content, '=');
        if (!equals) {
          error = "config.toml line " + std::to_string(lineNumber) + " needs =";
          return false;
        }
        *equals = '\0';
        const char* key = trim(content);
        char* setting = trim(equals + 1);
        uint32_t unsignedValue = 0;
        float floatValue = 0.0f;
        if (strcmp(key, "new_cards_per_day") == 0 && parseUnsigned(setting, unsignedValue) && unsignedValue <= 1000) {
          config.newCardsPerDay = static_cast<uint16_t>(unsignedValue);
        } else if (strcmp(key, "desired_retention") == 0 && parseFloat(setting, floatValue) && floatValue >= 0.70f &&
                   floatValue <= 0.99f) {
          config.desiredRetention = floatValue;
        } else if (strcmp(key, "maximum_interval_days") == 0 && parseUnsigned(setting, unsignedValue) &&
                   unsignedValue >= 1 && unsignedValue <= 365000) {
          config.maximumIntervalDays = unsignedValue;
        } else {
          error = "Invalid setting on config.toml line " + std::to_string(lineNumber);
          return false;
        }
      }
      length = 0;
    }
    if (value < 0) break;
    if (value == '\n') ++lineNumber;
  }
  return true;
}

bool FlashcardStore::scanDecks(std::vector<DeckSummary>& decks) {
  decks.clear();
  decks.reserve(MAX_DECKS);
  if (!Storage.exists(SOURCE_DIRECTORY) && !Storage.mkdir(SOURCE_DIRECTORY, true)) {
    LOG_ERR(MODULE, "Could not create %s", SOURCE_DIRECTORY);
    return false;
  }
  if (!Storage.exists(CACHE_DIRECTORY) && !Storage.mkdir(CACHE_DIRECTORY, true)) {
    LOG_ERR(MODULE, "Could not create %s", CACHE_DIRECTORY);
    return false;
  }

  HalFile directory;
  if (!Storage.openFileForRead(MODULE, SOURCE_DIRECTORY, directory)) return false;
  while (decks.size() < MAX_DECKS) {
    HalFile entry = directory.openNextFile();
    if (!entry) break;
    char fileName[192];
    if (entry.isDirectory() || entry.getName(fileName, sizeof(fileName)) == 0 || !hasCsvExtension(fileName)) continue;

    DeckSummary deck;
    deck.name = deckDisplayName(fileName);
    deck.sourcePath = std::string(SOURCE_DIRECTORY) + "/" + fileName;
    deck.key = deckKey(fileName);
    CacheHeader header;
    if (ensureImported(deck.sourcePath.c_str(), deck.key, header, deck.error)) deck.cardCount = header.cardCount;
    decks.push_back(std::move(deck));
  }
  std::sort(decks.begin(), decks.end(), [](const DeckSummary& left, const DeckSummary& right) {
    return left.name < right.name;
  });
  return true;
}

bool FlashcardStore::loadStudyQueue(const DeckSummary& deck, const int64_t now, const Config& config,
                                    StudyQueue& queue, std::string& error) {
  queue = StudyQueue{};
  error.clear();
  CacheHeader header;
  if (!ensureImported(deck.sourcePath.c_str(), deck.key, header, error)) return false;
  char cachePath[96];
  makeCachePath(deck.key, cachePath, sizeof(cachePath));
  if (!loadCards(cachePath, header, queue, error)) {
    Storage.remove(cachePath);
    CacheHeader rebuilt;
    if (!ensureImported(deck.sourcePath.c_str(), deck.key, rebuilt, error) ||
        !loadCards(cachePath, rebuilt, queue, error)) {
      return false;
    }
  }

  const int32_t today = static_cast<int32_t>(now / 86400);
  if (!replayHistory(deck.key, today, queue, error)) return false;
  queue.dueCards.reserve(queue.cards.size());
  queue.newCards.reserve(queue.cards.size());

  std::vector<uint16_t> unseen;
  unseen.reserve(queue.cards.size());
  for (uint16_t i = 0; i < queue.cards.size(); ++i) {
    const StudyCard& card = queue.cards[i];
    if (card.initialized) {
      if (card.due <= now) queue.dueCards.push_back(i);
    } else if (card.introducedDay == today) {
      queue.newCards.push_back(i);
    } else if (card.introducedDay >= 0) {
      queue.dueCards.push_back(i);
    } else {
      unseen.push_back(i);
    }
  }

  const uint16_t remaining = queue.introducedToday >= config.newCardsPerDay
                                 ? 0
                                 : static_cast<uint16_t>(config.newCardsPerDay - queue.introducedToday);
  std::sort(unseen.begin(), unseen.end(), [&](const uint16_t left, const uint16_t right) {
    return queue.cards[left].sourceOrder < queue.cards[right].sourceOrder;
  });
  for (size_t i = 0; i < std::min<size_t>(remaining, unseen.size()); ++i) queue.newCards.push_back(unseen[i]);

  std::sort(queue.dueCards.begin(), queue.dueCards.end(), [&](const uint16_t left, const uint16_t right) {
    const StudyCard& a = queue.cards[left];
    const StudyCard& b = queue.cards[right];
    const int64_t aDue = a.initialized ? a.due : 0;
    const int64_t bDue = b.initialized ? b.due : 0;
    return aDue != bDue ? aDue < bDue : a.sourceOrder < b.sourceOrder;
  });
  std::sort(queue.newCards.begin(), queue.newCards.end(), [&](const uint16_t left, const uint16_t right) {
    return queue.cards[left].sourceOrder < queue.cards[right].sourceOrder;
  });
  return true;
}

bool FlashcardStore::readCardText(const DeckSummary& deck, const StudyCard& card, const bool back,
                                  std::string& text, std::string& error) {
  char path[96];
  makeCachePath(deck.key, path, sizeof(path));
  CacheHeader header;
  if (!readCacheHeader(path, header)) {
    error = "Could not read deck cache";
    return false;
  }
  const uint32_t offset = back ? card.backOffset : card.frontOffset;
  const uint32_t length = back ? card.backLength : card.frontLength;
  if (length > MAX_CARD_TEXT_BYTES || static_cast<uint64_t>(offset) + length > header.textBytes) {
    error = "Card text offset is invalid";
    return false;
  }
  HalFile cache;
  const uint64_t absolute = CACHE_HEADER_SIZE + header.recordsBytes + offset;
  if (!Storage.openFileForRead(MODULE, path, cache) || !cache.seek64(absolute)) {
    error = "Could not seek deck cache";
    return false;
  }
  text.resize(length);
  if (length > 0 && cache.read(text.data(), length) != static_cast<int>(length)) {
    error = "Could not read card text";
    return false;
  }
  return true;
}

bool FlashcardStore::introduceCard(const DeckSummary& deck, StudyCard& card, const int64_t now,
                                   std::string& error) {
  if (card.introducedDay >= 0) return true;
  const int32_t today = static_cast<int32_t>(now / 86400);
  const HistoryEvent event{HistoryType::Introduction, Rating::Again, card.fingerprint, card.memory, now, now, today};
  if (!appendHistory(deck.key, event, error)) return false;
  card.introducedDay = today;
  return true;
}

bool FlashcardStore::reviewCard(const DeckSummary& deck, StudyCard& card, const int64_t now, const Rating rating,
                                const Config& config, std::string& error) {
  if (card.introducedDay < 0 && !introduceCard(deck, card, now, error)) return false;
  const uint32_t elapsedDays = card.initialized && now > card.lastReview
                                   ? static_cast<uint32_t>((now - card.lastReview) / 86400)
                                   : 0;
  SchedulingResult result;
  const MemoryState* current = card.initialized ? &card.memory : nullptr;
  if (!FsrsScheduler::next(current, elapsedDays, rating, config.desiredRetention, config.maximumIntervalDays,
                           result)) {
    LOG_ERR(MODULE, "FSRS rejected card state");
    error = "Could not schedule card";
    return false;
  }
  const int64_t due = rating == Rating::Again ? now : now + static_cast<int64_t>(result.intervalDays) * 86400;
  const HistoryEvent event{HistoryType::Review, rating, card.fingerprint, result.memory, now, due,
                           card.introducedDay};
  if (!appendHistory(deck.key, event, error)) return false;
  card.memory = result.memory;
  card.lastReview = now;
  card.due = due;
  card.initialized = true;
  return true;
}

}  // namespace flashcards
