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

#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include "TzParser.h"
#include "Utils.h"
#include "TempDir.h"

// parseTimeZone() reads /usr/share/zoneinfo/<name>; the name comes from the
// bus (setPreferences timeZone). These tests read the build host's tzdata.

static const char *kZoneInfo = "/usr/share/zoneinfo/";

static bool haveZone(const char *name)
{
	return Utils::doesExistOnFilesystem((std::string(kZoneInfo) + name).c_str());
}

static const TzTransition *findTransition(const TzTransitionList& list, time_t when)
{
	for (const TzTransition& t : list)
		if (t.time == when)
			return &t;
	return nullptr;
}

TEST(TzParser, RejectsBadNames)
{
	EXPECT_TRUE(parseTimeZone(nullptr).empty());
	EXPECT_TRUE(parseTimeZone("").empty());
	EXPECT_TRUE(parseTimeZone("No/Such_Zone").empty());
	// a directory is not a zone file
	EXPECT_TRUE(parseTimeZone("Europe").empty());
}

TEST(TzParser, ParsesAZoneWithTransitions)
{
	if (!haveZone("Europe/Amsterdam"))
		GTEST_SKIP() << "no Europe/Amsterdam in the host tzdata";

	TzTransitionList list = parseTimeZone("Europe/Amsterdam");
	ASSERT_FALSE(list.empty());

	// 1996-03-31 01:00 UTC: to CEST
	const TzTransition *summer = findTransition(list, 828234000);
	ASSERT_NE(nullptr, summer);
	EXPECT_TRUE(summer->isDst);
	EXPECT_EQ(7200, summer->utcOffset);
	EXPECT_EQ(1996, summer->year);
	EXPECT_STREQ("CEST", summer->abbrName);

	// 1996-10-27 01:00 UTC: back to CET
	const TzTransition *winter = findTransition(list, 846378000);
	ASSERT_NE(nullptr, winter);
	EXPECT_FALSE(winter->isDst);
	EXPECT_EQ(3600, winter->utcOffset);
	EXPECT_STREQ("CET", winter->abbrName);

	for (const TzTransition& t : list)
		EXPECT_LT(strlen(t.abbrName), static_cast<size_t>(TZ_ABBR_MAX_LEN));
}

TEST(TzParser, AZoneWithoutTransitionsGetsOneEntry)
{
	if (!haveZone("UTC"))
		GTEST_SKIP() << "no UTC in the host tzdata";

	TzTransitionList list = parseTimeZone("UTC");
	ASSERT_EQ(1u, list.size());
	EXPECT_EQ(0, list.front().utcOffset);
	EXPECT_FALSE(list.front().isDst);
	EXPECT_STREQ("UTC", list.front().abbrName);
}

TEST(TzParser, FallsBackToEtc)
{
	if (!haveZone("Etc/GMT+5") || haveZone("GMT+5"))
		GTEST_SKIP() << "host tzdata has no Etc-only zone to try";

	TzTransitionList list = parseTimeZone("GMT+5");
	ASSERT_EQ(1u, list.size());
	EXPECT_EQ(-5 * 3600, list.front().utcOffset);
}

TEST(TzParser, StaysInsideTheZoneinfoDirectory)
{
	if (!haveZone("Europe/Amsterdam"))
		GTEST_SKIP() << "no Europe/Amsterdam in the host tzdata";

	// a perfectly good zone file outside zoneinfo, reachable by ".."
	TempDir dir;
	ASSERT_FALSE(dir.path().empty());
	ASSERT_EQ('/', dir.path()[0]);
	ASSERT_EQ(1, Utils::fileCopy((std::string(kZoneInfo) + "Europe/Amsterdam").c_str(),
	                             dir.file("zone").c_str()));
	const std::string escape = "../../.." + dir.path() + "/zone";
	ASSERT_TRUE(Utils::doesExistOnFilesystem((kZoneInfo + escape).c_str()));

	EXPECT_TRUE(parseTimeZone(escape.c_str()).empty());
	EXPECT_TRUE(parseTimeZone(("Europe/../../../.." + dir.path() + "/zone").c_str()).empty());
	// "/." components and dot entries are refused even where they resolve
	EXPECT_TRUE(parseTimeZone("Europe/./Amsterdam").empty());
	EXPECT_TRUE(parseTimeZone("./Europe/Amsterdam").empty());
	EXPECT_TRUE(parseTimeZone((dir.path() + "/zone").c_str()).empty());
}
