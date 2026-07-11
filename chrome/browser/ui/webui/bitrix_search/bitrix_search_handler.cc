#include "chrome/browser/ui/webui/bitrix_search/bitrix_search_handler.h"

#include <cstdlib>
#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace {
constexpr char kSearchUrl[] = "https://marta-ai-web-search-vkcs.bitrix24.tech/v2/search/stream";
constexpr char kTokenPrefName[] = "bitrix_search.token";

// Debug-only fallback for environments where persisting via the pref isn't
// convenient (e.g. quick local testing). The persisted pref always takes
// priority when present. Compiled out of release builds: this reads a
// plaintext token straight out of the process environment, which is not an
// acceptable channel outside of local debugging.
std::string GetEnvToken() {
#if !defined(NDEBUG)
  if (const char* token = std::getenv("BITRIX_SEARCH_TOKEN"); token && *token) {
    return token;
  }
#endif
  return {};
}

// Limits below guard against an adversarial/misbehaving backend growing our
// buffers without bound for the lifetime of a single search.
constexpr size_t kMaxAnswerBytes = 1024 * 1024;  // 1 MB
constexpr size_t kMaxSources = 100;

// Only "simple"/"deep" are valid search modes; anything else (including
// empty/missing) normalizes to "deep".
std::string NormalizeMode(const std::string& mode) {
  return mode == "simple" ? "simple" : "deep";
}

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("bitrix_gpt_search", R"(
      semantics { sender: "BitrixGPT Search" description: "User initiated web search." trigger: "Omnibox search." data: "Search terms." destination: OTHER }
      policy { cookies_allowed: NO setting: "This hackathon build uses BitrixGPT as its search provider." policy_exception_justification: "Hackathon-only integration." })");
}  // namespace

BitrixSearchHandler::BitrixSearchHandler() {
  // Kick off encryptor resolution as early as possible so it's typically
  // already available by the time the page actually needs the token. See
  // PromoCardsHandler (notification_cards_handler.cc) for the same pattern.
  if (g_browser_process && g_browser_process->os_crypt_async()) {
    g_browser_process->os_crypt_async()->GetInstance(base::BindOnce(
        &BitrixSearchHandler::OnEncryptorReady, weak_factory_.GetWeakPtr()));
  }
}
BitrixSearchHandler::~BitrixSearchHandler() = default;

// static
void BitrixSearchHandler::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kTokenPrefName, std::string());
}

void BitrixSearchHandler::OnJavascriptDisallowed() {
  // Tear down any in-flight stream immediately. OnDataReceived/OnComplete are
  // called directly by `loader_` (not gated by `weak_factory_`), so without
  // this a stream still running when the tab navigates/closes would keep
  // calling FireWebUIListener, which CHECKs IsJavascriptAllowed() and crashes.
  CancelSearch({});
  // Any pending encryptor-gated continuation (RunWithEncryptor) or the
  // constructor's OnEncryptorReady would otherwise talk to the (now gone)
  // renderer via FireWebUIListener.
  weak_factory_.InvalidateWeakPtrs();
}

void BitrixSearchHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("startSearch", base::BindRepeating(
      &BitrixSearchHandler::StartSearch, base::Unretained(this)));
  web_ui()->RegisterMessageCallback("cancelSearch", base::BindRepeating(
      &BitrixSearchHandler::CancelSearch, base::Unretained(this)));
  web_ui()->RegisterMessageCallback("setToken", base::BindRepeating(
      &BitrixSearchHandler::SetToken, base::Unretained(this)));
  web_ui()->RegisterMessageCallback("clearToken", base::BindRepeating(
      &BitrixSearchHandler::ClearToken, base::Unretained(this)));
  web_ui()->RegisterMessageCallback("pageReady", base::BindRepeating(
      &BitrixSearchHandler::PageReady, base::Unretained(this)));
}

void BitrixSearchHandler::StartSearch(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) return;
  const std::string& query = args[0].GetString();
  if (query.empty()) return;
  std::string mode;
  if (args.size() > 1 && args[1].is_string()) mode = args[1].GetString();
  mode = NormalizeMode(mode);
  last_query_ = query;
  last_mode_ = mode;
  StartSearchQuery(query, mode);
}

void BitrixSearchHandler::SetToken(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) return;
  std::string token(base::TrimWhitespaceASCII(args[0].GetString(), base::TRIM_ALL));
  if (base::StartsWith(token, "Bearer ")) {
    token.erase(0, 7);
    token = std::string(base::TrimWhitespaceASCII(token, base::TRIM_ALL));
  }
  if (token.empty()) return;
  AllowJavascript();
  const int operation_id = ++operation_id_;
  RunWithEncryptor(base::BindOnce(&BitrixSearchHandler::DoSetToken,
                                  weak_factory_.GetWeakPtr(), operation_id,
                                  std::move(token)));
}

