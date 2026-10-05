#pragma once

#include <ReadwiseDocument.h>

#include <string>

#include "activities/Activity.h"

/**
 * Explicit Readwise synchronization: connect Wi-Fi if needed, run the sync
 * engine (push queued ops, pull changed metadata, rebuild indexes, commit the
 * checkpoint), show the outcome.
 *
 * Follows KOReaderSyncActivity's shape: blocking network work on the main loop
 * task with RenderLock'd state updates, no cancellation once started, and the
 * WiFi teardown + silent restart in onExit() to shed heap fragmentation. A
 * failed sync preserves the previous checkpoint and the pending queue by
 * construction (that is the engine's contract, proven by the native tests).
 */
class ReadwiseSyncActivity final : public Activity {
 public:
  explicit ReadwiseSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadwiseSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override {
    return state == State::CONNECTING || state == State::SYNCING || state == State::DOWNLOADING_BODIES;
  }

 private:
  enum class State : uint8_t {
    CONNECTING,
    SYNCING,
    DOWNLOADING_BODIES,
    COMPLETE,
    FAILED,
    NO_TOKEN,
  };

  void onWifiSelectionComplete(bool connected);
  void performSync();
  void renderComplete() const;
  void renderDownloading() const;

  enum class DownloadStep : uint8_t { Article, Image, RateLimit };

  State state = State::CONNECTING;
  std::string statusMessage;
  uint16_t pushed = 0;
  uint16_t pulled = 0;
  uint16_t bodiesDone = 0;
  uint16_t bodiesTotal = 0;
  uint16_t bodiesFailed = 0;
  // What the body pass is doing right now. Copied out of the engine callback
  // because the title pointer there dies when the callback returns.
  DownloadStep downloadStep = DownloadStep::Article;
  uint16_t stepIndex = 0;
  uint16_t stepCount = 0;
  uint32_t currentWords = 0;
  readwise::Category currentCategory = readwise::Category::Unknown;
  char currentTitle[readwise::TITLE_CAP] = {};
  // Title of the first article that failed, named on the summary screen.
  std::string failedTitle;
  bool wifiActivated = false;
};
