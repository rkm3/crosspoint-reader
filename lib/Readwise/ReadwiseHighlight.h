#pragma once

#include <cstddef>
#include <cstdint>

#include "ReadwiseDocument.h"

// A clip posted to the Readwise highlight library (POST /api/v2/highlights/),
// which is what the Obsidian export syncs. It is not a Reader document highlight:
// the same title and source URL still land in their own book.
//
// The quote lives in bodies/<id>/highlights.bin. The journal cannot hold it
// (one-byte payload). highlights.idx lists the documents that still have an
// unposted clip, because the file store cannot list a directory.

namespace readwise {

inline constexpr const char* HIGHLIGHTS_URL = "https://readwise.io/api/v2/highlights/";
inline constexpr const char* HIGHLIGHT_SOURCE_TYPE = "crosspoint";

// 280 bytes of quote, plus the NUL the in-memory buffer keeps.
inline constexpr size_t HIGHLIGHT_TEXT_MAX = 280;
inline constexpr int HIGHLIGHT_MAX_PER_DOC = 32;
inline constexpr int HIGHLIGHT_INDEX_MAX = 160;

inline constexpr uint8_t HIGHLIGHT_FILE_VERSION = 1;
inline constexpr uint8_t HIGHLIGHT_INDEX_VERSION = 1;
inline constexpr uint8_t HIGHLIGHT_FLAG_POSTED = 1;

// u8 version | title[TITLE_CAP] | author[AUTHOR_CAP] | sourceUrl[SOURCE_URL_CAP]
inline constexpr size_t HIGHLIGHT_HEADER_BYTES = 1 + TITLE_CAP + AUTHOR_CAP + SOURCE_URL_CAP;

// Cuts `text` down to HIGHLIGHT_TEXT_MAX bytes on a UTF-8 boundary, and on a
// word boundary when the cut would drop a partial word. `cap` includes the NUL.
// Returns false when the result is empty.
bool fitHighlightText(char* text, size_t cap);

// {"highlights":[{text, title, author, source_url, source_type, category}]}.
// Empty title, author, and source URL are omitted. Returns false when `out`
// cannot hold the escaped body.
bool buildHighlightBody(const char* text, const char* title, const char* author, const char* sourceUrl, char* out,
                        size_t outCap);

}  // namespace readwise
