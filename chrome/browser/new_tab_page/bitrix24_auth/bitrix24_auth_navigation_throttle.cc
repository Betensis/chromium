// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/new_tab_page/bitrix24_auth/bitrix24_auth_navigation_throttle.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/new_tab_page/bitrix24_auth/bitrix24_auth_service.h"
#include "chrome/browser/new_tab_page/bitrix24_auth/bitrix24_auth_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"

void Bitrix24AuthNavigationThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  if (!handle.IsInMainFrame()) {
    return;
  }
  auto* web_contents = handle.GetWebContents();
  auto* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  Bitrix24AuthService* service =
      Bitrix24AuthServiceFactory::GetForProfile(profile);
  if (service && service->IsExpectedCallback(handle.GetURL())) {
    registry.AddThrottle(
        std::make_unique<Bitrix24AuthNavigationThrottle>(registry));
  }
}

Bitrix24AuthNavigationThrottle::Bitrix24AuthNavigationThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}
Bitrix24AuthNavigationThrottle::~Bitrix24AuthNavigationThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
Bitrix24AuthNavigationThrottle::WillStartRequest() {
  return MaybeCapture();
}

content::NavigationThrottle::ThrottleCheckResult
Bitrix24AuthNavigationThrottle::WillRedirectRequest() {
  return MaybeCapture();
}

content::NavigationThrottle::ThrottleCheckResult
Bitrix24AuthNavigationThrottle::WillProcessResponse() {
  return MaybeCapture();
}

const char* Bitrix24AuthNavigationThrottle::GetNameForLogging() {
  return "Bitrix24AuthNavigationThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
Bitrix24AuthNavigationThrottle::MaybeCapture() {
  auto* web_contents = navigation_handle()->GetWebContents();
  auto* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  Bitrix24AuthService* service =
      Bitrix24AuthServiceFactory::GetForProfile(profile);
  if (!service ||
      !service->HandleOAuthCallback(navigation_handle()->GetURL())) {
    return PROCEED;
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&content::WebContents::ClosePage,
                                web_contents->GetWeakPtr()));
  return CANCEL_AND_IGNORE;
}
