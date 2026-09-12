#pragma once

#include <cstdint>

namespace flashcards {

enum class Rating : uint8_t { Again = 1, Good = 3 };

struct MemoryState {
  float stability = 0.0f;
  float difficulty = 0.0f;
};

struct SchedulingResult {
  MemoryState memory;
  float intervalDaysExact = 1.0f;
  uint32_t intervalDays = 1;
};

// Scheduling-only scalar FSRS-6 implementation. Parameter optimization and the
// Rust tensor/training stack are intentionally not part of the firmware.
class FsrsScheduler {
 public:
  static bool next(const MemoryState* current, uint32_t elapsedDays, Rating rating, float desiredRetention,
                   uint32_t maximumIntervalDays, SchedulingResult& result);
};

}  // namespace flashcards
