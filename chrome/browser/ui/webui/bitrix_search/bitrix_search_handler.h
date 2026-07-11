#ifndef CHROME_BROWSER_UI_WEBUI_BITRIX_SEARCH_BITRIX_SEARCH_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_BITRIX_SEARCH_BITRIX_SEARCH_HANDLER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/ui/webui/bitrix_search/bitrix_search_sse_parser.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"

class PrefRegistrySimple;

namespace network {
class SimpleURLLoader;
}

namespace os_crypt_async {
class Encryptor;
}

// Backs the chrome://bitrix-search WebUI. Talks to the Bitrix Search
// streaming (SSE) backend and relays structured progress/answer events to
// the page. See the C++<->JS contract in the Bitrix Search spec for the
// exact message/event names and payload shapes.
class BitrixSearchHandler : public content::WebUIMessageHandler,
                            public network::SimpleURLLoaderStreamConsumer {
 public:
  BitrixSearchHandler();
  ~BitrixSearchHandler() override;
  void RegisterMessages() override;
  // `mode` must be "simple" or "deep"; anything else (including empty) is
  // normalized to "deep".
  void SetPendingQuery(std::string query, std::string mode = "deep");
  void OnDataReceived(std::string_view data, base::OnceClosure resume) override;
  void OnComplete(bool success) override;
  void OnRetry(base::OnceClosure start_retry) override;

  // Registers the `bitrix_search.token` profile pref that stores the
  // (encrypted) persistent auth token. Called from
  // chrome::RegisterProfilePrefs (chrome/browser/prefs/browser_prefs.cc).
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

 private:
  // WebUIMessageHandler:
  void OnJavascriptDisallowed() override;

  // chrome.send message handlers.
  void StartSearch(const base::ListValue& args);
  void CancelSearch(const base::ListValue& args);
  void SetToken(const base::ListValue& args);
  void ClearToken(const base::ListValue& args);
  void PageReady(const base::ListValue& args);

  // Kicks off a search for `query` in `mode` ("simple"/"deep"), failing fast
  // with kind:"token" if no token is configured yet (pref nor env fallback).
  void StartSearchQuery(const std::string& query, const std::string& mode);

  // Runs `callback` once `encryptor_` is available, queuing it otherwise.
  void RunWithEncryptor(base::OnceClosure callback);
  void OnEncryptorReady(scoped_refptr<os_crypt_async::Encryptor> encryptor);

  // Encryptor-gated continuations of StartSearchQuery/SetToken. `operation_id`
  // is the value of `operation_id_` at the time the continuation was queued;
  // if `operation_id_` has since moved on (setToken/clearToken/cancelSearch
  // ran in the meantime), the continuation is stale and no-ops. This is what
  // stops a pending setToken from reviving a token/search that clearToken
  // already tore down while the encryptor wasn't ready yet.
  void DoStartSearchQuery(int operation_id, const std::string& query,
                          const std::string& mode);
  void DoSetToken(int operation_id, std::string token);

  // True if a token is persisted (pref) or available via the env fallback.
  // Does not require `encryptor_` — only checks whether *something* is
  // stored/set, not its decrypted value.
  bool HasConfiguredToken();
  // Decrypts the persisted token (requires `encryptor_`) or falls back to
  // the env var. Returns an empty string if nothing is available.
  std::string GetEffectiveToken();
  // Encrypts and persists `token` (requires `encryptor_`).
  void PersistToken(const std::string& token);
  void SendTokenStatus();

  void ConsumeEvent(const BitrixSearchSseEvent& event);
  void Fail(std::string kind, std::string message);

  std::unique_ptr<network::SimpleURLLoader> loader_;
  BitrixSearchSseParser parser_;
  bool got_done_ = false;
  // Set once Fail() has fired for the current search; makes failure terminal
  // so a late/malformed event (e.g. a "done" arriving after an "error", or
  // Finish() flushing a dangling block) can't send a success signal on top
  // of an already-reported failure. Reset by CancelSearch().
  bool failed_ = false;
  bool synthesizing_sent_ = false;
  // Kind of the last progress event sent; used to dedupe consecutive
  // "thinking" progress events.
  std::string last_progress_kind_;
  std::string answer_;
  base::ListValue sources_;

  // Query pending from the initial ?q= URL param, consumed once by the
  // first pageReady.
  std::string pending_query_;
  // Mode ("simple"/"deep") for `pending_query_`, already normalized.
  std::string pending_mode_ = "deep";
  // Most recently attempted query (from pageReady or startSearch), used to
  // auto-retry the search once a token is configured via setToken.
  std::string last_query_;
  // Mode ("simple"/"deep") for `last_query_`, used for the same retry.
  std::string last_mode_ = "deep";

  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  std::vector<base::OnceClosure> pending_encryptor_callbacks_;

  // Generation counter for the "current operation". Bumped by
  // setToken/clearToken/cancelSearch so that an encryptor-gated continuation
  // (DoSetToken/DoStartSearchQuery) queued before one of those ran can detect
  // it's stale and no-op instead of resurrecting a cleared token or
  // restarting a cancelled search.
  int operation_id_ = 0;

  base::WeakPtrFactory<BitrixSearchHandler> weak_factory_{this};
};

#endif
