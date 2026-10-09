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

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <gtest/gtest.h>

#include "TimePrefsHandler.h"
#include "Utils.h"

// /time/convertDate and the TZ juggling behind it. These tests read the
// build host's /usr/share/zoneinfo and skip zones it lacks.

static bool haveZones()
{
	return Utils::doesExistOnFilesystem("/usr/share/zoneinfo/Europe/Amsterdam")
	    && Utils::doesExistOnFilesystem("/usr/share/zoneinfo/UTC")
	    && Utils::doesExistOnFilesystem("/usr/share/zoneinfo/Asia/Tokyo");
}

// Puts TZ back as the test found it.
class SavedTz
{
public:
	SavedTz()
	{
		const char *tz = getenv("TZ");
		m_had = (tz != nullptr);
		if (m_had)
			m_value = tz;
	}
	~SavedTz()
	{
		if (m_had)
			setenv("TZ", m_value.c_str(), 1);
		else
			unsetenv("TZ");
		tzset();
	}

private:
	bool m_had = false;
	std::string m_value;
};

TEST(ConvertDate, ConvertsBetweenZones)
{
	if (!haveZones())
		GTEST_SKIP() << "host lacks Europe/Amsterdam, UTC or Asia/Tokyo";
	SavedTz saved;

	std::string result, error;
	ASSERT_TRUE(TimePrefsHandler::convertDate("2024-01-01 12:00:00", "Europe/Amsterdam",
	                                          "UTC", result, error)) << error;
	EXPECT_EQ("Mon Jan  1 11:00:00 2024", result);

	// summer time on the source side
	ASSERT_TRUE(TimePrefsHandler::convertDate("2024-07-01 12:00:00", "Europe/Amsterdam",
	                                          "Asia/Tokyo", result, error)) << error;
	EXPECT_EQ("Mon Jul  1 19:00:00 2024", result);
}

TEST(ConvertDate, RejectsBadInput)
{
	SavedTz saved;
	std::string result = "untouched", error;

	EXPECT_FALSE(TimePrefsHandler::convertDate("2024-01-01", "UTC", "UTC", result, error));
	EXPECT_EQ("unrecognized date format: '2024-01-01'", error);
	EXPECT_FALSE(TimePrefsHandler::convertDate("2024-01-01 12:00:00x", "UTC", "UTC", result, error));
	EXPECT_EQ("unrecognized characters in date: '2024-01-01 12:00:00x'", error);
	EXPECT_FALSE(TimePrefsHandler::convertDate("2024-01-01 12:00:00", "../etc/passwd", "UTC", result, error));
	EXPECT_EQ("timezone not found: '../etc/passwd'", error);
	EXPECT_FALSE(TimePrefsHandler::convertDate("2024-01-01 12:00:00", "UTC", "No/Such_Zone", result, error));
	EXPECT_EQ("timezone not found: 'No/Such_Zone'", error);
	EXPECT_FALSE(TimePrefsHandler::convertDate("2024-01-01 12:00:00", "", "UTC", result, error));
	EXPECT_EQ("untouched", result);
}

// The service's own TZ must come out of a conversion as it went in: set to
// the same zone, or not set at all.
TEST(ConvertDate, LeavesTzAsItWas)
{
	if (!haveZones())
		GTEST_SKIP() << "host lacks Europe/Amsterdam, UTC or Asia/Tokyo";
	SavedTz saved;
	std::string result, error;

	setenv("TZ", "Asia/Tokyo", 1);
	tzset();
	ASSERT_TRUE(TimePrefsHandler::convertDate("2024-01-01 12:00:00", "Europe/Amsterdam",
	                                          "UTC", result, error)) << error;
	ASSERT_NE(nullptr, getenv("TZ"));
	EXPECT_STREQ("Asia/Tokyo", getenv("TZ"));

	unsetenv("TZ");
	tzset();
	ASSERT_TRUE(TimePrefsHandler::convertDate("2024-01-01 12:00:00", "Europe/Amsterdam",
	                                          "UTC", result, error)) << error;
	EXPECT_EQ(nullptr, getenv("TZ"));
}

// Each conversion sets TZ three times. The old setter freed the previous
// "TZ=..." string while environ still pointed at it, so every later setter
// read freed memory inside putenv(). valgrind reports that; ASan does not,
// as the read happens in the uninstrumented C library.
TEST(ConvertDate, RepeatedConversionsAgree)
{
	if (!haveZones())
		GTEST_SKIP() << "host lacks Europe/Amsterdam, UTC or Asia/Tokyo";
	SavedTz saved;
	setenv("TZ", "UTC", 1);
	tzset();

	for (int i = 0; i < 4; i++) {
		std::string result, error;
		ASSERT_TRUE(TimePrefsHandler::convertDate("2024-01-01 12:00:00", "Europe/Amsterdam",
		                                          "Asia/Tokyo", result, error)) << error;
		EXPECT_EQ("Mon Jan  1 20:00:00 2024", result);
	}
	EXPECT_STREQ("UTC", getenv("TZ"));
}

TEST(ZoneAbbreviation, FitsOrIsEmpty)
{
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 124;
	tm.tm_mday = 1;

	char cet[] = "CET";
	tm.tm_zone = cet;
	EXPECT_EQ("CET", TimePrefsHandler::zoneAbbreviation(tm));

	// strftime() fails on this and leaves its buffer unspecified; the old
	// code used the buffer anyway
	std::string longName(200, 'X');
	tm.tm_zone = &longName[0];
	EXPECT_EQ("", TimePrefsHandler::zoneAbbreviation(tm));
}
