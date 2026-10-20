// Quotes are stored as space-separated selectable tokens, with no positions.
// markHighlightWords is the whole matching rule the article page draws from:
// every full occurrence, and an edge fragment only when the quote does not
// fit on the page.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "lib/Readwise/ReadwiseHighlight.h"

namespace {

using namespace readwise;

void words(HighlightWordView* out, const char* const* text, uint16_t count) {
  for (uint16_t i = 0; i < count; ++i) {
    out[i].text = text[i];
    out[i].length = static_cast<uint16_t>(strlen(text[i]));
  }
}

uint8_t markAt(const char* const* page, uint16_t pageCount, const char* quote, uint8_t flags, uint8_t* marks) {
  HighlightWordView views[16];
  words(views, page, pageCount);
  const char* quotes[] = {quote};
  const uint8_t flagRow[] = {flags};
  markHighlightWords(views, pageCount, quotes, flagRow, 1, marks);
  return marks[0];
}

}  // namespace

TEST(HighlightMarkTest, FullOccurrenceIsPendingUntilPosted) {
  const char* page[] = {"alpha", "beta", "gamma"};
  uint8_t marks[3] = {9, 9, 9};
  markAt(page, 3, "alpha beta", HIGHLIGHT_FLAG_FAILED, marks);
  EXPECT_EQ(marks[0], static_cast<uint8_t>(HighlightMark::Pending));
  EXPECT_EQ(marks[1], static_cast<uint8_t>(HighlightMark::Pending));
  EXPECT_EQ(marks[2], static_cast<uint8_t>(HighlightMark::None));

  markAt(page, 3, "alpha beta", HIGHLIGHT_FLAG_POSTED, marks);
  EXPECT_EQ(marks[0], static_cast<uint8_t>(HighlightMark::Posted));
  EXPECT_EQ(marks[1], static_cast<uint8_t>(HighlightMark::Posted));
}

TEST(HighlightMarkTest, MarksEveryNonOverlappingFullOccurrence) {
  const char* page[] = {"red", "red", "blue", "red"};
  uint8_t marks[4] = {};
  HighlightWordView views[4];
  words(views, page, 4);
  const char* quotes[] = {"red"};
  const uint8_t flags[] = {HIGHLIGHT_FLAG_POSTED};
  markHighlightWords(views, 4, quotes, flags, 1, marks);
  EXPECT_EQ(marks[0], static_cast<uint8_t>(HighlightMark::Posted));
  EXPECT_EQ(marks[1], static_cast<uint8_t>(HighlightMark::Posted));
  EXPECT_EQ(marks[2], static_cast<uint8_t>(HighlightMark::None));
  EXPECT_EQ(marks[3], static_cast<uint8_t>(HighlightMark::Posted));
}

TEST(HighlightMarkTest, PostedWinsWhenTheSameWordsAreAlsoPending) {
  const char* page[] = {"one", "two", "three"};
  uint8_t marks[3] = {};
  HighlightWordView views[3];
  words(views, page, 3);
  const char* quotes[] = {"one two", "one two three"};
  const uint8_t flags[] = {0, HIGHLIGHT_FLAG_POSTED};
  markHighlightWords(views, 3, quotes, flags, 2, marks);
  EXPECT_EQ(marks[0], static_cast<uint8_t>(HighlightMark::Posted));
  EXPECT_EQ(marks[1], static_cast<uint8_t>(HighlightMark::Posted));
  EXPECT_EQ(marks[2], static_cast<uint8_t>(HighlightMark::Posted));
}

