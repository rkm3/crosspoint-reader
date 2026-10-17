#include "HighlightSelectActivity.h"

#include <Epub/Section.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <NullReadwiseApi.h>
#include <ReadwiseHighlight.h>
#include <ReadwiseSyncEngine.h>
#include <SdReadwiseFileStore.h>

#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "HapticFeedback.h"
#include "HighlightWords.h"
#include "ReadwiseCredentialStore.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"

namespace {

constexpr unsigned long POPUP_MS = 900;
constexpr unsigned long REPEAT_START_MS = 500;
constexpr unsigned long REPEAT_INTERVAL_MS = 500;

}  // namespace

HighlightSelectActivity::HighlightSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 Section* section, const int pageIndex, const int marginLeft,
                                                 const int marginTop, const char* documentId)
    : Activity("HighlightSelect", renderer, mappedInput),
      section(section),
      pageIndex(pageIndex),
      marginLeft(marginLeft),
      marginTop(marginTop) {
  readwise::copyBounded(this->documentId, sizeof(this->documentId), documentId,
                        documentId != nullptr ? strlen(documentId) : 0);
}

void HighlightSelectActivity::onEnter() {
  Activity::onEnter();
  // Back and Home leave this cancelled. A saved quote replaces it before finish().
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  setResult(std::move(cancelled));
  fontId = SETTINGS.getReaderFontId();
  lineHeight = renderer.getLineHeight(fontId);
  if (section == nullptr || !rebuildWindow()) {
    LOG_ERR("HL", "Could not open highlight view");
    finish();
    return;
  }
  requestUpdate();
}

bool HighlightSelectActivity::holdPage(const int index) {
  if (section == nullptr || index < 0 || index >= static_cast<int>(section->pageCount)) {
    return false;
  }
  if (heldPage(index) != nullptr) {
    return true;
  }
  if (heldCount >= kMaxHeldPages) {
    return false;
  }
  auto loaded = section->loadPage(index);
  if (!loaded) {
    LOG_ERR("HL", "Could not load page %d for highlight view", index);
    return false;
  }
  held[heldCount].index = index;
  held[heldCount].page = std::move(loaded);
  ++heldCount;
  return true;
}

Page* HighlightSelectActivity::heldPage(const int index) const {
  for (int i = 0; i < heldCount; ++i) {
    if (held[i].index == index) {
      return held[i].page.get();
    }
  }
  return nullptr;
}

void HighlightSelectActivity::releasePagesOutside(const int firstPage, const int lastPage) {
  int write = 0;
  for (int i = 0; i < heldCount; ++i) {
    if (held[i].index < firstPage || held[i].index > lastPage) {
      continue;
    }
    if (write != i) {
      held[write] = std::move(held[i]);
    }
    ++write;
  }
  for (int i = write; i < heldCount; ++i) {
    held[i].page.reset();
    held[i].index = -1;
  }
  heldCount = write;
}

uint16_t HighlightSelectActivity::countTextLines(const Page& page) const {
  uint16_t count = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) {
      continue;
    }
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto* block = line->getBlock();
    if (block == nullptr || !block->valid()) {
      continue;
    }
    if (count == UINT16_MAX) {
      break;
    }
    ++count;
  }
  return count;
}

