#pragma once

#include <FlashcardStore.h>

#include <cstddef>
#include <cstdint>
#include <string>

class FlashcardBackupUploader {
 public:
  using ProgressCallback = void (*)(void* ctx, size_t completed, size_t total);

  // Upload all CSV sources and all history journals to a timestamped Copyparty
  // directory. Never upload config.toml, which contains the backup credential.
  static bool upload(const flashcards::Config& config, int64_t timestamp, ProgressCallback progress, void* ctx,
                     std::string& error);
};
