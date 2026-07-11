// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/new_tab_page/bitrix24_auth/bitrix24_auth_service.h"

#include <string>
#include <utility>

#include "base/base64.h"
#include "base/base64url.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/rand_util.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "crypto/sha2.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace {

constexpr char kAuthorizeUrl[] = "http://network.kiselev.bx/oauth/authorize/";
constexpr char kTokenUrl[] = "http://network.kiselev.bx/oauth/token/";
constexpr char kClientId[] = "browser.bitrix24.chromium";
constexpr char kRedirectUri[] =
    "https://browser-auth.bitrix24.com/oauth/callback";
constexpr char kScopes[] = "auth profile";

constexpr char kRefreshTokenPref[] = "bitrix24.auth.encrypted_refresh_token";
constexpr char kPortalOriginPref[] = "bitrix24.auth.portal_origin";
constexpr char kPortalUserIdPref[] = "bitrix24.auth.portal_user_id";
constexpr char kProfileNamePref[] = "bitrix24.auth.profile_name";
constexpr char kProfileAvatarUrlPref[] = "bitrix24.auth.profile_avatar_url";

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("bitrix24_network_oauth", R"(
      semantics {
        sender: "Bitrix24 Network authentication"
        description:
          "Exchanges a user-approved OAuth authorization code for tokens so "
          "the Bitrix24 new tab integration can access the selected cloud portal."
        trigger: "The user clicks Sign in to Bitrix24 on the New Tab Page."
        data: "OAuth code, PKCE verifier, redirect URI and public client ID."
        destination: OTHER
        destination_other: "Bitrix24 Network"
      }
      policy {
        cookies_allowed: NO
        setting: "The user can sign out from the New Tab Page."
        policy_exception_justification: "Not implemented for this first-party integration."
      })");

std::string MakeBase64Url(std::string_view bytes) {
  std::string encoded;
  base::Base64UrlEncode(bytes, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &encoded);
  return encoded;
}

bool IsAllowedPortalUrl(const GURL& url) {
  if (!url.is_valid() || url.has_username() || url.has_password()) {
    return false;
  }
  if (url.SchemeIs("https")) {
    return true;
  }
  return url.SchemeIs("http") &&
         (url.DomainIs("bx") || url.host() == "localhost" ||
          url.host() == "127.0.0.1");
}

}  // namespace

Bitrix24AuthService::Bitrix24AuthService(Profile* profile)
    : profile_(profile), url_loader_factory_(profile->GetURLLoaderFactory()) {
  portal_origin_ = profile_->GetPrefs()->GetString(kPortalOriginPref);
  profile_name_ = profile_->GetPrefs()->GetString(kProfileNamePref);
  profile_avatar_url_ = profile_->GetPrefs()->GetString(kProfileAvatarUrlPref);
  g_browser_process->os_crypt_async()->GetInstance(base::BindOnce(
      &Bitrix24AuthService::OnOsCryptReady, weak_factory_.GetWeakPtr()));
}

Bitrix24AuthService::~Bitrix24AuthService() = default;

// static
void Bitrix24AuthService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kRefreshTokenPref, std::string());
  registry->RegisterStringPref(kPortalOriginPref, std::string());
  registry->RegisterStringPref(kPortalUserIdPref, std::string());
  registry->RegisterStringPref(kProfileNamePref, std::string());
  registry->RegisterStringPref(kProfileAvatarUrlPref, std::string());
}