void BitrixSearchHandler::ClearToken(const base::ListValue&) {
  // Bumps operation_id_ (invalidating any pending DoSetToken/
  // DoStartSearchQuery queued before this) and drops the active loader, so a
  // search running under the old token doesn't keep streaming after it's
  // cleared.
  CancelSearch({});
  Profile::FromWebUI(web_ui())->GetPrefs()->ClearPref(kTokenPrefName);
  AllowJavascript();
  SendTokenStatus();
}

void BitrixSearchHandler::SetPendingQuery(std::string query, std::string mode) {
  pending_query_ = std::move(query);
  pending_mode_ = NormalizeMode(mode);
}

void BitrixSearchHandler::PageReady(const base::ListValue&) {
  AllowJavascript();
  SendTokenStatus();
  if (pending_query_.empty()) return;
  const std::string query = std::exchange(pending_query_, {});
  const std::string mode = std::exchange(pending_mode_, std::string("deep"));
  last_query_ = query;
  last_mode_ = mode;
  if (HasConfiguredToken()) {
    StartSearchQuery(query, mode);
  }
  // If no token is configured we deliberately do not fail here: the UI
  // shows the onboarding form based on bitrix-search-status, and the
  // remembered last_query_/last_mode_ will be retried automatically once
  // setToken succeeds.
}

void BitrixSearchHandler::StartSearchQuery(const std::string& query,
                                            const std::string& mode) {
  if (query.empty()) return;
  AllowJavascript();
  if (!HasConfiguredToken()) {
    Fail("token", "Токен Bitrix Search не настроен.");
    return;
  }
  const int operation_id = ++operation_id_;
  RunWithEncryptor(base::BindOnce(&BitrixSearchHandler::DoStartSearchQuery,
                                  weak_factory_.GetWeakPtr(), operation_id,
                                  query, mode));
}

void BitrixSearchHandler::RunWithEncryptor(base::OnceClosure callback) {
  if (encryptor_) {
    std::move(callback).Run();
    return;
  }
  pending_encryptor_callbacks_.push_back(std::move(callback));
}

void BitrixSearchHandler::OnEncryptorReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  encryptor_ = std::move(encryptor);
  std::vector<base::OnceClosure> callbacks;
  callbacks.swap(pending_encryptor_callbacks_);
  for (auto& callback : callbacks) {
    std::move(callback).Run();
  }
}

bool BitrixSearchHandler::HasConfiguredToken() {
  Profile* profile = Profile::FromWebUI(web_ui());
  if (!profile->GetPrefs()->GetString(kTokenPrefName).empty()) return true;
  return !GetEnvToken().empty();
}

std::string BitrixSearchHandler::GetEffectiveToken() {
  Profile* profile = Profile::FromWebUI(web_ui());
  const std::string stored = profile->GetPrefs()->GetString(kTokenPrefName);
  if (!stored.empty() && encryptor_) {
    std::string ciphertext;
    if (base::Base64Decode(stored, &ciphertext)) {
      std::string plaintext;
      if (encryptor_->DecryptString(ciphertext, &plaintext)) {
        return plaintext;
      }
    }
  }
  return GetEnvToken();
}

void BitrixSearchHandler::PersistToken(const std::string& token) {
  if (!encryptor_) return;
  std::string ciphertext;
  if (!encryptor_->EncryptString(token, &ciphertext)) return;
  Profile::FromWebUI(web_ui())->GetPrefs()->SetString(
      kTokenPrefName, base::Base64Encode(ciphertext));
}

void BitrixSearchHandler::SendTokenStatus() {
  base::DictValue out;
  out.Set("tokenConfigured", HasConfiguredToken());
  FireWebUIListener("bitrix-search-status", out);
}

void BitrixSearchHandler::DoSetToken(int operation_id, std::string token) {
  if (operation_id != operation_id_) return;  // Superseded; see header comment.
  PersistToken(token);
  SendTokenStatus();
  if (!last_query_.empty()) {
    StartSearchQuery(last_query_, last_mode_);
  }
}

