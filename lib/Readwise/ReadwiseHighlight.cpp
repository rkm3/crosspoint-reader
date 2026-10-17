#include "ReadwiseHighlight.h"

#include <cstdio>
#include <cstring>

namespace readwise {
namespace {

bool appendRaw(char* out, size_t cap, size_t& pos, const char* bytes, size_t len) {
  if (pos + len >= cap) {
    return false;
  }
  memcpy(out + pos, bytes, len);
  pos += len;
  return true;
}

bool appendJsonString(char* out, size_t cap, size_t& pos, const char* value) {
  if (!appendRaw(out, cap, pos, "\"", 1)) {
    return false;
  }
  for (const auto* p = reinterpret_cast<const uint8_t*>(value); *p != 0; ++p) {
    if (*p == '"' || *p == '\\') {
      const char escaped[2] = {'\\', static_cast<char>(*p)};
      if (!appendRaw(out, cap, pos, escaped, 2)) {
        return false;
      }
    } else if (*p == '\n') {
      if (!appendRaw(out, cap, pos, "\\n", 2)) {
        return false;
      }
    } else if (*p == '\r') {
      if (!appendRaw(out, cap, pos, "\\r", 2)) {
        return false;
      }
    } else if (*p == '\t') {
      if (!appendRaw(out, cap, pos, "\\t", 2)) {
        return false;
      }
    } else if (*p < 0x20) {
      char hex[7];
      snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned>(*p));
      if (!appendRaw(out, cap, pos, hex, 6)) {
        return false;
      }
    } else if (!appendRaw(out, cap, pos, reinterpret_cast<const char*>(p), 1)) {
      return false;
    }
  }
  return appendRaw(out, cap, pos, "\"", 1);
}

bool appendField(char* out, size_t cap, size_t& pos, const char* key, const char* value, bool& first) {
  if (value == nullptr || value[0] == '\0') {
    return true;
  }
  if (!first && !appendRaw(out, cap, pos, ",", 1)) {
    return false;
  }
  first = false;
  return appendRaw(out, cap, pos, key, strlen(key)) && appendJsonString(out, cap, pos, value);
}

}  // namespace

bool fitHighlightText(char* text, size_t cap) {
  if (text == nullptr || cap < 2) {
    return false;
  }
  const size_t limit = cap - 1 < HIGHLIGHT_TEXT_MAX ? cap - 1 : HIGHLIGHT_TEXT_MAX;
  size_t len = 0;
  while (text[len] != '\0') {
    ++len;
  }
  if (len > limit) {
    size_t cut = limit;
    while (cut > 0 && (static_cast<uint8_t>(text[cut]) & 0xC0) == 0x80) {
      --cut;
    }
    size_t word = cut;
    while (word > 0 && text[word - 1] != ' ') {
      --word;
    }
    if (word > 0) {
      cut = word - 1;
    }
    text[cut] = '\0';
  }
  return text[0] != '\0';
}

bool buildHighlightBody(const char* text, const char* title, const char* author, const char* sourceUrl, char* out,
                        size_t outCap) {
  if (out == nullptr || outCap == 0 || text == nullptr || text[0] == '\0') {
    return false;
  }
  size_t pos = 0;
  if (!appendRaw(out, outCap, pos, "{\"highlights\":[{", 16)) {
    return false;
  }
  bool first = true;
  if (!appendField(out, outCap, pos, "\"text\":", text, first) ||
      !appendField(out, outCap, pos, "\"title\":", title, first) ||
      !appendField(out, outCap, pos, "\"author\":", author, first) ||
      !appendField(out, outCap, pos, "\"source_url\":", sourceUrl, first)) {
    return false;
  }
  if (!first && !appendRaw(out, outCap, pos, ",", 1)) {
    return false;
  }
  // note holds an inline tag (".x4pro"); Readwise turns it into #x4pro.
  if (!appendRaw(out, outCap, pos, "\"source_type\":\"", 15) ||
      !appendRaw(out, outCap, pos, HIGHLIGHT_SOURCE_TYPE, strlen(HIGHLIGHT_SOURCE_TYPE)) ||
      !appendRaw(out, outCap, pos, "\",\"category\":\"articles\",\"note\":\".", 33) ||
      !appendRaw(out, outCap, pos, HIGHLIGHT_INLINE_TAG, strlen(HIGHLIGHT_INLINE_TAG)) ||
      !appendRaw(out, outCap, pos, "\"}]}", 4)) {
    return false;
  }
  if (pos >= outCap) {
    return false;
  }
  out[pos] = '\0';
  return true;
}

