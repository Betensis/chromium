#ifndef CHROME_BROWSER_UI_WEBUI_BITRIX_SEARCH_BITRIX_SEARCH_UI_H_
#define CHROME_BROWSER_UI_WEBUI_BITRIX_SEARCH_BITRIX_SEARCH_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

class BitrixSearchUI;
class BitrixSearchUIConfig : public content::DefaultWebUIConfig<BitrixSearchUI> {
 public:
  BitrixSearchUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, "bitrix-search") {}
};

class BitrixSearchUI : public content::WebUIController {
 public:
  BitrixSearchUI(content::WebUI* web_ui, const GURL& url);
  ~BitrixSearchUI() override;
};

#endif
