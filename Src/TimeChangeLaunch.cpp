// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

#include "TimeChangeLaunch.h"

#include <algorithm>
#include <set>

#include <sys/stat.h>

#include "Logging.h"
#include "PrefsDb.h"

using namespace pbnjson;

namespace TimeChangeLaunch {

static const char s_key[] = "timeChangeLaunch";

bool isValidAppId(const std::string& appId)
{
	if (appId.empty() || appId.size() > 255)
		return false;
	for (std::string::size_type i = 0; i < appId.size(); ++i) {
		const char c = appId[i];
		// ASCII only, whatever the locale
		const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if (i == 0 ? !alnum : !(alnum || c == '.' || c == '_' || c == '+' || c == '-'))
			return false;
	}
	return true;
}

std::vector<Entry> parse(const std::string& stored)
{
	std::vector<Entry> entries;

	JValue root = JDomParser::fromString(stored);
	if (!root.isObject() || !root["launchList"].isArray()) {
		PmLogWarning(sysServiceLogContext(), "TIME_CHANGE_LAUNCH_INVALID", 0,
		             "timeChangeLaunch is not an object with a launchList array; nothing taken from it");
		return entries;
	}

	std::set<std::string> seen;
	int index = -1;
	for (const JValue item : root["launchList"].items()) {
		++index;
		if (!item.isObject() || !item["appId"].isString()) {
			PmLogWarning(sysServiceLogContext(), "TIME_CHANGE_LAUNCH_ENTRY_DROPPED", 0,
			             "launchList entry %d has no string appId; dropped", index);
			continue;
		}
		const std::string appId = item["appId"].asString();
		if (!isValidAppId(appId)) {
			PmLogWarning(sysServiceLogContext(), "TIME_CHANGE_LAUNCH_ENTRY_DROPPED", 0,
			             "launchList entry %d has an invalid appId; dropped", index);
			continue;
		}
		JValue parameters = item["parameters"];
		if (!parameters.isValid())
			parameters = JObject();
		if (!parameters.isObject()) {
			PmLogWarning(sysServiceLogContext(), "TIME_CHANGE_LAUNCH_ENTRY_DROPPED", 0,
			             "launchList entry %d (%s): parameters is not an object; dropped", index, appId.c_str());
			continue;
		}
		if (!seen.insert(appId).second) {
			PmLogWarning(sysServiceLogContext(), "TIME_CHANGE_LAUNCH_ENTRY_DROPPED", 0,
			             "launchList entry %d (%s) repeats an appId; dropped", index, appId.c_str());
			continue;
		}
		entries.push_back(Entry{appId, parameters});
	}
	return entries;
}

const std::vector<std::string>& defaultAppDirs()
{
	static const std::vector<std::string> s_dirs = {
		// luna.conf ApplicationPath (luna-sysmgr) and AppInstallBase/Relative
		"/usr/palm/applications",
		"/var/usr/palm/applications",
		"/media/cryptofs/apps/usr/palm/applications",
		"/usr/lib/luna/applications",
		"/var/luna/applications",
		"/var/usr/lib/luna/applications",
		// sam-conf.json ApplicationPaths not listed above
		"/media/system/apps/usr/palm/applications",
		"/media/developer/apps/usr/palm/applications",
		"/mnt/otycabi/usr/palm/applications",
		"/mnt/otncabi/usr/palm/applications",
	};
	return s_dirs;
}

bool isInstalled(const std::string& appId, const std::vector<std::string>& appDirs)
{
	if (!isValidAppId(appId))
		return false;

	return std::any_of(appDirs.begin(), appDirs.end(), [&appId](const std::string& dir) {
		std::string appinfo = dir;
		appinfo += '/';
		appinfo += appId;
		appinfo += "/appinfo.json";
		struct stat st;
		if (lstat(appinfo.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
			return false;
		JValue info = JDomParser::fromFile(appinfo.c_str());
		return info.isObject() && info["id"].isString() && info["id"].asString() == appId;
	});
}

bool add(PrefsDb& db, const std::string& appId, const JValue& parameters)
{
	//get the currently stored list of launch apps
	JValue storedJson = JDomParser::fromString(db.getPref(s_key));
	if (!storedJson.isValid()) {
		storedJson = JObject();
	}

	//get the launchList array object out of it
	JValue storedJson_listArray = storedJson["launchList"];
	if (!storedJson_listArray.isValid()) {
		//nothing in the list yet
		storedJson_listArray = JArray();
		storedJson.put("launchList", storedJson_listArray);
	}

	//go through the array and try and find the appid (if it's in there already)
	JValue storedJson_listObject;
	for (const JValue key: storedJson_listArray.items()) {
		JValue label = key["appId"];
		if (!label.isString()) {
			continue; //something really bad happened; something was stored in the list w/o an appId!
		}
		if (appId == label.asString()) {
			storedJson_listObject = key;
			break;
		}
	}

	if (storedJson_listObject.isValid() && !storedJson_listObject.isNull()) {
		//found it...
		storedJson_listObject.put("parameters", parameters);
	}
	else {
		//create a new object, populate, and add it to the array
		storedJson_listArray.append(JObject {{"appId", appId}, {"parameters", parameters}});
	}

	//store the pref back, in string form
	return db.setPref(s_key, storedJson.stringify());
}

std::vector<Entry> addInstalled(PrefsDb& db, const std::vector<Entry>& pending,
                                const std::vector<std::string>& appDirs)
{
	std::vector<Entry> notInstalled;
	for (const Entry& entry : pending) {
		if (!isInstalled(entry.appId, appDirs)) {
			notInstalled.push_back(entry);
			continue;
		}
		if (add(db, entry.appId, entry.parameters))
			PmLogInfo(sysServiceLogContext(), "TIME_CHANGE_LAUNCH_RESTORED", 0,
			          "restored timeChangeLaunch entry for %s", entry.appId.c_str());
	}
	return notInstalled;
}

} // namespace TimeChangeLaunch