TEST(HighlightMarkTest, PartialMatchOnlyAtThePageEdges) {
  const char* page[] = {"the", "quick", "brown", "fox", "jumps"};
  uint8_t marks[5] = {};
  HighlightWordView views[5];
  words(views, page, 5);

  const char* prefix = "brown fox jumps over the lazy";
  const uint8_t pending[] = {0};
  markHighlightWords(views, 5, &prefix, pending, 1, marks);
  EXPECT_EQ(marks[0], static_cast<uint8_t>(HighlightMark::None));
  EXPECT_EQ(marks[1], static_cast<uint8_t>(HighlightMark::None));
  EXPECT_EQ(marks[2], static_cast<uint8_t>(HighlightMark::Pending));
  EXPECT_EQ(marks[3], static_cast<uint8_t>(HighlightMark::Pending));
  EXPECT_EQ(marks[4], static_cast<uint8_t>(HighlightMark::Pending));

  const char* suffix = "over the lazy the quick brown";
  markHighlightWords(views, 5, &suffix, pending, 1, marks);
  EXPECT_EQ(marks[0], static_cast<uint8_t>(HighlightMark::Pending));
  EXPECT_EQ(marks[1], static_cast<uint8_t>(HighlightMark::Pending));
  EXPECT_EQ(marks[2], static_cast<uint8_t>(HighlightMark::Pending));
  EXPECT_EQ(marks[3], static_cast<uint8_t>(HighlightMark::None));
  EXPECT_EQ(marks[4], static_cast<uint8_t>(HighlightMark::None));

  const char* middle = "quick brown fox extra words here";
  markHighlightWords(views, 5, &middle, pending, 1, marks);
  for (uint8_t mark : marks) {
    EXPECT_EQ(mark, static_cast<uint8_t>(HighlightMark::None));
  }
}

TEST(HighlightMarkTest, ShortEdgeFragmentIsNotMarked) {
  const char* page[] = {"only", "two", "left"};
  uint8_t marks[3] = {9, 9, 9};
  HighlightWordView views[3];
  words(views, page, 3);
  const char* quote = "two left behind";
  const uint8_t pending[] = {0};
  markHighlightWords(views, 3, &quote, pending, 1, marks);
  EXPECT_EQ(marks[0], static_cast<uint8_t>(HighlightMark::None));
  EXPECT_EQ(marks[1], static_cast<uint8_t>(HighlightMark::None));
  EXPECT_EQ(marks[2], static_cast<uint8_t>(HighlightMark::None));
}

HighlightWordMark markedWord(const int16_t x, const int16_t width, const uint16_t line, const HighlightMark mark) {
  HighlightWordMark word;
  word.x = x;
  word.top = 10;
  word.width = width;
  word.height = 18;
  word.underlineY = 26;
  word.line = line;
  word.mark = static_cast<uint8_t>(mark);
  return word;
}

TEST(HighlightShade, BandCoversTheGlyphAndNotTheLinePitch) {
  // A 16px ascender used to be shaded through the underline (ascender + 2 + 3).
  EXPECT_EQ(highlightShadeBandHeight(16, false), 16);
  EXPECT_LT(highlightShadeBandHeight(16, false), 16 + 2 + HIGHLIGHT_DARK_UNDERLINE_PX);
  EXPECT_EQ(highlightShadeBandHeight(16, true), 8);
  EXPECT_EQ(highlightShadeBandHeight(0, false), 0);
  EXPECT_EQ(highlightShadeBandHeight(-4, true), 0);
}

TEST(HighlightRun, AdjacentWordsOnOneLineBecomeOneRun) {
  const HighlightWordMark words[] = {
      markedWord(10, 20, 0, HighlightMark::Pending), markedWord(40, 15, 0, HighlightMark::Pending),
      markedWord(70, 10, 0, HighlightMark::Posted),  markedWord(90, 12, 1, HighlightMark::Posted),
      markedWord(110, 8, 1, HighlightMark::None),    markedWord(130, 14, 1, HighlightMark::Pending),
  };
  HighlightRun runs[8] = {};
  const uint8_t count = mergeHighlightRuns(words, 6, runs, 8);
  ASSERT_EQ(count, 4u);
  // The gap between x=30 and x=40 is inside the run.
  EXPECT_EQ(runs[0].x, 10);
  EXPECT_EQ(runs[0].width, 45);
  EXPECT_EQ(runs[0].mark, static_cast<uint8_t>(HighlightMark::Pending));
  // A different mark, then a different line, each start a new run.
  EXPECT_EQ(runs[1].x, 70);
  EXPECT_EQ(runs[1].width, 10);
  EXPECT_EQ(runs[1].mark, static_cast<uint8_t>(HighlightMark::Posted));
  EXPECT_EQ(runs[2].x, 90);
  EXPECT_EQ(runs[2].width, 12);
  EXPECT_EQ(runs[2].mark, static_cast<uint8_t>(HighlightMark::Posted));
  EXPECT_EQ(runs[3].x, 130);
  EXPECT_EQ(runs[3].width, 14);
  EXPECT_EQ(runs[3].mark, static_cast<uint8_t>(HighlightMark::Pending));
}

