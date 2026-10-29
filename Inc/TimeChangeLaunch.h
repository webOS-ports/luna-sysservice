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

#ifndef TIME_CHANGE_LAUNCH_H
#define TIME_CHANGE_LAUNCH_H

#include <string>
#include <vector>

#include <pbnjson.hpp>

class PrefsDb;

/*
 * The "timeChangeLaunch" preference: apps launched, with their parameters,
 * whenever the system time changes. Stored as
 *
 *   {"launchList":[{"appId":"<id>","parameters":{...}}, ...]}
 *
 * and written only by /time/setTimeChangeLaunch and by a backup restore.
 */
namespace TimeChangeLaunch {

struct Entry
{
	std::string appId;
	pbnjson::JValue parameters;	///< always a json object
};

/// Letters, digits, '.', '_', '+', '-', starting with a letter or digit,
/// at most 255 characters: the app ids found on LuneOS, never a '/', a
/// leading '.' or '-', or a shell metacharacter.
bool isValidAppId(const std::string& appId);

/// The entries of a stored (or backed-up) value that have the shape
/// setTimeChangeLaunch stores: a "launchList" array of objects, each with a
/// string "appId" that isValidAppId() and an object "parameters" (missing
/// means {}). Other entries are dropped one by one, with a log line; a later
/// entry for an appId already seen is dropped too. Nothing comes back when
/// the value is not an object with a launchList array.
std::vector<Entry> parse(const std::string& stored);

/// The directories apps are installed in (<dir>/<appId>/appinfo.json): the
/// ones SAM (/etc/palm/sam-conf.json) and luna-appmanager (luna.conf
/// ApplicationPath, AppInstallBase) scan on LuneOS.
const std::vector<std::string>& defaultAppDirs();

/// Whether appId is installed: <dir>/<appId>/appinfo.json is a regular file
/// (not a symlink) in one of appDirs, and its "id" is appId.
bool isInstalled(const std::string& appId, const std::vector<std::string>& appDirs);

/// Adds appId with parameters to the list in db, or replaces the parameters
/// of its entry - what setTimeChangeLaunch does for "active": true.
bool add(PrefsDb& db, const std::string& appId, const pbnjson::JValue& parameters);

/// Adds each pending entry whose app is installed, through add(). Returns
/// the entries whose app is not (yet) installed.
std::vector<Entry> addInstalled(PrefsDb& db, const std::vector<Entry>& pending,
                                const std::vector<std::string>& appDirs);

} // namespace TimeChangeLaunch

#endif // TIME_CHANGE_LAUNCH_H
