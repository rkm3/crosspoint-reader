#pragma once

#include <cstddef>
#include <cstdint>

#include "ReadwiseDocument.h"

// A clip posted to the Readwise highlight library (POST /api/v2/highlights/),
// which is what the Obsidian export syncs. It is not a Reader document highlight:
// the same title and source URL still land in their own book.
//
// The quote lives in highlights/<id>.bin, outside the article directory, so
// archiving or deleting the article cannot take an unsent clip with it. Files
// written by older builds at bodies/<id>/highlights.bin are copied across on
// the next read. The journal cannot hold a quote (one-byte payload).
// highlights.idx lists documents that still have an unposted clip, because
// the file store cannot list a directory.

namespace readwise {

inline constexpr const char* HIGHLIGHTS_URL = "https://readwise.io/api/v2/highlights/";
inline constexpr const char* HIGHLIGHT_SOURCE_TYPE = "crosspoint";

// Board name posted as an inline tag in the highlight note (".x4pro" → #x4pro).
// Mirrors FirmwareBoardTag board names; host tests without a device flag fall
// back to "crosspoint".
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "x4pro";
#elif defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "x4c";
#elif defined(FREEINK_DEVICE_METALIO_EINK4) && FREEINK_DEVICE_METALIO_EINK4
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "metalio_eink4";
#elif (defined(FREEINK_DEVICE_X4) && FREEINK_DEVICE_X4) || (defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3)
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "x4";
#elif defined(FREEINK_DEVICE_PAPERMONO) && FREEINK_DEVICE_PAPERMONO
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "papermono";
#elif defined(FREEINK_DEVICE_STICKY) && FREEINK_DEVICE_STICKY
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "sticky";
#elif defined(FREEINK_DEVICE_M5PAPER) && FREEINK_DEVICE_M5PAPER
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "m5paper";
#elif defined(FREEINK_DEVICE_LILYGO) && FREEINK_DEVICE_LILYGO
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "lilygo";
#elif defined(FREEINK_DEVICE_M5) && FREEINK_DEVICE_M5
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "m5";
#elif defined(FREEINK_DEVICE_MURPHY) && FREEINK_DEVICE_MURPHY
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "murphy";
#elif defined(FREEINK_DEVICE_DELINK) && FREEINK_DEVICE_DELINK
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "delink";
#else
inline constexpr const char* HIGHLIGHT_INLINE_TAG = "crosspoint";
#endif

// Quote bytes, plus the NUL the in-memory buffer keeps. Version 1 files stored
// at most 280. Version 2 stores the same records with this larger cap.
// Readwise's highlight API accepts 1024.
inline constexpr size_t HIGHLIGHT_TEXT_MAX_V1 = 280;
inline constexpr size_t HIGHLIGHT_TEXT_MAX = 1024;
inline constexpr int HIGHLIGHT_MAX_PER_DOC = 32;
inline constexpr int HIGHLIGHT_INDEX_MAX = 160;

inline constexpr uint8_t HIGHLIGHT_FILE_VERSION_V1 = 1;
inline constexpr uint8_t HIGHLIGHT_FILE_VERSION = 2;
inline constexpr uint8_t HIGHLIGHT_INDEX_VERSION = 1;

inline bool isHighlightFileVersion(const uint8_t version) {
  return version == HIGHLIGHT_FILE_VERSION_V1 || version == HIGHLIGHT_FILE_VERSION;
}

// Longest quote a file of `version` may contain. A version 1 record longer
// than the old cap is truncated data, not a quote this build wrote.
inline size_t highlightTextLimit(const uint8_t version) {
  return version == HIGHLIGHT_FILE_VERSION_V1 ? HIGHLIGHT_TEXT_MAX_V1 : HIGHLIGHT_TEXT_MAX;
}
// bit 0: the server accepted the quote. bit 1: the server rejected it (4xx).
// A rejected quote is not retried. The flag byte is the same in version 1 and 2.
inline constexpr uint8_t HIGHLIGHT_FLAG_POSTED = 1;
inline constexpr uint8_t HIGHLIGHT_FLAG_FAILED = 2;

// u8 version | title[TITLE_CAP] | author[AUTHOR_CAP] | sourceUrl[SOURCE_URL_CAP]
inline constexpr size_t HIGHLIGHT_HEADER_BYTES = 1 + TITLE_CAP + AUTHOR_CAP + SOURCE_URL_CAP;
// Header plus every record at the text cap. A larger file is left where it is.
inline constexpr size_t HIGHLIGHT_FILE_MAX_BYTES =
    HIGHLIGHT_HEADER_BYTES + static_cast<size_t>(HIGHLIGHT_MAX_PER_DOC) * (3 + HIGHLIGHT_TEXT_MAX);

// Cuts `text` down to HIGHLIGHT_TEXT_MAX bytes on a UTF-8 boundary, and on a
// word boundary when the cut would drop a partial word. `cap` includes the NUL.
// Returns false when the result is empty.
bool fitHighlightText(char* text, size_t cap);

// One selectable word in reading order. `text` is not necessarily terminated
// at `length`; only `length` bytes are compared.
struct HighlightWordView {
  const char* text = nullptr;
  uint16_t length = 0;
};

enum class HighlightMark : uint8_t { None = 0, Pending = 1, Posted = 2 };

// Pending and rejected quotes are the dark style (the reader rarely revisits
// an article after sync). Posted quotes are the light style.
//   dark:  3px solid underline, dark gray shade
//   light: 1px solid underline, light gray shade
// The shade is painted on the grayscale pass. A path without a gray plane
// dithers the same two weights instead.
inline constexpr int HIGHLIGHT_DARK_UNDERLINE_PX = 3;
inline constexpr int HIGHLIGHT_LIGHT_UNDERLINE_PX = 1;

// In highlight mode, before a selection starts, each side-button press moves
// the visible lines by this many. Presses accumulate, up to one page.
inline constexpr int HIGHLIGHT_NUDGE_LINES = 3;

// A cross-page fragment shorter than this is not marked. A full quote is
// marked at any length.
inline constexpr uint16_t HIGHLIGHT_PARTIAL_MIN_WORDS = 3;

// One selectable word plus where it sits, for joining marks into runs.
// `line` changes when the word is on a different text line. `mark` is a
// HighlightMark value.
struct HighlightWordMark {
  int16_t x = 0;
  int16_t top = 0;
  int16_t width = 0;
  int16_t height = 0;
  int16_t underlineY = 0;
  uint16_t line = 0;
  uint8_t mark = 0;
};

// A continuous mark. Adjacent words on the same line with the same mark become
// one run whose box spans the gaps between them.
struct HighlightRun {
  int16_t x = 0;
  int16_t top = 0;
  int16_t width = 0;
  int16_t height = 0;
  int16_t underlineY = 0;
  uint8_t mark = 0;
};

// Merges reading-order words into runs. A None mark or a line change ends the
// current run. Returns how many runs were written, stopping at `outCap`.
uint8_t mergeHighlightRuns(const HighlightWordMark* words, uint16_t wordCount, HighlightRun* out, uint8_t outCap);

// Clamps a line offset so the window stays inside [0, lineCount) and moves at
// most `maxShift` lines from the anchor page. `anchorLine` is the first line
// of the page the reader saved. Positive shift reveals later lines.
int clampHighlightShift(int shift, uint16_t anchorLine, uint16_t lineCount, uint16_t viewportLines, uint16_t maxShift);

// Half-open line range [first, last) covered by `shift`. The window is
// `viewportLines` long and is clamped the same way as clampHighlightShift
// with maxShift = viewportLines.
void highlightWindow(uint16_t anchorLine, uint16_t lineCount, uint16_t viewportLines, int shift, uint16_t& firstLine,
                     uint16_t& lastLine);

// Indexes of `wordLines` that fall inside the window. `wordLines[i]` is the
// source line of word i. Returns how many indexes were written.
uint16_t nudgedViewIndices(const uint16_t* wordLines, uint16_t wordCount, uint16_t anchorLine, uint16_t viewportLines,
                           int shift, uint16_t* out, uint16_t outCap);

// Joins `words` with single spaces and trims to the quote cap on a word
// boundary. `cap` includes the NUL. Returns false when the result is empty.
bool quoteFromHighlightWords(const char* const* words, uint16_t count, char* out, size_t cap);

// Marks `words` where a stored quote occurs. Quotes are space-separated
// selectable tokens, the same sequence HighlightSelect stores. Every
// non-overlapping full occurrence is marked. A quote with no full match is
// marked only when its prefix is the page's trailing words, or its suffix is
// the page's leading words, and that fragment is at least
// HIGHLIGHT_PARTIAL_MIN_WORDS. Posted wins over pending on the same word.
// `marks` is `wordCount` entries and is cleared first. `quoteFlags` bit 0
// means the server accepted that quote. A rejected quote (bit 1, not bit 0)
// is pending: the page draws it with the dark style.
void markHighlightWords(const HighlightWordView* words, uint16_t wordCount, const char* const* quotes,
                        const uint8_t* quoteFlags, uint16_t quoteCount, uint8_t* marks);

// Same matching as one pass of markHighlightWords, without clearing `marks`.
// Page rendering calls this once per stored quote so it does not hold every
// quote at once.
void applyHighlightQuote(const HighlightWordView* words, uint16_t wordCount, const char* quote, uint8_t flags,
                         uint8_t* marks);

// {"highlights":[{text, title, author, source_url, source_type, category, note}]}.
// `note` is the inline tag (".x4pro") so Readwise attaches #x4pro (or the
// board's tag). Empty title, author, and source URL are omitted. Returns false
// when `out` cannot hold the escaped body.
bool buildHighlightBody(const char* text, const char* title, const char* author, const char* sourceUrl, char* out,
                        size_t outCap);

}  // namespace readwise
