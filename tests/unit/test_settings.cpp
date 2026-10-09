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

#include <gtest/gtest.h>

#include "Settings.h"

TEST(Settings, SchemaErrorOptionOnlyTakesDefinedValues)
{
	for (int v = EIgnore; v <= EDefault; v++) {
		ESchemaErrorOptions option = EIgnore;
		EXPECT_TRUE(schemaErrorOptionFromInt(v, option)) << v;
		EXPECT_EQ(v, static_cast<int>(option));
	}

	for (int v : {-1, EDefault + 1, 7, 8, 1000, -1000}) {
		ESchemaErrorOptions option = EValidateAndContinue;
		EXPECT_FALSE(schemaErrorOptionFromInt(v, option)) << v;
		EXPECT_EQ(EValidateAndContinue, option) << v;
	}
}

TEST(Settings, LogLevelOptionIsLowerCased)
{
	char prog[] = "LunaSysService";
	char opt[] = "-l";
	char level[] = "DeBuG";
	char *argv[] = {prog, opt, level, nullptr};

	ASSERT_TRUE(Settings::instance()->parseCommandlineOptions(3, argv));
	EXPECT_EQ("debug", Settings::instance()->m_logLevel);
}
