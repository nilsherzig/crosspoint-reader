#pragma once

#include <BoardConfig.h>

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO

#include <FlashcardStore.h>

#include "activities/Activity.h"

class FlashcardBackupActivity final : public Activity {
  enum class State { Connecting, Uploading, Complete, Failed };
  flashcards::Config config;
  State state = State::Connecting;
  size_t completed = 0;
  size_t total = 0;

  void onWifiSelectionComplete(bool connected);
  void setState(State next);
  static void onProgress(void* ctx, size_t completed, size_t total);

 public:
  FlashcardBackupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardBackup", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};

#endif
