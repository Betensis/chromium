#include "chrome/browser/ui/webui/bitrix_search/bitrix_search_handler.h"

#include <cstdlib>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"

namespace {
constexpr char kSearchUrl[] = "https://marta-ai-web-search-vkcs.bitrix24.tech/v2/search/stream";
constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("bitrix_gpt_search", R"(
      semantics { sender: "BitrixGPT Search" description: "User initiated web search." trigger: "Omnibox search." data: "Search terms." destination: OTHER }
      policy { cookies_allowed: NO setting: "This hackathon build uses BitrixGPT as its search provider." policy_exception_justification: "Hackathon-only integration." })");
}

BitrixSearchHandler::BitrixSearchHandler() = default;
BitrixSearchHandler::~BitrixSearchHandler() = default;

void BitrixSearchHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("startSearch", base::BindRepeating(
      &BitrixSearchHandler::StartSearch, base::Unretained(this)));
  web_ui()->RegisterMessageCallback("cancelSearch", base::BindRepeating(
      &BitrixSearchHandler::CancelSearch, base::Unretained(this)));
}

void BitrixSearchHandler::StartSearch(const base::ListValue& args) {
  CancelSearch({});
  if (args.empty() || !args[0].is_string() || args[0].GetString().empty()) return;
  const char* token = std::getenv("BITRIX_SEARCH_TOKEN");
  if (!token || !*token) { Fail("Токен BitrixGPT Search не настроен."); return; }
  base::DictValue body;
  body.Set("query", args[0].GetString()); body.Set("mode", "deep");
  body.Set("lang", "ru"); body.Set("max_steps", 5);
  std::string json; base::JSONWriter::Write(body, &json);
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(kSearchUrl); request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->headers.SetHeader("Authorization", std::string("Bearer ") + token);
  request->headers.SetHeader("Accept", "text/event-stream");
  loader_ = network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader_->SetTimeoutDuration(base::Seconds(30));
  loader_->AttachStringForUpload(json, "application/json");
  AllowJavascript();
  FireWebUIListener("bitrix-search-started");
  auto* profile = Profile::FromWebUI(web_ui());
  loader_->DownloadAsStream(profile->GetURLLoaderFactory().get(), this);
}

void BitrixSearchHandler::CancelSearch(const base::ListValue&) {
  loader_.reset(); parser_ = BitrixSearchSseParser(); got_done_ = false;
  answer_.clear(); sources_.clear();
}

void BitrixSearchHandler::OnDataReceived(std::string_view data, base::OnceClosure resume) {
  for (const auto& event : parser_.Consume(data)) ConsumeEvent(event);
  std::move(resume).Run();
}

void BitrixSearchHandler::OnComplete(bool success) {
  for (const auto& event : parser_.Finish()) ConsumeEvent(event);
  if (!got_done_) Fail(success ? "Поисковый агент завершил поток без результата." : "Сервис поиска недоступен.");
  loader_.reset();
}

void BitrixSearchHandler::OnRetry(base::OnceClosure start_retry) { std::move(start_retry).Run(); }

void BitrixSearchHandler::ConsumeEvent(const BitrixSearchSseEvent& event) {
  auto value = base::JSONReader::Read(event.data, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) { if (event.type != "thinking") Fail("Получен повреждённый ответ поиска."); return; }
  const auto& dict = value->GetDict();
  if (event.type == "thinking") {
    base::DictValue out; out.Set("message", "Анализирую найденные материалы");
    FireWebUIListener("bitrix-search-progress", out);
  } else if (event.type == "tool_call") {
    base::DictValue out; out.Set("message", "Открываю и проверяю источники");
    FireWebUIListener("bitrix-search-progress", out);
  } else if (event.type == "message") {
    if (const std::string* content = dict.FindString("content")) answer_ += *content;
    base::DictValue out; out.Set("answer", answer_); out.Set("sources", sources_.Clone());
    FireWebUIListener("bitrix-search-answer", out);
  } else if (event.type == "done") {
    got_done_ = true;
    if (const std::string* answer = dict.FindString("answer")) answer_ = *answer;
    if (const base::ListValue* sources = dict.FindList("sources")) sources_ = sources->Clone();
    if (answer_.empty()) { Fail("Поиск завершён без ответа."); return; }
    base::DictValue out; out.Set("answer", answer_); out.Set("sources", sources_.Clone());
    FireWebUIListener("bitrix-search-completed", out);
  } else if (event.type == "error") {
    Fail("Поисковый агент сообщил об ошибке.");
  }
}

void BitrixSearchHandler::Fail(std::string message) {
  base::DictValue out; out.Set("message", std::move(message));
  FireWebUIListener("bitrix-search-failed", out);
}
