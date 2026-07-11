// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NEW_TAB_PAGE_BITRIX24_AUTH_BITRIX24_AUTH_NAVIGATION_THROTTLE_H_
#define CHROME_BROWSER_NEW_TAB_PAGE_BITRIX24_AUTH_BITRIX24_AUTH_NAVIGATION_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"

class Bitrix24AuthNavigationThrottle : public content::NavigationThrottle {
 public:
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);
  explicit Bitrix24AuthNavigationThrottle(
      content::NavigationThrottleRegistry& registry);
  ~Bitrix24AuthNavigationThrottle() override;

  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  ThrottleCheckResult WillProcessResponse() override;
  const char* GetNameForLogging() override;

 private:
  ThrottleCheckResult MaybeCapture();
};

#endif
