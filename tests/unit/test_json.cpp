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

#include <string>

#include <gtest/gtest.h>

#include "JSONUtils.h"
#include "TimePrefsHandler.h"

// The schema macros are what every bus method uses to validate its payload;
// these are the schemas of addRingtone and setTimeZoneByEasData (sic) as the
// handlers spell them.

static const char *kRingtoneSchema =
	STRICT_SCHEMA(PROPS_1(PROPERTY(filePath, string)) REQUIRED_1(filePath));

static const char *kEasSchema =
	STRICT_SCHEMA(PROPS_5(PROPERTY(bias, integer),
	                      NAKED_OBJECT_OPTIONAL_8(standardDate, year, integer, month, integer, dayOfWeek, integer, day, integer, week, integer, hour, integer, minute, integer, second, integer),
	                      PROPERTY(standardBias, integer),
	                      NAKED_OBJECT_OPTIONAL_8(daylightDate, year, integer, month, integer, dayOfWeek, integer, day, integer, week, integer, hour, integer, minute, integer, second, integer),
	                      PROPERTY(daylightBias, integer))
	              REQUIRED_1(bias));

static bool accepts(const char *schema, const char *json)
{
	JsonMessageParser parser(json, schema);
	return parser.parse(__FUNCTION__);
}

TEST(JsonSchema, StrictSchemaAcceptsOnlyWhatItDescribes)
{
	EXPECT_TRUE(accepts(kRingtoneSchema, R"({"filePath": "/media/internal/ringtones/a.mp3"})"));

	EXPECT_FALSE(accepts(kRingtoneSchema, R"({})"));
	EXPECT_FALSE(accepts(kRingtoneSchema, R"({"filePath": 1})"));
	EXPECT_FALSE(accepts(kRingtoneSchema, R"({"filePath": "/a", "extra": true})"));
	EXPECT_FALSE(accepts(kRingtoneSchema, R"(["filePath"])"));
	EXPECT_FALSE(accepts(kRingtoneSchema, R"({"filePath": "/a")"));
	EXPECT_FALSE(accepts(kRingtoneSchema, ""));
}

TEST(JsonSchema, RelaxedSchemaAllowsExtraProperties)
{
	const char *schema = RELAXED_SCHEMA(PROPS_1(PROPERTY(tempDir, string)));
	EXPECT_TRUE(accepts(schema, R"({"tempDir": "/tmp", "other": 1})"));
	EXPECT_FALSE(accepts(schema, R"({"tempDir": 5})"));
}

TEST(JsonSchema, NestedObjectsAreStrictToo)
{
	EXPECT_TRUE(accepts(kEasSchema, R"({"bias": -60})"));
	EXPECT_TRUE(accepts(kEasSchema,
		R"({"bias": -60, "standardDate": {"year": 0, "month": 10, "dayOfWeek": 0, "day": 5, "hour": 3}})"));
	EXPECT_FALSE(accepts(kEasSchema, R"({"standardBias": 0})"));
	EXPECT_FALSE(accepts(kEasSchema, R"({"bias": "-60"})"));
	EXPECT_FALSE(accepts(kEasSchema, R"({"bias": 1.5})"));
	EXPECT_FALSE(accepts(kEasSchema, R"({"bias": 0, "standardDate": {"month": "10"}})"));
	EXPECT_FALSE(accepts(kEasSchema, R"({"bias": 0, "daylightDate": {"leap": 1}})"));
}

TEST(JsonSchema, GettersReadTheParsedMessage)
{
	JsonMessageParser parser(R"({"s": "text", "b": true, "n": 42})",
	                         RELAXED_SCHEMA(PROPS_3(PROPERTY(s, string), PROPERTY(b, boolean), PROPERTY(n, integer))));
	ASSERT_TRUE(parser.parse(__FUNCTION__));

	std::string s;
	bool b = false;
	int n = 0;
	EXPECT_TRUE(parser.get("s", s));
	EXPECT_EQ("text", s);
	EXPECT_TRUE(parser.get("b", b));
	EXPECT_TRUE(b);
	EXPECT_TRUE(parser.get("n", n));
	EXPECT_EQ(42, n);

	EXPECT_FALSE(parser.get("n", s));
	EXPECT_FALSE(parser.get("missing", b));
}

TEST(JsonReply, CarriesOnlyWhatIsGiven)
{
	pbnjson::JValue ok = createJsonReply();
	EXPECT_TRUE(ok["returnValue"].asBool());
	EXPECT_FALSE(ok.hasKey("errorCode"));
	EXPECT_FALSE(ok.hasKey("errorText"));

	pbnjson::JValue err = createJsonReply(false, -1, "bad");
	EXPECT_FALSE(err["returnValue"].asBool());
	EXPECT_EQ(-1, err["errorCode"].asNumber<int>());
	EXPECT_EQ("bad", err["errorText"].asString());
}

// The timeZone preference arrives over the bus as a json zone object.
TEST(TimeZoneJson, ZoneIdIsOnlyTakenFromAStringField)
{
	EXPECT_EQ("Europe/Amsterdam",
	          TimePrefsHandler::tzNameFromJsonString(R"({"ZoneID": "Europe/Amsterdam", "City": "Amsterdam"})"));
	EXPECT_EQ("", TimePrefsHandler::tzNameFromJsonString(R"({"ZoneID": 1})"));
	EXPECT_EQ("", TimePrefsHandler::tzNameFromJsonString(R"({"City": "Amsterdam"})"));
	EXPECT_EQ("", TimePrefsHandler::tzNameFromJsonString(R"(["Europe/Amsterdam"])"));
	EXPECT_EQ("", TimePrefsHandler::tzNameFromJsonString("not json"));
	EXPECT_EQ("", TimePrefsHandler::tzNameFromJsonString(""));

	pbnjson::JValue zone = pbnjson::JObject{{"ZoneID", "Asia/Tokyo"}, {"City", "Tokyo"}};
	EXPECT_EQ("Asia/Tokyo", TimePrefsHandler::tzNameFromJsonValue(zone));
	EXPECT_EQ("Tokyo", TimePrefsHandler::tzCityNameFromJsonValue(zone));

	pbnjson::JValue bad = pbnjson::JObject{{"ZoneID", 3}, {"City", false}};
	EXPECT_EQ("", TimePrefsHandler::tzNameFromJsonValue(bad));
	EXPECT_EQ("", TimePrefsHandler::tzCityNameFromJsonValue(bad));
	EXPECT_EQ("", TimePrefsHandler::tzNameFromJsonValue(pbnjson::JValue("Asia/Tokyo")));
	EXPECT_EQ("", TimePrefsHandler::tzCityNameFromJsonValue(pbnjson::Array()));
}

TEST(TimeZoneJson, QualifiedIdRejectsMalformedInput)
{
	EXPECT_EQ("", TimePrefsHandler::getQualifiedTZIdFromJson(""));
	EXPECT_EQ("", TimePrefsHandler::getQualifiedTZIdFromJson("not json"));
	EXPECT_EQ("", TimePrefsHandler::getQualifiedTZIdFromJson(R"(["x"])"));
	EXPECT_EQ("", TimePrefsHandler::getQualifiedTZIdFromJson(R"({"ZoneID": 7})"));
	EXPECT_EQ("", TimePrefsHandler::getQualifiedTZIdFromName(""));
}
