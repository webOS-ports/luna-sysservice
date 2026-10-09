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

#include <list>
#include <map>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "PrefsDb.h"
#include "TempDir.h"

// Keys and values reach PrefsDb straight from setPreferences/getPreferences
// on the bus, so they are hostile until proven otherwise.

static const std::string kInjection = "x'); DROP TABLE Preferences; --";
static const std::string kQuoted = "it's \"quoted\" \\ and %s %n";

class PrefsDbTest : public testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_FALSE(m_dir.path().empty());
		m_db.reset(PrefsDb::createStandalone(m_dir.file("prefs.db")));
		ASSERT_NE(nullptr, m_db);
	}

	TempDir m_dir;
	std::unique_ptr<PrefsDb> m_db;
};

TEST_F(PrefsDbTest, StoresAndReadsBack)
{
	EXPECT_TRUE(m_db->setPref("ringtone", "{\"fullPath\":\"/a.mp3\"}"));
	EXPECT_EQ("{\"fullPath\":\"/a.mp3\"}", m_db->getPref("ringtone"));

	std::string v;
	EXPECT_TRUE(m_db->getPref("ringtone", v));
	EXPECT_EQ("{\"fullPath\":\"/a.mp3\"}", v);

	// a second set replaces the value
	EXPECT_TRUE(m_db->setPref("ringtone", "other"));
	EXPECT_EQ("other", m_db->getPref("ringtone"));

	EXPECT_EQ("", m_db->getPref("missing"));
	v = "untouched";
	EXPECT_FALSE(m_db->getPref("missing", v));
	EXPECT_EQ("untouched", v);
}

TEST_F(PrefsDbTest, EmptyKeysAreRefused)
{
	EXPECT_FALSE(m_db->setPref("", "v"));
	EXPECT_EQ("", m_db->getPref(""));
	std::string v;
	EXPECT_FALSE(m_db->getPref("", v));
}

TEST_F(PrefsDbTest, QuotesInKeysAndValuesAreData)
{
	EXPECT_TRUE(m_db->setPref(kInjection, kQuoted));
	EXPECT_TRUE(m_db->setPref(kQuoted, kInjection));
	EXPECT_TRUE(m_db->setPref("plain", "value"));

	EXPECT_EQ(kQuoted, m_db->getPref(kInjection));
	EXPECT_EQ(kInjection, m_db->getPref(kQuoted));
	std::string v;
	EXPECT_TRUE(m_db->getPref(kInjection, v));
	EXPECT_EQ(kQuoted, v);
	EXPECT_TRUE(m_db->getPref(kQuoted, v));
	EXPECT_EQ(kInjection, v);
	// the table is still there and nothing else changed
	EXPECT_EQ("value", m_db->getPref("plain"));

	std::map<std::string, std::string> all = m_db->getAllPrefs();
	EXPECT_EQ(kQuoted, all[kInjection]);
	EXPECT_EQ("value", all["plain"]);
}

TEST_F(PrefsDbTest, GetPrefsReturnsOnlyTheKeysAsked)
{
	ASSERT_TRUE(m_db->setPref("a", "1"));
	ASSERT_TRUE(m_db->setPref("b", "2"));
	ASSERT_TRUE(m_db->setPref(kInjection, "3"));

	std::map<std::string, std::string> got = m_db->getPrefs({"a", kInjection, "nope"});
	EXPECT_EQ((std::map<std::string, std::string>{{"a", "1"}, {kInjection, "3"}}), got);

	// a key that would close the IN () list must not widen the query
	got = m_db->getPrefs({"x') OR ('1'='1"});
	EXPECT_TRUE(got.empty());

	EXPECT_TRUE(m_db->getPrefs({}).empty());
}

TEST_F(PrefsDbTest, CopyKeysHonoursOverwrite)
{
	std::unique_ptr<PrefsDb> src(PrefsDb::createStandalone(m_dir.file("src.db")));
	ASSERT_NE(nullptr, src);
	ASSERT_TRUE(src->setPref("a", "src-a"));
	ASSERT_TRUE(src->setPref("b", "src-b"));
	ASSERT_TRUE(m_db->setPref("a", "dst-a"));

	EXPECT_EQ(1, m_db->copyKeys(src.get(), {"a", "b", "c"}, false));
	EXPECT_EQ("dst-a", m_db->getPref("a"));
	EXPECT_EQ("src-b", m_db->getPref("b"));

	EXPECT_EQ(2, m_db->copyKeys(src.get(), {"a", "b"}, true));
	EXPECT_EQ("src-a", m_db->getPref("a"));

	EXPECT_EQ(0, m_db->copyKeys(m_db.get(), {"a"}, true));
	EXPECT_EQ(0, m_db->copyKeys(nullptr, {"a"}, true));
	EXPECT_EQ(0, m_db->copyKeys(src.get(), {}, true));
}

TEST_F(PrefsDbTest, MergeWithAQuoteInTheFileName)
{
	const std::string srcName = m_dir.file("it's.db");
	std::unique_ptr<PrefsDb> src(PrefsDb::createStandalone(srcName));
	ASSERT_NE(nullptr, src);
	ASSERT_TRUE(src->setPref("merged", "yes"));
	src.reset();

	EXPECT_EQ(1, m_db->merge(srcName, true));
	EXPECT_EQ("yes", m_db->getPref("merged"));
	// only the overwriting merge exists
	EXPECT_EQ(0, m_db->merge(srcName, false));
}

TEST_F(PrefsDbTest, ReopeningKeepsTheData)
{
	ASSERT_TRUE(m_db->setPref("persist", "1"));
	m_db.reset(PrefsDb::createStandalone(m_dir.file("prefs.db"), false));
	ASSERT_NE(nullptr, m_db);
	EXPECT_EQ("1", m_db->getPref("persist"));

	m_db.reset(PrefsDb::createStandalone(m_dir.file("prefs.db"), true));
	ASSERT_NE(nullptr, m_db);
	EXPECT_EQ("", m_db->getPref("persist"));
}
