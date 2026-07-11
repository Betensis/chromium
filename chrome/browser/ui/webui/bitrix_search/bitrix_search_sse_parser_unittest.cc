#include "chrome/browser/ui/webui/bitrix_search/bitrix_search_sse_parser.h"

#include "testing/gtest/include/gtest/gtest.h"

TEST(BitrixSearchSseParserTest, ParsesSplitAndMultilineEvents) {
  BitrixSearchSseParser parser;
  auto first = parser.Consume("event: message\ndata: {\"content\":\"Hel");
  EXPECT_TRUE(first.empty());
  auto second = parser.Consume("lo\"}\n\nevent: done\ndata: {\"answer\":\"Hello\",\ndata: \"sources\":[]}\n\n");
  ASSERT_EQ(2u, second.size());
  EXPECT_EQ("message", second[0].type);
  EXPECT_EQ("{\"content\":\"Hello\"}", second[0].data);
  EXPECT_EQ("done", second[1].type);
}

TEST(BitrixSearchSseParserTest, IgnoresCommentsAndUnknownFields) {
  BitrixSearchSseParser parser;
  auto events = parser.Consume(": keepalive\nid: 12\nevent: thinking\ndata: {}\n\n");
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ("thinking", events[0].type);
}

TEST(BitrixSearchSseParserTest, FlushesFinalEventWithoutBlankLine) {
  BitrixSearchSseParser parser;
  parser.Consume("event: error\ndata: {\"error\":\"failed\"}");
  auto events = parser.Finish();
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ("error", events[0].type);
}

TEST(BitrixSearchSseParserTest, ParsesHttpStyleCrLfBoundariesAcrossChunks) {
  BitrixSearchSseParser parser;
  EXPECT_TRUE(parser.Consume("event: message\r").empty());
  auto events = parser.Consume("\ndata: {\"content\":\"Hello\"}\r\n\r\n");
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ("message", events[0].type);
  EXPECT_EQ("{\"content\":\"Hello\"}", events[0].data);
}

TEST(BitrixSearchSseParserTest, ParsesLoneCrBoundaries) {
  BitrixSearchSseParser parser;
  // Old Mac-style line endings: bare '\r' instead of '\n' or '\r\n', with a
  // doubled '\r' as the blank-line block separator. The first block (whose
  // separator is followed by more data, "event: done...") resolves eagerly;
  // the very last '\r' is inherently ambiguous (could be half of a split
  // '\r\n') and only resolves at Finish().
  auto events = parser.Consume(
      "event: message\rdata: {\"content\":\"Hi\"}\r\revent: done\r"
      "data: {\"answer\":\"Hi\",\"sources\":[]}\r\r");
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ("message", events[0].type);
  EXPECT_EQ("{\"content\":\"Hi\"}", events[0].data);

  auto more = parser.Finish();
  ASSERT_EQ(1u, more.size());
  EXPECT_EQ("done", more[0].type);
}

TEST(BitrixSearchSseParserTest, ResolvesTrailingLoneCrOnlyAtFinish) {
  BitrixSearchSseParser parser;
  // A lone trailing '\r' is ambiguous mid-stream (it could be the first half
  // of a split '\r\n'), so it must not be resolved into a boundary until
  // Finish() confirms no more data is coming.
  auto mid = parser.Consume("event: message\rdata: {\"content\":\"Hi\"}\r");
  EXPECT_TRUE(mid.empty());
  auto events = parser.Finish();
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ("message", events[0].type);
  EXPECT_EQ("{\"content\":\"Hi\"}", events[0].data);
}
