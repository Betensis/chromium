#ifndef CHROME_BROWSER_UI_WEBUI_BITRIX_SEARCH_BITRIX_SEARCH_SSE_PARSER_H_
#define CHROME_BROWSER_UI_WEBUI_BITRIX_SEARCH_BITRIX_SEARCH_SSE_PARSER_H_

#include <string>
#include <string_view>
#include <vector>

struct BitrixSearchSseEvent {
  std::string type;
  std::string data;
};

class BitrixSearchSseParser {
 public:
  std::vector<BitrixSearchSseEvent> Consume(std::string_view chunk);
  std::vector<BitrixSearchSseEvent> Finish();

  // True once the currently-buffered (not yet dispatched) SSE block exceeds
  // a sane size cap. A backend that never sends the blank-line terminator
  // would otherwise grow `buffer_` without bound; the caller
  // (BitrixSearchHandler) should treat this as a fatal stream error.
  bool IsOverflowing() const;

 private:
  std::vector<BitrixSearchSseEvent> Drain(bool finish);
  // Collapses CRLF and lone-CR line endings in `buffer_` to LF. When `eof`
  // is false, a trailing lone '\r' is left untouched, since it may be the
  // first half of a '\r\n' pair split across chunk boundaries; `Finish()`
  // passes `eof=true` to resolve it once no more data is coming.
  void NormalizeLineEndings(bool eof);
  std::string buffer_;
};

#endif
