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
#include <pbnjson.hpp>

#include "PrefsFactory.h"

// getPreferences returns stored values through valueFromDbString(). Values
// are normally stored as serialized json; platform defaults and customer
// care entries are stored as bare strings.

TEST(PrefsValue, JsonIsReturnedAsIs)
{
	pbnjson::JValue v = PrefsFactory::valueFromDbString(R"({"ZoneID":"Europe/Amsterdam"})");
	ASSERT_TRUE(v.isObject());
	EXPECT_EQ("Europe/Amsterdam", v["ZoneID"].asString());

	v = PrefsFactory::valueFromDbString(R"("quoted")");
	ASSERT_TRUE(v.isString());
	EXPECT_EQ("quoted", v.asString());

	EXPECT_TRUE(PrefsFactory::valueFromDbString("true").asBool());
	EXPECT_TRUE(PrefsFactory::valueFromDbString("null").isNull());
	EXPECT_EQ(42, PrefsFactory::valueFromDbString("42").asNumber<int>());
}

TEST(PrefsValue, BareStringsAndPrimitives)
{
	pbnjson::JValue v = PrefsFactory::valueFromDbString("Europe/Amsterdam");
	ASSERT_TRUE(v.isString());
	EXPECT_EQ("Europe/Amsterdam", v.asString());

	// a number followed by whitespace only is still a number
	v = PrefsFactory::valueFromDbString("3.5 ");
	ASSERT_TRUE(v.isNumber());
	EXPECT_DOUBLE_EQ(3.5, v.asNumber<double>());

	// anything else after the number makes it a string, a byte above 0x7f too
	v = PrefsFactory::valueFromDbString("12abc");
	ASSERT_TRUE(v.isString());
	EXPECT_EQ("12abc", v.asString());
	v = PrefsFactory::valueFromDbString("5\xa0");
	ASSERT_TRUE(v.isString());
	EXPECT_EQ("5\xa0", v.asString());
}

// A bare string with a quote or a backslash used to be pasted into json
// text unescaped, which failed to parse and made getPreferences fail for
// every key requested with it.
TEST(PrefsValue, BareStringsWithJsonSyntaxSurvive)
{
	pbnjson::JValue v = PrefsFactory::valueFromDbString("say \"hi\"");
	ASSERT_TRUE(v.isString());
	EXPECT_EQ("say \"hi\"", v.asString());

	v = PrefsFactory::valueFromDbString("C:\\path");
	ASSERT_TRUE(v.isString());
	EXPECT_EQ("C:\\path", v.asString());
}
