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

#include "UrlRep.h"

// UrlRep breaks down the urls apps hand over the bus (wallpaper and ringtone
// targets, image paths) before sysservice touches the file they name.

TEST(UrlRep, RejectsNullAndUnparsableInput)
{
	EXPECT_FALSE(UrlRep::fromUrl(nullptr).valid);
	EXPECT_FALSE(UrlRep::fromUrl("http://[::1").valid);
	EXPECT_FALSE(UrlRep::fromUrl("a b c://x").valid);
}

TEST(UrlRep, SplitsAllComponents)
{
	UrlRep u = UrlRep::fromUrl("http://user@example.org:8080/dir/sub/file.jpg?x=1&y=t%20o#frag");
	ASSERT_TRUE(u.valid);
	EXPECT_EQ("http", u.scheme);
	EXPECT_EQ("user", u.userInfo);
	EXPECT_EQ("example.org", u.host);
	EXPECT_EQ("8080", u.port);
	EXPECT_EQ("/dir/sub/file.jpg", u.path);
	EXPECT_EQ("/dir/sub", u.pathOnly);
	EXPECT_EQ("file.jpg", u.resource);
	EXPECT_EQ("frag", u.fragment);
	ASSERT_EQ(2u, u.query.size());
	EXPECT_EQ("1", u.query["x"]);
	EXPECT_EQ("t o", u.query["y"]);
}

TEST(UrlRep, FileUrlsAreUnescaped)
{
	UrlRep u = UrlRep::fromUrl("file:///media/internal/my%20pic.png");
	ASSERT_TRUE(u.valid);
	EXPECT_EQ("file", u.scheme);
	EXPECT_EQ("", u.host);
	EXPECT_EQ("/media/internal/my pic.png", u.path);
	EXPECT_EQ("/media/internal", u.pathOnly);
	EXPECT_EQ("my pic.png", u.resource);
	EXPECT_TRUE(u.query.empty());
}

TEST(UrlRep, QueryKeysWithoutValues)
{
	UrlRep u = UrlRep::fromUrl("http://h/p?flag&k=");
	ASSERT_TRUE(u.valid);
	ASSERT_EQ(1u, u.query.count("flag"));
	EXPECT_EQ("", u.query["flag"]);
	ASSERT_EQ(1u, u.query.count("k"));
	EXPECT_EQ("", u.query["k"]);
}

TEST(UrlRep, EscapeAndUnescape)
{
	EXPECT_EQ("", escape(""));
	EXPECT_EQ("a%20b", escape("a b"));
	EXPECT_EQ("a b", unescape("a%20b"));
	EXPECT_EQ("100%", unescape("100%"));

	const std::string nasty = "%/?#[]@!$&'()*+,;= \t\"<>";
	EXPECT_EQ(nasty, unescape(escape(nasty)));
}
