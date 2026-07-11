#include "chrome/browser/ui/webui/bitrix_search/bitrix_search_sse_parser.h"

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

std::vector<BitrixSearchSseEvent> BitrixSearchSseParser::Consume(
    std::string_view chunk) {
  buffer_.append(chunk);
  return Drain(false);
}

std::vector<BitrixSearchSseEvent> BitrixSearchSseParser::Finish() {
  return Drain(true);
}

std::vector<BitrixSearchSseEvent> BitrixSearchSseParser::Drain(bool finish) {
  std::vector<BitrixSearchSseEvent> result;
  size_t boundary;
  while ((boundary = buffer_.find("\n\n")) != std::string::npos ||
         (finish && !buffer_.empty())) {
    const size_t length = boundary == std::string::npos ? buffer_.size() : boundary;
    std::string block = buffer_.substr(0, length);
    buffer_.erase(0, boundary == std::string::npos ? length : length + 2);
    std::string type;
    std::vector<std::string> data;
    for (std::string_view line : base::SplitStringPiece(
             block, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      if (base::StartsWith(line, "event:"))
        type = std::string(base::TrimWhitespaceASCII(line.substr(6), base::TRIM_ALL));
      else if (base::StartsWith(line, "data:"))
        data.emplace_back(base::TrimWhitespaceASCII(line.substr(5), base::TRIM_ALL));
    }
    if (!type.empty() && !data.empty())
      result.push_back({std::move(type), base::JoinString(data, "\n")});
  }
  return result;
}