bool HighlightSelectActivity::rebuildWindow() {
  // loadPage reads a page; it does not change section->currentPage.
  if (section == nullptr || !holdPage(pageIndex)) {
    return false;
  }
  Page* origin = heldPage(pageIndex);
  if (origin == nullptr) {
    return false;
  }
  const uint16_t originLines = countTextLines(*origin);
  viewportLines = originLines > kMaxPlacedLines ? static_cast<uint16_t>(kMaxPlacedLines) : originLines;

  int firstPage = pageIndex;
  int lastPage = pageIndex;
  if (shift < 0) {
    int got = 0;
    const int need = -shift;
    for (int page = pageIndex - 1; page >= 0 && got < need; --page) {
      if (heldPage(page) == nullptr && heldCount >= kMaxHeldPages) {
        break;
      }
      if (!holdPage(page)) {
        break;
      }
      got += countTextLines(*heldPage(page));
      firstPage = page;
    }
  } else if (shift > 0) {
    int got = 0;
    const int need = shift;
    for (int page = pageIndex + 1; page < static_cast<int>(section->pageCount) && got < need; ++page) {
      if (heldPage(page) == nullptr && heldCount >= kMaxHeldPages) {
        break;
      }
      if (!holdPage(page)) {
        break;
      }
      got += countTextLines(*heldPage(page));
      lastPage = page;
    }
  }
  releasePagesOutside(firstPage, lastPage);

  int order[kMaxHeldPages];
  int orderCount = 0;
  for (int i = 0; i < heldCount; ++i) {
    order[orderCount++] = held[i].index;
  }
  for (int i = 1; i < orderCount; ++i) {
    const int value = order[i];
    int j = i;
    while (j > 0 && order[j - 1] > value) {
      order[j] = order[j - 1];
      --j;
    }
    order[j] = value;
  }

  struct Span {
    int index = -1;
    uint16_t start = 0;
  };
  Span spans[kMaxHeldPages];
  int spanCount = 0;
  uint16_t cursor = 0;
  anchorLine = 0;
  for (int i = 0; i < orderCount; ++i) {
    Page* page = heldPage(order[i]);
    if (page == nullptr) {
      continue;
    }
    if (order[i] == pageIndex) {
      anchorLine = cursor;
    }
    const uint16_t lines = countTextLines(*page);
    spans[spanCount].index = order[i];
    spans[spanCount].start = cursor;
    ++spanCount;
    if (static_cast<uint32_t>(cursor) + lines > UINT16_MAX) {
      break;
    }
    cursor = static_cast<uint16_t>(cursor + lines);
  }

  shift = readwise::clampHighlightShift(shift, anchorLine, cursor, viewportLines, viewportLines);
  uint16_t winFirst = 0;
  uint16_t winLast = 0;
  readwise::highlightWindow(anchorLine, cursor, viewportLines, shift, winFirst, winLast);

  placedCount = 0;
  int topY = 0;
  for (int s = 0; s < spanCount; ++s) {
    Page* page = heldPage(spans[s].index);
    if (page == nullptr) {
      continue;
    }
    uint16_t local = 0;
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) {
        continue;
      }
      const auto* line = static_cast<const PageLine*>(element.get());
      const auto* block = line->getBlock();
      if (block == nullptr || !block->valid()) {
        continue;
      }
      const uint16_t sourceIndex = static_cast<uint16_t>(spans[s].start + local);
      ++local;
      if (sourceIndex == anchorLine) {
        topY = line->yPos;
      }
      if (sourceIndex < winFirst || sourceIndex >= winLast || placedCount >= kMaxPlacedLines) {
        continue;
      }
      placedLines[placedCount].line = line;
      placedLines[placedCount].y = line->yPos;
      placedLines[placedCount].pageIndex = spans[s].index;
      ++placedCount;
    }
  }

  if (shift != 0 && placedCount > 0) {
    int placeY = topY;
    for (uint16_t i = 0; i < placedCount; ++i) {
      int gap = lineHeight > 0 ? lineHeight : 16;
      if (i + 1 < placedCount && placedLines[i + 1].pageIndex == placedLines[i].pageIndex &&
          placedLines[i + 1].y > placedLines[i].y) {
        gap = placedLines[i + 1].y - placedLines[i].y;
      }
      placedLines[i].y = static_cast<int16_t>(placeY);
      placeY += gap;
    }
  }

  extractWords();
  return true;
}

void HighlightSelectActivity::nudge(const int direction) {
  if (direction == 0 || anchor >= 0 || viewportLines == 0) {
    return;
  }
  const int room = static_cast<int>(viewportLines);
  int next = shift + direction * readwise::HIGHLIGHT_NUDGE_LINES;
  if (next > room) {
    next = room;
  }
  if (next < -room) {
    next = -room;
  }
  if (next == shift) {
    return;
  }
  shift = next;
  if (!rebuildWindow()) {
    return;
  }
  anchor = -1;
  end = -1;
  requestUpdate();
}

void HighlightSelectActivity::drawPlacedLines() const {
  if (shift == 0) {
    Page* origin = heldPage(pageIndex);
    if (origin != nullptr) {
      origin->render(renderer, fontId, marginLeft, marginTop);
    }
    return;
  }
  for (uint16_t i = 0; i < placedCount; ++i) {
    const PageLine* line = placedLines[i].line;
    if (line == nullptr) {
      continue;
    }
    const int yOffset = placedLines[i].y + marginTop - line->yPos;
    line->render(renderer, fontId, marginLeft, yOffset);
  }
}