TEST(HighlightRun, StopsWhenTheRunBufferIsFull) {
  const HighlightWordMark words[] = {
      markedWord(0, 4, 0, HighlightMark::Pending),
      markedWord(0, 4, 1, HighlightMark::Pending),
      markedWord(0, 4, 2, HighlightMark::Pending),
  };
  HighlightRun runs[2] = {};
  EXPECT_EQ(mergeHighlightRuns(words, 3, runs, 2), 2u);
  EXPECT_EQ(runs[0].x, 0);
}

TEST(HighlightNudge, QuoteCrossesThePageBreak) {
  // Four lines on the saved page, four on the next. One word per line.
  const char* tokens[] = {"p0", "p1", "p2", "p3", "n0", "n1", "n2", "n3"};
  const uint16_t wordLines[] = {0, 1, 2, 3, 4, 5, 6, 7};
  uint16_t indexes[8] = {};

  const uint16_t nudged = nudgedViewIndices(wordLines, 8, 0, 4, HIGHLIGHT_NUDGE_LINES, indexes, 8);
  ASSERT_EQ(nudged, 4u);
  EXPECT_EQ(indexes[0], 3u);
  EXPECT_EQ(indexes[1], 4u);
  EXPECT_EQ(indexes[2], 5u);
  EXPECT_EQ(indexes[3], 6u);

  const char* selected[4] = {};
  for (uint16_t i = 0; i < nudged; ++i) {
    selected[i] = tokens[indexes[i]];
  }
  char quote[HIGHLIGHT_TEXT_MAX + 1] = {};
  ASSERT_TRUE(quoteFromHighlightWords(selected, nudged, quote, sizeof(quote)));
  EXPECT_STREQ(quote, "p3 n0 n1 n2");

  // A second nudge accumulates, and the window stops at one page of lines.
  const uint16_t further = nudgedViewIndices(wordLines, 8, 0, 4, HIGHLIGHT_NUDGE_LINES * 2, indexes, 8);
  ASSERT_EQ(further, 4u);
  EXPECT_EQ(indexes[0], 4u);
  EXPECT_EQ(indexes[3], 7u);

  const uint16_t back = nudgedViewIndices(wordLines, 8, 4, 4, -HIGHLIGHT_NUDGE_LINES, indexes, 8);
  ASSERT_EQ(back, 4u);
  EXPECT_EQ(indexes[0], 1u);
  EXPECT_EQ(indexes[3], 4u);
}

TEST(HighlightMarkTest, FullMatchSuppressesAShorterEdgeFragment) {
  const char* page[] = {"alpha", "beta", "gamma", "alpha"};
  uint8_t marks[4] = {};
  HighlightWordView views[4];
  words(views, page, 4);
  const char* quote = "alpha beta gamma";
  const uint8_t pending[] = {0};
  markHighlightWords(views, 4, &quote, pending, 1, marks);
  EXPECT_EQ(marks[0], static_cast<uint8_t>(HighlightMark::Pending));
  EXPECT_EQ(marks[1], static_cast<uint8_t>(HighlightMark::Pending));
  EXPECT_EQ(marks[2], static_cast<uint8_t>(HighlightMark::Pending));
  EXPECT_EQ(marks[3], static_cast<uint8_t>(HighlightMark::None));
}
