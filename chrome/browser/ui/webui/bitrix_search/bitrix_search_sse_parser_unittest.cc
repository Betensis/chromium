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
