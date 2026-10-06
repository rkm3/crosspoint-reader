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
  if (!appendRaw(out, outCap, pos, "\"source_type\":\"", 15) ||
      !appendRaw(out, outCap, pos, HIGHLIGHT_SOURCE_TYPE, strlen(HIGHLIGHT_SOURCE_TYPE)) ||
      !appendRaw(out, outCap, pos, "\",\"category\":\"articles\"}]}", 26)) {
    return false;
  }
  if (pos >= outCap) {
    return false;
  }
  out[pos] = '\0';
  return true;
}

}  // namespace readwise
