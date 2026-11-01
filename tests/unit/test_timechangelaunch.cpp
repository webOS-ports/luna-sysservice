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

#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <pbnjson.hpp>

#include "PrefsDb.h"
#include "TempDir.h"
#include "TimeChangeLaunch.h"

// timeChangeLaunch names apps launched on every time change. A backup can
// carry anything, so a restored value is taken apart entry by entry and
// only entries setTimeChangeLaunch could have stored survive.

using namespace pbnjson;

TEST(TimeChangeLaunchAppId, AcceptsAppIds)
{
	EXPECT_TRUE(TimeChangeLaunch::isValidAppId("com.palm.app.clock"));
	EXPECT_TRUE(TimeChangeLaunch::isValidAppId("org.webosports.app.x-y_z+1"));
	EXPECT_TRUE(TimeChangeLaunch::isValidAppId("A"));
	EXPECT_TRUE(TimeChangeLaunch::isValidAppId("9lives"));
	EXPECT_TRUE(TimeChangeLaunch::isValidAppId(std::string(255, 'a')));
}

TEST(TimeChangeLaunchAppId, RefusesEverythingElse)
{
	EXPECT_FALSE(TimeChangeLaunch::isValidAppId(""));
	EXPECT_FALSE(TimeChangeLaunch::isValidAppId(std::string(256, 'a')));
	EXPECT_FALSE(TimeChangeLaunch::isValidAppId(".hidden"));
	EXPECT_FALSE(TimeChangeLaunch::isValidAppId("-rf"));
	EXPECT_FALSE(TimeChangeLaunch::isValidAppId("_x"));
	EXPECT_FALSE(TimeChangeLaunch::isValidAppId("../etc"));
	EXPECT_FALSE(TimeChangeLaunch::isValidAppId("com.x/../../y"));
	EXPECT_FALSE(TimeChangeLaunch::isValidAppId("com x"));
	EXPECT_FALSE(TimeChangeLaunch::isValidAppId("com.x;reboot"));
	EXPECT_FALSE(TimeChangeLaunch::isValidAppId(std::string("com.x\0y", 7)));
	EXPECT_FALSE(TimeChangeLaunch::isValidAppId("com.\xc3\xa9"));
}

TEST(TimeChangeLaunchParse, TakesTheStoredShape)
{
	auto entries = TimeChangeLaunch::parse(
		R"({"launchList":[{"appId":"com.a","parameters":{"k":1}},{"appId":"com.b","parameters":{}}]})");
	ASSERT_EQ(2u, entries.size());
	EXPECT_EQ("com.a", entries[0].appId);
	EXPECT_EQ(1, entries[0].parameters["k"].asNumber<int>());
	EXPECT_EQ("com.b", entries[1].appId);
	EXPECT_TRUE(entries[1].parameters.isObject());
}

TEST(TimeChangeLaunchParse, NothingFromTheWrongShape)
{
	EXPECT_TRUE(TimeChangeLaunch::parse("").empty());
	EXPECT_TRUE(TimeChangeLaunch::parse("not json").empty());
	EXPECT_TRUE(TimeChangeLaunch::parse("[]").empty());
	EXPECT_TRUE(TimeChangeLaunch::parse(R"({"launchList":{"appId":"com.a"}})").empty());
	EXPECT_TRUE(TimeChangeLaunch::parse(R"({"other":[{"appId":"com.a"}]})").empty());
}

TEST(TimeChangeLaunchParse, DropsBadEntriesOneByOne)
{
	auto entries = TimeChangeLaunch::parse(R"({"launchList":[
		"com.string",
		{"appid":"com.wrongcase"},
		{"appId":7},
		{"appId":"../../bin/sh"},
		{"appId":"com.badparams","parameters":"--x"},
		{"appId":"com.arrayparams","parameters":[]},
		{"appId":"com.good","parameters":{"a":"b"}},
		{"appId":"com.good","parameters":{"a":"second"}},
		{"appId":"com.noparams"}
	]})");
	ASSERT_EQ(2u, entries.size());
	EXPECT_EQ("com.good", entries[0].appId);
	// the first entry for an appId wins, as add() keeps one per appId
	EXPECT_EQ("b", entries[0].parameters["a"].asString());
	EXPECT_EQ("com.noparams", entries[1].appId);
	// missing parameters means {}, as launchAppsOnTimeChange treats it
	EXPECT_TRUE(entries[1].parameters.isObject());
	EXPECT_EQ(0u, entries[1].parameters.objectSize());
}

class TimeChangeLaunchAddTest : public testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_FALSE(m_dir.path().empty());
		m_db.reset(PrefsDb::createStandalone(m_dir.file("prefs.db")));
		ASSERT_NE(nullptr, m_db);
	}

	JValue stored() { return JDomParser::fromString(m_db->getPref("timeChangeLaunch")); }

	TempDir m_dir;
	std::unique_ptr<PrefsDb> m_db;
};

TEST_F(TimeChangeLaunchAddTest, StartsTheList)
{
	ASSERT_TRUE(TimeChangeLaunch::add(*m_db, "com.a", JObject{{"k", "v"}}));
	JValue list = stored()["launchList"];
	ASSERT_TRUE(list.isArray());
	ASSERT_EQ(1, list.arraySize());
	EXPECT_EQ("com.a", list[0]["appId"].asString());
	EXPECT_EQ("v", list[0]["parameters"]["k"].asString());
}

TEST_F(TimeChangeLaunchAddTest, AppendsAndReplaces)
{
	ASSERT_TRUE(TimeChangeLaunch::add(*m_db, "com.a", JObject{{"n", 1}}));
	ASSERT_TRUE(TimeChangeLaunch::add(*m_db, "com.b", JObject{{"n", 2}}));
	ASSERT_TRUE(TimeChangeLaunch::add(*m_db, "com.a", JObject{{"n", 3}}));
	JValue list = stored()["launchList"];
	ASSERT_EQ(2, list.arraySize());
	EXPECT_EQ("com.a", list[0]["appId"].asString());
	EXPECT_EQ(3, list[0]["parameters"]["n"].asNumber<int>());
	EXPECT_EQ("com.b", list[1]["appId"].asString());
	EXPECT_EQ(2, list[1]["parameters"]["n"].asNumber<int>());
}

TEST_F(TimeChangeLaunchAddTest, KeepsWhatIsStored)
{
	ASSERT_TRUE(m_db->setPref("timeChangeLaunch",
	                          R"({"launchList":[{"appId":"com.live","parameters":{}}]})"));
	ASSERT_TRUE(TimeChangeLaunch::add(*m_db, "com.restored", JObject()));
	JValue list = stored()["launchList"];
	ASSERT_EQ(2, list.arraySize());
	EXPECT_EQ("com.live", list[0]["appId"].asString());
	EXPECT_EQ("com.restored", list[1]["appId"].asString());
}
