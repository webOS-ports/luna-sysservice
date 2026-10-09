// Copyright (c) 2010-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0


#ifndef LOCALEPREFSHANDLER_H
#define LOCALEPREFSHANDLER_H

#include <pbnjson.hpp>

#include "PrefsHandler.h"

class LocalePrefsHandler : public PrefsHandler
{
public:

	explicit LocalePrefsHandler(LSHandle* serviceHandle);
	~LocalePrefsHandler() override;

	std::list<std::string> keys() const override;
	bool validate(const std::string& key, const pbnjson::JValue &value) override;
	void valueChanged(const std::string& key, const pbnjson::JValue &value) override;
	pbnjson::JValue valuesForKey(const std::string& key) override;

private:

	void init();
	void readLocaleFile();
	void readRegionFile();
	
	bool validateLocale(const pbnjson::JValue &value) const;
	bool validateRegion(const pbnjson::JValue &value) const;
	pbnjson::JValue valuesForLocale() const;
	pbnjson::JValue valuesForRegion() const;
		
private:

	typedef std::pair<std::string, std::string> NameCodePair;
	typedef std::string						NameCodeTriplet[3];
	typedef std::list<NameCodePair> NameCodePairList;

	struct LocaleEntry {
		NameCodePair language;
		NameCodePairList countries;
	};

	struct RegionEntry {
		NameCodeTriplet region;
	};
	
	typedef std::list<LocaleEntry> LocaleEntryList;
	typedef std::list<RegionEntry> RegionEntryList;
	
	LocaleEntryList m_localeEntryList;
	RegionEntryList m_regionEntryList;
};

#endif /* LOCALEPREFSHANDLER_H */