void HighlightSelectActivity::extractWords() {
  words.clear();
  words.reserve(128);
  rowCount = 0;
  std::string pageText;
  pageText.reserve(2048);
  uint8_t styleMask = 0;

  const int ascender = renderer.getFontAscenderSize(fontId);
  for (uint16_t li = 0; li < placedCount; ++li) {
    const PageLine* line = placedLines[li].line;
    if (line == nullptr) {
      continue;
    }
    const auto* block = line->getBlock();
    if (block == nullptr || !block->valid()) {
      continue;
    }

    bool rowHasWords = false;
    const int rubyShift = block->getRubyShift(ascender);
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      const char* text = block->wordText(i);
      if (!isSelectableToken(text)) continue;
      WordBox box;
      box.x = static_cast<int16_t>(line->xPos + block->wordXpos(i) + marginLeft);
      box.y = static_cast<int16_t>(placedLines[li].y + marginTop + rubyShift);
      box.style = block->wordStyle(i);
      box.width = 0;
      box.row = rowCount;
      box.text = text;
      words.push_back(box);
      rowHasWords = true;
      pageText.append(text);
      pageText.push_back(' ');
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(box.style) & 0x03));
    }
    if (rowHasWords) rowCount++;
  }

  if (styleMask == 0) styleMask = 0x01;
  renderer.ensureSdCardFontReady(fontId, pageText.c_str(), styleMask);
  for (auto& word : words) {
    word.width = static_cast<int16_t>(renderer.getTextAdvanceX(fontId, word.text, word.style));
  }
}

int HighlightSelectActivity::wordAt(const int x, const int y) const {
  constexpr int kSlop = 4;
  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    const WordBox& word = words[i];
    if (x >= word.x - kSlop && x < word.x + word.width + kSlop && y >= word.y - kSlop &&
        y < word.y + lineHeight + kSlop) {
      return i;
    }
  }
  return -1;
}