void Bitrix24AuthService::StartLogin(
    base::WeakPtr<content::WebContents> source_tab) {
  login_source_tab_ = std::move(source_tab);
  token_loader_.reset();
  access_token_.clear();
  refresh_token_.clear();
  pending_refresh_token_.clear();
  state_nonce_ = MakeBase64Url(base::RandBytesAsString(32));
  pkce_verifier_ = MakeBase64Url(base::RandBytesAsString(32));
  const std::string challenge =
      MakeBase64Url(crypto::SHA256HashString(pkce_verifier_));

  GURL authorize_url(kAuthorizeUrl);
  authorize_url =
      net::AppendQueryParameter(authorize_url, "client_id", kClientId);
  authorize_url =
      net::AppendQueryParameter(authorize_url, "redirect_uri", kRedirectUri);
  authorize_url =
      net::AppendQueryParameter(authorize_url, "response_type", "code");
  authorize_url = net::AppendQueryParameter(authorize_url, "scope", kScopes);
  authorize_url =
      net::AppendQueryParameter(authorize_url, "state", state_nonce_);
  authorize_url =
      net::AppendQueryParameter(authorize_url, "code_challenge", challenge);
  authorize_url =
      net::AppendQueryParameter(authorize_url, "code_challenge_method", "S256");

  state_ = State::kAuthorizing;
  NavigateParams params(profile_, authorize_url, ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_POPUP;
  Navigate(&params);
}

void Bitrix24AuthService::SignOut() {
  token_loader_.reset();
  state_nonce_.clear();
  pkce_verifier_.clear();
  access_token_.clear();
  refresh_token_.clear();
  pending_refresh_token_.clear();
  portal_origin_.clear();
  profile_name_.clear();
  profile_avatar_url_.clear();
  profile_->GetPrefs()->ClearPref(kRefreshTokenPref);
  profile_->GetPrefs()->ClearPref(kPortalOriginPref);
  profile_->GetPrefs()->ClearPref(kPortalUserIdPref);
  profile_->GetPrefs()->ClearPref(kProfileNamePref);
  profile_->GetPrefs()->ClearPref(kProfileAvatarUrlPref);
  state_ = State::kSignedOut;
}

bool Bitrix24AuthService::IsExpectedCallback(const GURL& url) const {
  const GURL callback(kRedirectUri);
  return state_ == State::kAuthorizing && url.SchemeIs("https") &&
         url.DeprecatedGetOriginAsURL() ==
             callback.DeprecatedGetOriginAsURL() &&
         url.path() == callback.path();
}

bool Bitrix24AuthService::HandleOAuthCallback(const GURL& url) {
  if (!IsExpectedCallback(url)) {
    return false;
  }

  std::string returned_state;
  if (!net::GetValueForKeyInQuery(url, "state", &returned_state) ||
      returned_state != state_nonce_) {
    return false;
  }

  std::string error;
  if (net::GetValueForKeyInQuery(url, "error", &error)) {
    SetError();
    return true;
  }

  std::string code;
  if (!net::GetValueForKeyInQuery(url, "code", &code) || code.empty()) {
    SetError();
    return true;
  }

  state_nonce_.clear();
  ExchangeCode(std::move(code));
  return true;
}

void Bitrix24AuthService::ExchangeCode(std::string code) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(kTokenUrl);
  request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->headers.SetHeader("Content-Type",
                             "application/x-www-form-urlencoded");

  const std::string body = base::StrCat(
      {"grant_type=authorization_code&client_id=",
       base::EscapeQueryParamValue(kClientId, true),
       "&code=", base::EscapeQueryParamValue(code, true),
       "&redirect_uri=", base::EscapeQueryParamValue(kRedirectUri, true),
       "&code_verifier=", base::EscapeQueryParamValue(pkce_verifier_, true)});
  pkce_verifier_.clear();

  token_loader_ =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  token_loader_->AttachStringForUpload(body,
                                       "application/x-www-form-urlencoded");
  token_loader_->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&Bitrix24AuthService::OnTokenResponse,
                     weak_factory_.GetWeakPtr()),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void Bitrix24AuthService::OnTokenResponse(
    std::optional<std::string> response_body) {
  const auto* response_info =
      token_loader_ ? token_loader_->ResponseInfo() : nullptr;
  const bool http_ok = response_info && response_info->headers &&
                       response_info->headers->response_code() == net::HTTP_OK;
  token_loader_.reset();
  if (!http_ok || !response_body) {
    SetError();
    return;
  }

  std::optional<base::DictValue> response =
      base::JSONReader::ReadDict(*response_body, base::JSON_PARSE_RFC);
  if (!response) {
    SetError();
    return;
  }
  const std::string* access_token = response->FindString("access_token");
  const std::string* refresh_token = response->FindString("refresh_token");
  const std::string* portal_origin = response->FindString("portal_origin");
  if (!access_token || access_token->empty() || !refresh_token ||
      refresh_token->empty() || !portal_origin) {
    SetError();
    return;
  }

  const GURL portal_url(*portal_origin);
  if (!IsAllowedPortalUrl(portal_url)) {
    SetError();
    return;
  }

  access_token_ = *access_token;
  portal_origin_ = portal_url.DeprecatedGetOriginAsURL().spec();
  profile_->GetPrefs()->SetString(kPortalOriginPref, portal_origin_);
  if (const std::string* profile_name = response->FindString("profile_name")) {
    profile_name_ = *profile_name;
    profile_->GetPrefs()->SetString(kProfileNamePref, profile_name_);
  }
  if (const std::string* profile_avatar_url =
          response->FindString("profile_avatar_url")) {
    const GURL avatar_url(*profile_avatar_url);
    if (profile_avatar_url->empty() || IsAllowedPortalUrl(avatar_url)) {
      profile_avatar_url_ = *profile_avatar_url;
      profile_->GetPrefs()->SetString(kProfileAvatarUrlPref,
                                      profile_avatar_url_);
    }
  }
  if (const std::string* portal_user_id =
          response->FindString("portal_user_id")) {
    profile_->GetPrefs()->SetString(kPortalUserIdPref, *portal_user_id);
  } else if (std::optional<int> numeric_portal_user_id =
                 response->FindInt("portal_user_id")) {
    profile_->GetPrefs()->SetString(
        kPortalUserIdPref, base::NumberToString(*numeric_portal_user_id));
  }
  SaveRefreshToken(*refresh_token);
}

