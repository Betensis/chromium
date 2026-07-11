#include "chrome/browser/ui/webui/bitrix_search/bitrix_search_ui.h"

#include "chrome/browser/ui/webui/bitrix_search/bitrix_search_handler.h"
#include "chrome/grit/bitrix_search_resources.h"
#include "chrome/grit/bitrix_search_resources_map.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_data_source.h"
#include "net/base/url_util.h"
#include "ui/webui/webui_util.h"

BitrixSearchUI::BitrixSearchUI(content::WebUI* web_ui, const GURL& url)
    : WebUIController(web_ui) {
  auto* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(), "bitrix-search");
  webui::SetupWebUIDataSource(source, kBitrixSearchResources,
                              IDR_BITRIX_SEARCH_BITRIX_SEARCH_HTML);
  auto handler = std::make_unique<BitrixSearchHandler>();
  auto* handler_ptr = handler.get();
  web_ui->AddMessageHandler(std::move(handler));

  std::string query;
  if (net::GetValueForKeyInQuery(url, "q", &query) && !query.empty()) {
    std::string mode;
    net::GetValueForKeyInQuery(url, "mode", &mode);
    handler_ptr->SetPendingQuery(std::move(query), std::move(mode));
  }
}
BitrixSearchUI::~BitrixSearchUI() = default;