namespace {

bool nextToken(const char*& p, const char*& token, uint16_t& length) {
  while (*p == ' ') {
    ++p;
  }
  if (*p == '\0') {
    return false;
  }
  token = p;
  while (*p != '\0' && *p != ' ') {
    ++p;
  }
  length = static_cast<uint16_t>(p - token);
  return length > 0;
}

uint16_t countTokens(const char* quote) {
  uint16_t count = 0;
  const char* token = nullptr;
  uint16_t length = 0;
  const char* p = quote != nullptr ? quote : "";
  while (nextToken(p, token, length)) {
    ++count;
  }
  return count;
}

bool sameWord(const HighlightWordView& word, const char* token, uint16_t length) {
  return word.text != nullptr && word.length == length && memcmp(word.text, token, length) == 0;
}

// True when `count` words at `start` equal quote tokens [first, first + count).
bool tokensEqual(const HighlightWordView* words, uint16_t start, const char* quote, uint16_t first, uint16_t count) {
  const char* p = quote != nullptr ? quote : "";
  uint16_t index = 0;
  const char* token = nullptr;
  uint16_t length = 0;
  while (nextToken(p, token, length)) {
    if (index >= first) {
      const uint16_t offset = static_cast<uint16_t>(index - first);
      if (offset >= count || !sameWord(words[start + offset], token, length)) {
        return false;
      }
    }
    ++index;
    if (index >= first + count) {
      return true;
    }
  }
  return false;
}

uint16_t matchLengthAt(const HighlightWordView* words, uint16_t wordCount, uint16_t start, const char* quote) {
  const char* p = quote != nullptr ? quote : "";
  uint16_t matched = 0;
  const char* token = nullptr;
  uint16_t length = 0;
  while (nextToken(p, token, length)) {
    if (start + matched >= wordCount || !sameWord(words[start + matched], token, length)) {
      return 0;
    }
    ++matched;
  }
  return matched;
}

void paint(uint8_t* marks, uint16_t start, uint16_t count, bool posted) {
  const uint8_t style = static_cast<uint8_t>(posted ? HighlightMark::Posted : HighlightMark::Pending);
  for (uint16_t i = 0; i < count; ++i) {
    if (marks[start + i] == static_cast<uint8_t>(HighlightMark::Posted)) {
      continue;
    }
    if (posted || marks[start + i] == static_cast<uint8_t>(HighlightMark::None)) {
      marks[start + i] = style;
    }
  }
}

}  // namespace

void applyOneQuote(const HighlightWordView* words, const uint16_t wordCount, const char* quote, const bool posted,
                   uint8_t* marks) {
  const uint16_t tokens = countTokens(quote);
  if (tokens == 0) {
    return;
  }
  bool full = false;
  for (uint16_t i = 0; i < wordCount;) {
    const uint16_t matched = matchLengthAt(words, wordCount, i, quote);
    if (matched > 0) {
      paint(marks, i, matched, posted);
      full = true;
      i = static_cast<uint16_t>(i + matched);
    } else {
      ++i;
    }
  }
  if (full || tokens < HIGHLIGHT_PARTIAL_MIN_WORDS || wordCount < HIGHLIGHT_PARTIAL_MIN_WORDS) {
    return;
  }
  const uint16_t limit = wordCount < tokens ? wordCount : static_cast<uint16_t>(tokens - 1);
  for (uint16_t k = limit; k >= HIGHLIGHT_PARTIAL_MIN_WORDS; --k) {
    if (tokensEqual(words, static_cast<uint16_t>(wordCount - k), quote, 0, k)) {
      paint(marks, static_cast<uint16_t>(wordCount - k), k, posted);
      break;
    }
  }
  for (uint16_t k = limit; k >= HIGHLIGHT_PARTIAL_MIN_WORDS; --k) {
    if (tokensEqual(words, 0, quote, static_cast<uint16_t>(tokens - k), k)) {
      paint(marks, 0, k, posted);
      break;
    }
  }
}

void applyHighlightQuote(const HighlightWordView* words, const uint16_t wordCount, const char* quote,
                         const uint8_t flags, uint8_t* marks) {
  if (words == nullptr || marks == nullptr || quote == nullptr || quote[0] == '\0' || wordCount == 0) {
    return;
  }
  applyOneQuote(words, wordCount, quote, (flags & HIGHLIGHT_FLAG_POSTED) != 0, marks);
}

void markHighlightWords(const HighlightWordView* words, const uint16_t wordCount, const char* const* quotes,
                        const uint8_t* quoteFlags, const uint16_t quoteCount, uint8_t* marks) {
  if (marks == nullptr) {
    return;
  }
  if (wordCount > 0) {
    memset(marks, 0, wordCount);
  }
  if (words == nullptr || quotes == nullptr || quoteFlags == nullptr || wordCount == 0) {
    return;
  }
  for (uint16_t q = 0; q < quoteCount; ++q) {
    applyHighlightQuote(words, wordCount, quotes[q], quoteFlags[q], marks);
  }
}

