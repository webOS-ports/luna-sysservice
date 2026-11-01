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

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>
#include <glib/gstdio.h>
#include <pbnjson.hpp>

#include "BackupManager.h"
#include "PrefsDb.h"
#include "TempDir.h"
#include "TimeChangeLaunch.h"

// A restored timeChangeLaunch entry is added only for an app that is
// installed. The backup service reinstalls apps after postRestore, so an
// entry whose app is missing waits for restoreFinished, then is dropped.

using namespace pbnjson;

class TimeChangeLaunchInstalledTest : public testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_FALSE(m_dir.path().empty());
		m_apps = {m_dir.file("apps1"), m_dir.file("apps2")};
		for (const auto& d : m_apps)
			ASSERT_EQ(0, g_mkdir(d.c_str(), 0700));
		m_db.reset(PrefsDb::createStandalone(m_dir.file("prefs.db")));
		ASSERT_NE(nullptr, m_db);
	}

	void TearDown() override
	{
		// the singleton outlives the test: leave nothing pending
		BackupManager::instance()->restoreTimeChangeLaunch(*m_db, m_apps, true);
	}

	void install(size_t dir, const std::string& folder, const std::string& id)
	{
		const std::string app = m_apps[dir] + "/" + folder;
		ASSERT_EQ(0, g_mkdir(app.c_str(), 0700));
		const std::string info = "{\"id\":\"" + id + "\",\"main\":\"index.html\"}";
		ASSERT_TRUE(TempDir::write(app + "/appinfo.json", info));
	}

	std::vector<std::string> storedIds()
	{
		JValue list = JDomParser::fromString(m_db->getPref("timeChangeLaunch"))["launchList"];
		const int n = list.isArray() ? static_cast<int>(list.arraySize()) : 0;
		std::vector<std::string> ids(static_cast<size_t>(n));
		for (int i = 0; i < n; ++i)
			ids[static_cast<size_t>(i)] = list[i]["appId"].asString();
		return ids;
	}

	TempDir m_dir;
	std::vector<std::string> m_apps;
	std::unique_ptr<PrefsDb> m_db;
};

TEST_F(TimeChangeLaunchInstalledTest, FindsAnAppInAnyDir)
{
	install(1, "com.a", "com.a");
	EXPECT_TRUE(TimeChangeLaunch::isInstalled("com.a", m_apps));
	EXPECT_FALSE(TimeChangeLaunch::isInstalled("com.b", m_apps));
	EXPECT_FALSE(TimeChangeLaunch::isInstalled("com.a", {}));
}

TEST_F(TimeChangeLaunchInstalledTest, TheAppinfoHasToMatch)
{
	// a folder without appinfo.json, an appinfo.json naming another app,
	// and one that is not json
	ASSERT_EQ(0, g_mkdir((m_apps[0] + "/com.empty").c_str(), 0700));
	install(0, "com.other", "com.someone.else");
	const std::string bad = m_apps[0] + "/com.bad";
	ASSERT_EQ(0, g_mkdir(bad.c_str(), 0700));
	ASSERT_TRUE(TempDir::write(bad + "/appinfo.json", "{not json"));

	EXPECT_FALSE(TimeChangeLaunch::isInstalled("com.empty", m_apps));
	EXPECT_FALSE(TimeChangeLaunch::isInstalled("com.other", m_apps));
	EXPECT_FALSE(TimeChangeLaunch::isInstalled("com.bad", m_apps));
}

TEST_F(TimeChangeLaunchInstalledTest, RefusesSymlinksAndPaths)
{
	install(0, "com.real", "com.real");
	const std::string link = m_apps[1] + "/com.link";
	ASSERT_EQ(0, g_mkdir(link.c_str(), 0700));
	ASSERT_EQ(0, symlink((m_apps[0] + "/com.real/appinfo.json").c_str(), (link + "/appinfo.json").c_str()));
	// the same appinfo.json, but named through a symlink
	ASSERT_TRUE(TempDir::write(m_apps[0] + "/com.real/appinfo.json", R"({"id":"com.link"})"));
	EXPECT_FALSE(TimeChangeLaunch::isInstalled("com.link", m_apps));

	// an appId that walks out of the app dir is not looked up at all
	install(0, "x", "../apps1/x");
	EXPECT_FALSE(TimeChangeLaunch::isInstalled("../apps1/x", {m_apps[1]}));
}

TEST_F(TimeChangeLaunchInstalledTest, AddsTheInstalledReturnsTheRest)
{
	install(0, "com.here", "com.here");
	std::vector<TimeChangeLaunch::Entry> pending = {
		{"com.missing", JObject()}, {"com.here", JObject{{"p", 1}}}};

	auto left = TimeChangeLaunch::addInstalled(*m_db, pending, m_apps);
	ASSERT_EQ(1u, left.size());
	EXPECT_EQ("com.missing", left[0].appId);
	EXPECT_EQ(std::vector<std::string>({"com.here"}), storedIds());
}

// postRestore's filter, then restoreFinished, as BackupManager runs them
TEST_F(TimeChangeLaunchInstalledTest, HeldBackUntilRestoreFinished)
{
	BackupManager* bm = BackupManager::instance();
	install(0, "com.now", "com.now");
	const std::string value = R"({"launchList":[
		{"appId":"com.now","parameters":{}},
		{"appId":"com.later","parameters":{"x":1}},
		{"appId":"com.never","parameters":{}},
		{"appId":"/usr/bin/evil","parameters":{}}]})";

	// never stored as it is
	EXPECT_FALSE(BackupManager::acceptRestoredPref("timeChangeLaunch", value));
	ASSERT_EQ(3u, bm->pendingTimeChangeLaunch().size());

	bm->restoreTimeChangeLaunch(*m_db, m_apps, false);
	EXPECT_EQ(std::vector<std::string>({"com.now"}), storedIds());
	ASSERT_EQ(2u, bm->pendingTimeChangeLaunch().size());

	// the backup service reinstalls com.later, then calls restoreFinished
	install(1, "com.later", "com.later");
	bm->restoreTimeChangeLaunch(*m_db, m_apps, true);
	EXPECT_EQ(std::vector<std::string>({"com.now", "com.later"}), storedIds());
	EXPECT_TRUE(bm->pendingTimeChangeLaunch().empty());

	// com.never is gone for good, even if it turns up later
	install(1, "com.never", "com.never");
	bm->restoreTimeChangeLaunch(*m_db, m_apps, true);
	EXPECT_EQ(std::vector<std::string>({"com.now", "com.later"}), storedIds());
}

TEST_F(TimeChangeLaunchInstalledTest, ANewRestoreReplacesWhatIsHeld)
{
	const BackupManager* bm = BackupManager::instance();
	EXPECT_FALSE(BackupManager::acceptRestoredPref("timeChangeLaunch",
		R"({"launchList":[{"appId":"com.first"}]})"));
	EXPECT_FALSE(BackupManager::acceptRestoredPref("timeChangeLaunch", "garbage"));
	EXPECT_TRUE(bm->pendingTimeChangeLaunch().empty());
}