int HighlightSelectActivity::closestInRow(const uint16_t row, const int centerX) const {
  int best = -1;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    if (words[i].row != row) continue;
    const int distance = std::abs(words[i].x + words[i].width / 2 - centerX);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

bool HighlightSelectActivity::inRange(const int index) const {
  if (anchor < 0 || end < 0) return false;
  const int lo = anchor < end ? anchor : end;
  const int hi = anchor < end ? end : anchor;
  return index >= lo && index <= hi;
}

void HighlightSelectActivity::moveEnd(const int delta) {
  if (words.empty() || delta == 0) return;
  if (anchor < 0) {
    anchor = static_cast<int>(words.size()) / 2;
    end = anchor;
  }
  const int next = end + delta;
  if (next < 0 || next >= static_cast<int>(words.size())) return;
  end = next;
  requestUpdate();
}

void HighlightSelectActivity::moveVertical(const int direction) {
  if (words.empty()) return;
  if (anchor < 0) {
    anchor = static_cast<int>(words.size()) / 2;
    end = anchor;
  }
  const WordBox& current = words[end];
  const int targetRow = static_cast<int>(current.row) + direction;
  if (targetRow < 0 || targetRow >= static_cast<int>(rowCount)) return;
  const int best = closestInRow(static_cast<uint16_t>(targetRow), current.x + current.width / 2);
  if (best >= 0 && best != end) {
    end = best;
    requestUpdate();
  }
}

void HighlightSelectActivity::commit() {
  if (anchor < 0 || end < 0) return;
  const int lo = anchor < end ? anchor : end;
  const int hi = anchor < end ? end : anchor;
  auto quote = makeUniqueNoThrow<char[]>(readwise::HIGHLIGHT_TEXT_MAX + 1);
  if (!quote) {
    LOG_ERR("HL", "OOM: quote");
    popup = true;
    popupOk = false;
    popupTime = millis();
    requestUpdate();
    return;
  }
  const uint16_t count = static_cast<uint16_t>(hi - lo + 1);
  auto ptrs = makeUniqueNoThrow<const char*[]>(count);
  if (!ptrs) {
    LOG_ERR("HL", "OOM: quote words");
    popup = true;
    popupOk = false;
    popupTime = millis();
    requestUpdate();
    return;
  }
  for (uint16_t i = 0; i < count; ++i) {
    ptrs[i] = words[lo + i].text;
  }
  if (!readwise::quoteFromHighlightWords(ptrs.get(), count, quote.get(), readwise::HIGHLIGHT_TEXT_MAX + 1)) {
    popup = true;
    popupOk = false;
    popupTime = millis();
    requestUpdate();
    return;
  }

  readwise::NullReadwiseApi nullApi;
  readwise::SdReadwiseFileStore store;
  auto engine = makeUniqueNoThrow<readwise::ReadwiseSyncEngine>(nullApi, store, ReadwiseCredentialStore::getDataDir());
  if (engine && engine->appendHighlight(documentId, quote.get())) {
    ActivityResult saved;
    setResult(std::move(saved));
    finish();
    return;
  }
  LOG_ERR("HL", "Could not queue highlight for %s", documentId);
  popup = true;
  popupOk = false;
  popupTime = millis();
  requestUpdate();
}

bool HighlightSelectActivity::handleHomeGesture() {
  if (mappedInput.wasHomeKeyLongPressed()) return false;
  if (!popup) finish();
  return true;
}

void HighlightSelectActivity::loop() {
  if (mappedInput.wasHomeKeyLongPressed()) {
    onGoHome();
    return;
  }
  if (popup) {
    if (millis() - popupTime >= POPUP_MS) finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    commit();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTouchDown(x, y)) {
    const int hit = wordAt(x, y);
    saveOnRelease = hit >= 0 && inRange(hit);
    if (hit >= 0 && !saveOnRelease) {
      anchor = hit;
      end = hit;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.isScreenTouchHeld(x, y) && anchor >= 0) {
    const int hit = wordAt(x, y);
    if (hit >= 0 && hit != end) {
      end = hit;
      saveOnRelease = false;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTouchReleased() && saveOnRelease) {
    saveOnRelease = false;
    haptic_feedback::touchAction();
    commit();
    return;
  }

  const unsigned long now = millis();
  const bool repeat = mappedInput.getHeldTime() >= REPEAT_START_MS && now - lastMoveTime >= REPEAT_INTERVAL_MS;
  if (anchor < 0) {
    const bool back = mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                      (repeat && mappedInput.isPressed(MappedInputManager::Button::PageBack));
    const bool forward = mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
                         (repeat && mappedInput.isPressed(MappedInputManager::Button::PageForward));
    if (back || forward) {
      lastMoveTime = now;
      nudge(back ? -1 : 1);
      return;
    }
  }

  if (words.empty()) return;
  const bool left = mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft) ||
                    (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenLeft));
  const bool right = mappedInput.wasPressed(MappedInputManager::Button::ScreenRight) ||
                     (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenRight));
  if (left || right) {
    lastMoveTime = now;
    moveEnd(left ? -1 : 1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenUp)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenDown)) {
    moveVertical(1);
  }
}

void HighlightSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();
  auto* cache = renderer.getFontCacheManager();
  auto scope = cache->createPrewarmScope();
  drawPlacedLines();
  scope.endScanAndPrewarm();
  drawPlacedLines();

  if (anchor >= 0 && end >= 0) {
    const int lo = anchor < end ? anchor : end;
    const int hi = anchor < end ? end : anchor;
    for (int i = lo; i <= hi; ++i) {
      const WordBox& word = words[i];
      renderer.fillRect(word.x - 2, word.y - 2, word.width + 4, lineHeight + 4, true);
      cache->prewarmCache(fontId, word.text, static_cast<uint8_t>(1u << (static_cast<uint8_t>(word.style) & 0x03)));
      renderer.drawText(fontId, word.x, word.y, word.text, false, word.style);
    }
  }

  if (!mappedInput.hasTouch()) {
    const auto labels = mappedInput.mapDirectionalLabels(tr(STR_BACK), tr(STR_READWISE_HIGHLIGHT), tr(STR_DIR_LEFT),
                                                         tr(STR_DIR_RIGHT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  if (popup) {
    GUI.drawPopup(renderer, popupOk ? tr(STR_READWISE_CLIP_QUEUED) : tr(STR_READWISE_CLIP_FAILED));
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
