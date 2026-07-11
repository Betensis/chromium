#ifndef CHROME_BROWSER_UI_WEBUI_BITRIX_SEARCH_BITRIX_SEARCH_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_BITRIX_SEARCH_BITRIX_SEARCH_HANDLER_H_

#include <memory>
#include <string>

#include "base/values.h"
#include "chrome/browser/ui/webui/bitrix_search/bitrix_search_sse_parser.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"

namespace network { class SimpleURLLoader; }

class BitrixSearchHandler : public content::WebUIMessageHandler,
                            public network::SimpleURLLoaderStreamConsumer {
 public:
  BitrixSearchHandler();
  ~BitrixSearchHandler() override;
  void RegisterMessages() override;
  void OnDataReceived(std::string_view data, base::OnceClosure resume) override;
  void OnComplete(bool success) override;
  void OnRetry(base::OnceClosure start_retry) override;

 private:
  void StartSearch(const base::ListValue& args);
  void CancelSearch(const base::ListValue& args);
  void ConsumeEvent(const BitrixSearchSseEvent& event);
  void Fail(std::string message);
  std::unique_ptr<network::SimpleURLLoader> loader_;
  BitrixSearchSseParser parser_;
  bool got_done_ = false;
  std::string answer_;
  base::ListValue sources_;
};

#endif
