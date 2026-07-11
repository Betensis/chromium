#include "chrome/browser/ui/webui/bitrix_search/bitrix_search_sse_parser.h"

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace {
// Bound on a single buffered (not yet dispatched) SSE block. See
// IsOverflowing().
constexpr size_t kMaxBufferedBytes = 256 * 1024;  // 256 KB
}  // namespace

std::vector<BitrixSearchSseEvent> BitrixSearchSseParser::Consume(
    std::string_view chunk) {
  buffer_.append(chunk);
  NormalizeLineEndings(/*eof=*/false);
  return Drain(false);
}

std::vector<BitrixSearchSseEvent> BitrixSearchSseParser::Finish() {
  NormalizeLineEndings(/*eof=*/true);
  return Drain(true);
}

void BitrixSearchSseParser::NormalizeLineEndings(bool eof) {
  size_t pos = 0;
  while ((pos = buffer_.find('\r', pos)) != std::string::npos) {
    if (pos + 1 < buffer_.size()) {
      if (buffer_[pos + 1] == '\n') {
        // CRLF: drop the '\r', keep the '\n'; re-check from the same `pos`
        // (now pointing at that '\n', which is not itself a '\r').
        buffer_.erase(pos, 1);
        continue;
      }
      // Lone CR (old Mac style) followed by more data: it's a line ending.
      buffer_[pos] = '\n';
      ++pos;
      continue;
    }
    // Trailing '\r' with nothing after it (yet). Only resolve it to '\n' at
    // EOF; otherwise it may be the first half of a '\r\n' pair split across
    // this chunk and the next one, so leave it buffered as-is.
    if (eof) buffer_[pos] = '\n';
    break;
  }
}

bool BitrixSearchSseParser::IsOverflowing() const {
  return buffer_.size() > kMaxBufferedBytes;
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
