// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/new_tab_page/bitrix24_auth/bitrix24_auth_service_factory.h"

#include <memory>

#include "chrome/browser/new_tab_page/bitrix24_auth/bitrix24_auth_service.h"
#include "chrome/browser/profiles/profile.h"
#include "components/pref_registry/pref_registry_syncable.h"

Bitrix24AuthService* Bitrix24AuthServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<Bitrix24AuthService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

Bitrix24AuthServiceFactory* Bitrix24AuthServiceFactory::GetInstance() {
  static base::NoDestructor<Bitrix24AuthServiceFactory> instance;
  return instance.get();
}

Bitrix24AuthServiceFactory::Bitrix24AuthServiceFactory()
    : ProfileKeyedServiceFactory("Bitrix24AuthService",
                                 ProfileSelections::BuildForRegularProfile()) {}
Bitrix24AuthServiceFactory::~Bitrix24AuthServiceFactory() = default;

std::unique_ptr<KeyedService>
Bitrix24AuthServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<Bitrix24AuthService>(
      Profile::FromBrowserContext(context));
}

void Bitrix24AuthServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  Bitrix24AuthService::RegisterProfilePrefs(registry);
}
