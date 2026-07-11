// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NEW_TAB_PAGE_BITRIX24_AUTH_BITRIX24_AUTH_SERVICE_H_
#define CHROME_BROWSER_NEW_TAB_PAGE_BITRIX24_AUTH_BITRIX24_AUTH_SERVICE_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "components/keyed_service/core/keyed_service.h"

class GURL;
class PrefRegistrySimple;
class Profile;

namespace content {
class WebContents;
}

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace os_crypt_async {
class Encryptor;
}

// Owns the cloud-only Bitrix24 Network OAuth session for a regular profile.
// Secrets and all network operations stay in the browser process.
class Bitrix24AuthService : public KeyedService {
 public:
  enum class State { kSignedOut, kAuthorizing, kSignedIn, kError };

  explicit Bitrix24AuthService(Profile* profile);
  ~Bitrix24AuthService() override;
  Bitrix24AuthService(const Bitrix24AuthService&) = delete;
  Bitrix24AuthService& operator=(const Bitrix24AuthService&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void StartLogin(base::WeakPtr<content::WebContents> source_tab);
  void SignOut();
  bool HandleOAuthCallback(const GURL& url);
  bool IsExpectedCallback(const GURL& url) const;

  State state() const { return state_; }
  std::string GetStateString() const;
  const std::string& portal_origin() const { return portal_origin_; }
  const std::string& profile_name() const { return profile_name_; }
  const std::string& profile_avatar_url() const { return profile_avatar_url_; }

 private:
  void OnOsCryptReady(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  void ExchangeCode(std::string code);
  void OnTokenResponse(std::optional<std::string> response_body);
  void SaveRefreshToken(const std::string& refresh_token);
  void OpenAndPinPortalTab();
  void SetError();

  const raw_ptr<Profile> profile_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  std::unique_ptr<network::SimpleURLLoader> token_loader_;
  State state_ = State::kSignedOut;
  std::string state_nonce_;
  std::string pkce_verifier_;
  std::string access_token_;
  std::string refresh_token_;
  std::string pending_refresh_token_;
  std::string portal_origin_;
  std::string profile_name_;
  std::string profile_avatar_url_;
  base::WeakPtr<content::WebContents> login_source_tab_;
  base::WeakPtrFactory<Bitrix24AuthService> weak_factory_{this};
};

#endif  // CHROME_BROWSER_NEW_TAB_PAGE_BITRIX24_AUTH_BITRIX24_AUTH_SERVICE_H_