void Bitrix24AuthService::SaveRefreshToken(const std::string& refresh_token) {
  if (!encryptor_) {
    pending_refresh_token_ = refresh_token;
    return;
  }
  std::string encrypted;
  if (!encryptor_->EncryptString(refresh_token, &encrypted)) {
    SetError();
    return;
  }
  refresh_token_ = refresh_token;
  profile_->GetPrefs()->SetString(kRefreshTokenPref,
                                  base::Base64Encode(encrypted));
  state_ = State::kSignedIn;
  OpenAndPinPortalTab();
}

void Bitrix24AuthService::OpenAndPinPortalTab() {
  const GURL portal_url(portal_origin_);
  if (!IsAllowedPortalUrl(portal_url)) {
    return;
  }
  const GURL portal_auth_url =
      portal_url.Resolve("/?auth_service_id=Bitrix24Net&backurl=%2F");
  if (!portal_auth_url.is_valid()) {
    return;
  }

  GlobalBrowserCollection* browsers = GlobalBrowserCollection::GetInstance();
  BrowserWindowInterface* target_window = nullptr;
  content::WebContents* target_tab = nullptr;

  browsers->ForEach(
      [&](BrowserWindowInterface* window) {
        Browser* browser = window->GetBrowserForMigrationOnly();
        if (!browser || !browser->is_type_normal() ||
            window->GetProfile()->GetOriginalProfile() != profile_) {
          return true;
        }
        if (!target_window) {
          target_window = window;
        }
        TabStripModel* tabs = window->GetTabStripModel();
        for (int i = 0; i < tabs->count(); ++i) {
          content::WebContents* contents = tabs->GetWebContentsAt(i);
          if (contents->GetLastCommittedURL().DeprecatedGetOriginAsURL() ==
              portal_url.DeprecatedGetOriginAsURL()) {
            target_window = window;
            target_tab = contents;
            return false;
          }
        }
        return true;
      },
      BrowserCollection::Order::kCreation);

  if (!target_tab && login_source_tab_) {
    BrowserWindowInterface* source_window =
        browsers->FindBrowserWithTab(login_source_tab_.get());
    Browser* source_browser =
        source_window ? source_window->GetBrowserForMigrationOnly() : nullptr;
    if (source_browser && source_browser->is_type_normal() &&
        source_window->GetProfile()->GetOriginalProfile() == profile_) {
      target_window = source_window;
      target_tab = login_source_tab_.get();
    }
  }
  login_source_tab_.reset();

  if (target_tab && target_window) {
    TabStripModel* tabs = target_window->GetTabStripModel();
    int index = tabs->GetIndexOfWebContents(target_tab);
    if (index != TabStripModel::kNoTab) {
      index = tabs->SetTabPinned(index, true);
      index = tabs->MoveWebContentsAt(index, 0, true);
      tabs->ActivateTabAt(index);

      NavigateParams params(target_window, portal_auth_url,
                            ui::PAGE_TRANSITION_LINK);
      params.disposition = WindowOpenDisposition::CURRENT_TAB;
      params.source_contents = target_tab;
      Navigate(&params);
      return;
    }
  }

  if (target_window) {
    NavigateParams params(target_window, portal_auth_url,
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    params.tabstrip_index = 0;
    params.tabstrip_add_types |= AddTabTypes::ADD_PINNED;
    Navigate(&params);
  }
}

void Bitrix24AuthService::OnOsCryptReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  encryptor_ = std::move(encryptor);
  if (!pending_refresh_token_.empty()) {
    std::string token = std::move(pending_refresh_token_);
    pending_refresh_token_.clear();
    SaveRefreshToken(token);
    return;
  }

  const std::string encoded =
      profile_->GetPrefs()->GetString(kRefreshTokenPref);
  std::string encrypted;
  if (encoded.empty() || portal_origin_.empty() ||
      !base::Base64Decode(encoded, &encrypted)) {
    return;
  }
  if (encryptor_->DecryptString(encrypted, &refresh_token_) &&
      !refresh_token_.empty()) {
    state_ = State::kSignedIn;
  } else {
    SignOut();
  }
}

void Bitrix24AuthService::SetError() {
  state_nonce_.clear();
  pkce_verifier_.clear();
  access_token_.clear();
  state_ = State::kError;
}

std::string Bitrix24AuthService::GetStateString() const {
  switch (state_) {
    case State::kSignedOut:
      return "signed_out";
    case State::kAuthorizing:
      return "authorizing";
    case State::kSignedIn:
      return "signed_in";
    case State::kError:
      return "error";
  }
}