void BitrixSearchHandler::DoStartSearchQuery(int operation_id,
                                              const std::string& query,
                                              const std::string& mode) {
  if (operation_id != operation_id_) return;  // Superseded; see header comment.
  CancelSearch({});
  if (query.empty()) return;
  const std::string token = GetEffectiveToken();
  if (token.empty()) {
    Fail("token", "Токен Bitrix Search не настроен.");
    return;
  }
  base::DictValue body;
  body.Set("query", query); body.Set("mode", mode);
  body.Set("lang", "ru"); body.Set("max_steps", mode == "simple" ? 3 : 5);
  std::string json; base::JSONWriter::Write(body, &json);
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(kSearchUrl); request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->headers.SetHeader("Authorization", "Bearer " + token);
  request->headers.SetHeader("Accept", "text/event-stream");
  loader_ = network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  // Deep-mode runs can legitimately stream for well over two minutes; 120s
  // was observed to cut off live runs past ~130s.
  loader_->SetTimeoutDuration(base::Seconds(300));
  loader_->AttachStringForUpload(json, "application/json");
  base::DictValue started;
  started.Set("query", query);
  started.Set("mode", mode);
  FireWebUIListener("bitrix-search-started", started);
  auto* profile = Profile::FromWebUI(web_ui());
  loader_->DownloadAsStream(profile->GetURLLoaderFactory().get(), this);
}

void BitrixSearchHandler::CancelSearch(const base::ListValue&) {
  // Invalidates any DoSetToken/DoStartSearchQuery continuation queued before
  // this call (see operation_id_ comment in the header).
  ++operation_id_;
  loader_.reset();
  parser_ = BitrixSearchSseParser();
  got_done_ = false;
  failed_ = false;
  synthesizing_sent_ = false;
  last_progress_kind_.clear();
  answer_.clear();
  sources_.clear();
}

void BitrixSearchHandler::OnDataReceived(std::string_view data, base::OnceClosure resume) {
  for (const auto& event : parser_.Consume(data)) {
    ConsumeEvent(event);
    // Fail() (or the overflow check below, on a previous chunk) already tore
    // down `loader_`; stop feeding it further events from this chunk.
    if (!loader_) break;
  }
  if (loader_ && parser_.IsOverflowing()) {
    Fail("server", "Ответ поискового сервиса превысил допустимый размер.");
  }
  // Don't resume a stream we just cancelled: `loader_` (and the pipe backing
  // `resume`) no longer exists.
  if (loader_) std::move(resume).Run();
}

void BitrixSearchHandler::OnComplete(bool success) {
  // Only trust a dangling (blank-line-less) final SSE block when the
  // transport itself reported clean completion. On an actual network/mojo
  // failure the body can be cut off at an arbitrary byte offset; treating
  // whatever happens to be buffered as a real event risks synthesizing a
  // false "done"/"error" that masks the failure handled below (see
  // `got_done_` check). BitrixSearchSseParser::Finish() itself is left
  // unchanged — flushing a syntactically-complete dangling block is still
  // correct for servers that legitimately omit the trailing blank line.
  if (success) {
    for (const auto& event : parser_.Finish()) ConsumeEvent(event);
  }
  if (!got_done_) {
    int response_code = 0;
    if (loader_ && loader_->ResponseInfo() && loader_->ResponseInfo()->headers) {
      response_code = loader_->ResponseInfo()->headers->response_code();
    }
    const int net_error = loader_ ? loader_->NetError() : net::ERR_FAILED;
    if (response_code == 401 || response_code == 403) {
      Fail("token", "Токен Bitrix Search недействителен.");
    } else if (!success || net_error != net::OK) {
      Fail("network", "Сервис поиска недоступен.");
    } else {
      Fail("server", "Поисковый агент завершил поток без результата.");
    }
  }
  loader_.reset();
}

void BitrixSearchHandler::OnRetry(base::OnceClosure start_retry) { std::move(start_retry).Run(); }

