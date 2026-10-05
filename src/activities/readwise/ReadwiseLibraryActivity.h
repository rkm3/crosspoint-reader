#pragma once

#include <NullReadwiseApi.h>
#include <ReadwiseSyncEngine.h>
#include <SdReadwiseFileStore.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiTabListActivity.h"

/**
 * Offline browser for the synced Readwise locations.
 *
 * Reads only the local index and metadata files -- no network. Later,
 * Shortlist, and Feed are tabs; each keeps its own selection and scroll.
 * Row 0 is "Sync now" (which pushes ReadwiseSyncActivity). A document row
 * opens a preamble; Confirm there opens the cached body or downloads it.
 * A long Confirm press queues an archive, which takes effect locally
 * immediately via rebuildLocal().
 *
 * Only one visible window of metadata is resident at a time: documents are
 * ~800 bytes each and a full 100-document cap would be ~80 KB.
 */
class ReadwiseLibraryActivity final : public UiTabListActivity {
 public:
  explicit ReadwiseLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiTabListActivity("ReadwiseLibrary", renderer, mappedInput, true) {
    // Three short labels. A full-slot pill would stretch across a third of
    // the screen; cap the pill and leave the equal-width slots where they are.
    tabPillMaxPad = 16;
  }

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
#ifdef CROSSPOINT_READWISE_ONLY
  // This library is the home screen in the Readwise-only build, so the home
  // gesture must not try to navigate to a home behind it -- that would replace
  // this activity with another copy of itself.
  bool isHomeActivity() const override { return true; }
#endif

 private:
  enum class State : uint8_t {
    LIST,
    DOWNLOADING,
    DOWNLOAD_FAILED,
  };

  int listCount() const override { return totalRows(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  const char* headerTitle() const override;
  void drawFooter() override;
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  int tabCount() const override { return LOCATION_COUNT; }
  int activeTab() const override { return locationIndex; }
  const char* tabLabel(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;

  void reloadCounts();
  void ensureWindow(int docIndex);
  const readwise::Document* docAt(int docIndex);
  void openDocument(const readwise::Document& doc);
  void startDownload(const readwise::Document& doc);
  void performDownload();
  void migrateLegacyTextBodies();
  static void sImageProgress(void* ctx, size_t done, size_t total);
  void queueMove(const readwise::Document& doc, readwise::Location target);
  // Long-pressing a location button sends the selected article there instead
  // of switching to that view. Returns true while the button is held, so the
  // caller swallows the frame.
  bool handleLocationHold(MappedInputManager::Button button, int targetIndex);
  void selectTab(int index);
  void jumpToLocation(int index);
  // Persists locationIndex so Back out of an article returns to this view.
  void rememberLocation();
  // List row under the tab band. -1 when the band itself is focused.
  int selectedRow() const { return ringPos() - 1; }
  // Direct-jump targets for the Left/Right buttons: from Later they lead to
  // Shortlist and Feed; from Shortlist or Feed the button for the current view
  // leads back to Later. The hint labels name the destination view.
  int leftTargetIndex() const { return locationIndex == 1 ? 0 : 1; }
  int rightTargetIndex() const { return locationIndex == 2 ? 0 : 2; }
  int totalRows() const { return 1 + docCount; }

  readwise::NullReadwiseApi nullApi;
  readwise::SdReadwiseFileStore store;
  std::unique_ptr<readwise::ReadwiseSyncEngine> engine;

  State state = State::LIST;

  // The library views. Index 0 (Later) is the default; leftTargetIndex and
  // rightTargetIndex encode the button mapping on boards that have Left/Right.
  static constexpr int LOCATION_COUNT = 3;
  static constexpr readwise::Location LOCATIONS[LOCATION_COUNT] = {
      readwise::Location::Later, readwise::Location::Shortlist, readwise::Location::Feed};
  int locationIndex = 0;
  uint16_t docCount = 0;

  // Sliding metadata window backing the visible rows.
  std::vector<readwise::Document> window;
  int windowStart = 0;

  // One scratch string for the row value (word count and date). list() reads
  // the pointer before it asks for the next row.
  char rowValue[32] = {};

  // Set when a download is pending/failed; the id of the document involved.
  std::string pendingDownloadId;
  std::string pendingDownloadTitle;
  std::string pendingDownloadRev;
  // Relative image srcs resolve against source_url, and the OPF wants the
  // author, so both are carried from the row rather than re-read later.
  std::string pendingDownloadSourceUrl;
  std::string pendingDownloadAuthor;
  bool pendingDownloadSeen = false;
  std::string statusMessage;
  // Set when a hold has already acted (archive, or a move to another view), so
  // the release that follows does not also open the document or switch views.
  bool holdActionTriggered = false;
  bool wifiActivated = false;
  // Back is also what navigates *into* this screen (from Settings, or out of a
  // managed article), and the button is often still held when the activity is
  // constructed. Without this, the trailing release acts again immediately and
  // bounces straight back out to where the user just came from.
  bool ignoreBackUntilReleased = false;
};
