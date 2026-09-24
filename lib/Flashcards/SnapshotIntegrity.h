#pragma once

#include <cstddef>
#include <cstdint>

namespace flashcards::detail {

inline uint32_t updateCrc(uint32_t crc, const uint8_t* data, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc;
}

inline uint32_t crc32(const uint8_t* data, const size_t length) {
  return updateCrc(0xFFFFFFFFU, data, length) ^ 0xFFFFFFFFU;
}

// A valid history record ends with its own CRC. Hashing that trailer as well
// would produce the same CRC residue for every valid 60-byte record.
inline uint32_t historyBoundaryFingerprint(const uint8_t* record) { return crc32(record, 56); }

inline uint32_t snapshotHeaderChecksum(const uint8_t* header) { return crc32(header, 48); }

}  // namespace flashcards::detail
