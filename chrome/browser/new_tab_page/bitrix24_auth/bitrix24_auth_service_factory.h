// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NEW_TAB_PAGE_BITRIX24_AUTH_BITRIX24_AUTH_SERVICE_FACTORY_H_
#define CHROME_BROWSER_NEW_TAB_PAGE_BITRIX24_AUTH_BITRIX24_AUTH_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Bitrix24AuthService;
class Profile;

class Bitrix24AuthServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static Bitrix24AuthService* GetForProfile(Profile* profile);
  static Bitrix24AuthServiceFactory* GetInstance();

 private:
  friend base::NoDestructor<Bitrix24AuthServiceFactory>;
  Bitrix24AuthServiceFactory();
  ~Bitrix24AuthServiceFactory() override;
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) override;
};

#endif