uint8_t mergeHighlightRuns(const HighlightWordMark* words, const uint16_t wordCount, HighlightRun* out,
                           const uint8_t outCap) {
  if (words == nullptr || out == nullptr || outCap == 0) {
    return 0;
  }
  uint8_t written = 0;
  bool open = false;
  uint16_t openLine = 0;
  HighlightRun run;
  const auto flush = [&]() {
    if (!open || run.width <= 0 || written >= outCap) {
      open = false;
      return;
    }
    out[written++] = run;
    open = false;
  };
  for (uint16_t i = 0; i < wordCount; ++i) {
    const HighlightWordMark& word = words[i];
    if (word.mark == static_cast<uint8_t>(HighlightMark::None) || word.width <= 0) {
      flush();
      continue;
    }
    if (open && word.mark == run.mark && word.line == openLine) {
      const int left = word.x < run.x ? word.x : run.x;
      const int right = word.x + word.width > run.x + run.width ? word.x + word.width : run.x + run.width;
      const int top = word.top < run.top ? word.top : run.top;
      const int bottom = word.top + word.height > run.top + run.height ? word.top + word.height : run.top + run.height;
      run.x = static_cast<int16_t>(left);
      run.width = static_cast<int16_t>(right - left);
      run.top = static_cast<int16_t>(top);
      run.height = static_cast<int16_t>(bottom - top);
      if (word.underlineY > run.underlineY) {
        run.underlineY = word.underlineY;
      }
      continue;
    }
    flush();
    if (written >= outCap) {
      return written;
    }
    run.x = word.x;
    run.top = word.top;
    run.width = word.width;
    run.height = word.height;
    run.underlineY = word.underlineY;
    run.mark = word.mark;
    openLine = word.line;
    open = true;
  }
  flush();
  return written;
}

int clampHighlightShift(const int shift, const uint16_t anchorLine, const uint16_t lineCount,
                        const uint16_t viewportLines, const uint16_t maxShift) {
  if (viewportLines == 0 || lineCount == 0 || anchorLine >= lineCount) {
    return 0;
  }
  const uint16_t backRoom = anchorLine < maxShift ? anchorLine : maxShift;
  const uint16_t originEnd = static_cast<uint16_t>(anchorLine + viewportLines);
  const uint16_t after = lineCount > originEnd ? static_cast<uint16_t>(lineCount - originEnd) : 0;
  const int forwardRoom = after < maxShift ? static_cast<int>(after) : static_cast<int>(maxShift);
  int applied = shift;
  if (applied < -static_cast<int>(backRoom)) {
    applied = -static_cast<int>(backRoom);
  }
  if (applied > forwardRoom) {
    applied = forwardRoom;
  }
  return applied;
}

void highlightWindow(const uint16_t anchorLine, const uint16_t lineCount, const uint16_t viewportLines, const int shift,
                     uint16_t& firstLine, uint16_t& lastLine) {
  firstLine = 0;
  lastLine = 0;
  if (viewportLines == 0 || lineCount == 0) {
    return;
  }
  const int applied = clampHighlightShift(shift, anchorLine, lineCount, viewportLines, viewportLines);
  int start = static_cast<int>(anchorLine) + applied;
  if (start < 0) {
    start = 0;
  }
  int end = start + static_cast<int>(viewportLines);
  if (end > static_cast<int>(lineCount)) {
    end = static_cast<int>(lineCount);
  }
  if (end < start) {
    end = start;
  }
  firstLine = static_cast<uint16_t>(start);
  lastLine = static_cast<uint16_t>(end);
}

uint16_t nudgedViewIndices(const uint16_t* wordLines, const uint16_t wordCount, const uint16_t anchorLine,
                           const uint16_t viewportLines, const int shift, uint16_t* out, const uint16_t outCap) {
  if (wordLines == nullptr || out == nullptr || outCap == 0 || wordCount == 0) {
    return 0;
  }
  uint16_t lineCount = 0;
  for (uint16_t i = 0; i < wordCount; ++i) {
    if (wordLines[i] >= lineCount) {
      lineCount = static_cast<uint16_t>(wordLines[i] + 1);
    }
  }
  uint16_t first = 0;
  uint16_t last = 0;
  highlightWindow(anchorLine, lineCount, viewportLines, shift, first, last);
  uint16_t written = 0;
  for (uint16_t i = 0; i < wordCount && written < outCap; ++i) {
    if (wordLines[i] >= first && wordLines[i] < last) {
      out[written++] = i;
    }
  }
  return written;
}

bool quoteFromHighlightWords(const char* const* words, const uint16_t count, char* out, const size_t cap) {
  if (out == nullptr || cap < 2) {
    return false;
  }
  out[0] = '\0';
  if (words == nullptr || count == 0) {
    return false;
  }
  size_t used = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const char* word = words[i];
    if (word == nullptr || word[0] == '\0') {
      continue;
    }
    const size_t len = strlen(word);
    if (used > 0) {
      if (used + 1 >= cap) {
        break;
      }
      out[used++] = ' ';
    }
    const size_t room = cap - 1 - used;
    const size_t copy = len < room ? len : room;
    memcpy(out + used, word, copy);
    used += copy;
    out[used] = '\0';
    if (used + 1 >= cap) {
      break;
    }
  }
  return fitHighlightText(out, cap);
}

}  // namespace readwise