void BitrixSearchHandler::ConsumeEvent(const BitrixSearchSseEvent& event) {
  // Once Fail() has fired for this search, ignore anything further (e.g. a
  // late "done" arriving after an "error", or events flushed by Finish()):
  // failure is terminal until the next CancelSearch()/StartSearchQuery().
  if (failed_) return;

  if (event.type == "thinking") {
    if (last_progress_kind_ != "thinking") {
      base::DictValue out;
      out.Set("kind", "thinking");
      out.Set("message", "Анализирую найденные материалы");
      FireWebUIListener("bitrix-search-progress", out);
      last_progress_kind_ = "thinking";
    }
    return;
  }

  auto value = base::JSONReader::Read(event.data, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    Fail("server", "Получен повреждённый ответ поиска.");
    return;
  }
  const base::DictValue& dict = value->GetDict();

  if (event.type == "tool_call") {
    const std::string* tool = dict.FindString("tool");
    const base::DictValue* arguments = dict.FindDict("arguments");
    if (!tool || !arguments) return;
    if (*tool == "yandex_web_search" || *tool == "tavily_web_search" ||
        *tool == "search_web") {
      const std::string* query = arguments->FindString("query");
      if (!query) return;
      base::DictValue out;
      out.Set("kind", "search");
      out.Set("query", *query);
      FireWebUIListener("bitrix-search-progress", out);
      last_progress_kind_ = "search";
    } else if (*tool == "extract_pages" || *tool == "tavily_extract") {
      const base::ListValue* urls = arguments->FindList("urls");
      if (!urls) return;
      base::ListValue out_urls;
      for (const auto& url_value : *urls) {
        if (out_urls.size() >= 10) break;
        if (!url_value.is_string()) continue;
        const std::string& url = url_value.GetString();
        if (base::StartsWith(url, "http://") || base::StartsWith(url, "https://")) {
          out_urls.Append(url);
        }
      }
      base::DictValue out;
      out.Set("kind", "open");
      out.Set("urls", std::move(out_urls));
      FireWebUIListener("bitrix-search-progress", out);
      last_progress_kind_ = "open";
    }
  } else if (event.type == "tool_result") {
    const std::string* result_type = dict.FindString("result_type");
    const base::ListValue* items = dict.FindList("items");
    if (!result_type) return;
    if (*result_type == "search") {
      base::ListValue preview;
      if (items) {
        for (const auto& item : *items) {
          if (preview.size() >= 5) break;
          if (!item.is_dict()) continue;
          const base::DictValue& item_dict = item.GetDict();
          base::DictValue entry;
          if (const std::string* url = item_dict.FindString("url")) entry.Set("url", *url);
          if (const std::string* title = item_dict.FindString("title")) entry.Set("title", *title);
          preview.Append(std::move(entry));
        }
      }
      base::DictValue out;
      out.Set("kind", "found");
      out.Set("count", static_cast<int>(items ? items->size() : 0));
      out.Set("items", std::move(preview));
      FireWebUIListener("bitrix-search-progress", out);
      last_progress_kind_ = "found";
    } else if (*result_type == "extract") {
      base::DictValue out;
      out.Set("kind", "read");
      out.Set("count", static_cast<int>(items ? items->size() : 0));
      FireWebUIListener("bitrix-search-progress", out);
      last_progress_kind_ = "read";
    }
  } else if (event.type == "message") {
    if (!synthesizing_sent_) {
      base::DictValue progress;
      progress.Set("kind", "synthesizing");
      FireWebUIListener("bitrix-search-progress", progress);
      synthesizing_sent_ = true;
      last_progress_kind_ = "synthesizing";
    }
    if (const std::string* content = dict.FindString("content")) answer_ += *content;
    if (answer_.size() > kMaxAnswerBytes) {
      Fail("server", "Ответ поискового сервиса превысил допустимый размер.");
      return;
    }
    base::DictValue out;
    out.Set("answer", answer_); out.Set("sources", sources_.Clone());
    FireWebUIListener("bitrix-search-answer", out);
  } else if (event.type == "done") {
    got_done_ = true;
    if (const std::string* answer = dict.FindString("answer")) answer_ = *answer;
    if (const base::ListValue* sources = dict.FindList("sources")) {
      if (sources->size() > kMaxSources) {
        Fail("server", "Слишком много источников в ответе поиска.");
        return;
      }
      sources_ = sources->Clone();
    }
    if (answer_.size() > kMaxAnswerBytes) {
      Fail("server", "Ответ поискового сервиса превысил допустимый размер.");
      return;
    }
    if (answer_.empty()) { Fail("server", "Поиск завершён без ответа."); return; }
    base::DictValue out;
    out.Set("answer", answer_); out.Set("sources", sources_.Clone());
    FireWebUIListener("bitrix-search-completed", out);
  } else if (event.type == "error") {
    std::string message = "Поисковый агент сообщил об ошибке.";
    if (const std::string* error_text = dict.FindString("error");
        error_text && !error_text->empty()) {
      message = *error_text;
    }
    Fail("server", message);
  }
}

void BitrixSearchHandler::Fail(std::string kind, std::string message) {
  // Terminal: ignore a second Fail() for the same search (e.g. OnComplete's
  // own failure synthesis running after ConsumeEvent already failed it), and
  // stop the stream so no further events can arrive.
  if (failed_) return;
  failed_ = true;
  loader_.reset();
  base::DictValue out;
  out.Set("kind", std::move(kind));
  out.Set("message", std::move(message));
  FireWebUIListener("bitrix-search-failed", out);
}
