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

 private:
  std::vector<BitrixSearchSseEvent> Drain(bool finish);
  std::string buffer_;
};

#endif
