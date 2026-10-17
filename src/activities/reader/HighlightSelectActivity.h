#pragma once

#include <Epub/Page.h>
#include <ReadwiseDocument.h>

#include <memory>
#include <vector>

#include "activities/Activity.h"

class Section;

// Range selection over a managed article. Touch places the start, a drag or
// the side keys extend it, and Confirm (or a second tap on the range) queues
// the quote. Before a selection starts, the side page buttons shift the
// visible lines by three so a passage that crosses a page break can be
// selected on one screen. That shift is a temporary view: the reader's saved
// page does not change, and Back, Home, or a saved quote returns to it.
class HighlightSelectActivity final : public Activity {
 public:
  HighlightSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Section* section, int pageIndex,
                          int marginLeft, int marginTop, const char* documentId);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  struct WordBox {
    int16_t x;
    int16_t y;
    int16_t width;
    uint16_t row;
    const char* text;
    EpdFontFamily::Style style;
  };

  struct HeldPage {
    int index = -1;
    std::unique_ptr<Page> page;
  };

  struct PlacedLine {
    // PageLine::render is not const. The page that owns the line stays alive
    // for the whole activity.
    PageLine* line = nullptr;
    int16_t y = 0;
    int pageIndex = -1;
  };

  static constexpr int kMaxHeldPages = 3;
  static constexpr int kMaxPlacedLines = 80;

  bool rebuildWindow();
  bool holdPage(int index);
  Page* heldPage(int index) const;
  void releasePagesOutside(int firstPage, int lastPage);
  uint16_t countTextLines(const Page& page) const;
  void drawPlacedLines() const;
  void nudge(int direction);
  void extractWords();
  int wordAt(int x, int y) const;
  int closestInRow(uint16_t row, int centerX) const;
  void moveEnd(int delta);
  void moveVertical(int direction);
  bool inRange(int index) const;
  void commit();

  Section* section = nullptr;
  const int pageIndex;
  const int marginLeft;
  const int marginTop;
  char documentId[readwise::ID_CAP] = {};

  int fontId = 0;
  int lineHeight = 0;
  int shift = 0;
  uint16_t anchorLine = 0;
  uint16_t viewportLines = 0;
  HeldPage held[kMaxHeldPages];
  int heldCount = 0;
  PlacedLine placedLines[kMaxPlacedLines];
  uint16_t placedCount = 0;

  std::vector<WordBox> words;
  uint16_t rowCount = 0;
  int anchor = -1;
  int end = -1;
  bool saveOnRelease = false;
  unsigned long lastMoveTime = 0;

  bool popup = false;
  bool popupOk = false;
  unsigned long popupTime = 0;
};
