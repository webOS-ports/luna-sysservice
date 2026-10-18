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
#include <QImage>

#include "SystemRestore.h"
#include "TempDir.h"

// getPreferences runs this check on every read of "wallpaper" and puts the
// default back when it fails, so a check that wrongly fails undoes every
// wallpaper anyone picks.

static std::string wallpaperPref(const std::string& file)
{
	return "{\"wallpaperName\":\"w.png\",\"wallpaperFile\":\"" + file +
	       "\",\"wallpaperThumbFile\":\"\"}";
}

class WallpaperConsistencyTest : public testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_FALSE(m_dir.path().empty());
		QImage image(4, 4, QImage::Format_RGB32);
		image.fill(0xff2a7fd4);
		ASSERT_TRUE(image.save(QString::fromStdString(m_dir.file("w.png")), "PNG"));
	}

	TempDir m_dir;
};

TEST_F(WallpaperConsistencyTest, AnImageThatIsThereIsConsistent)
{
	EXPECT_TRUE(SystemRestore::isWallpaperPrefConsistent(wallpaperPref(m_dir.file("w.png"))));
}

TEST_F(WallpaperConsistencyTest, AMissingFileIsNot)
{
	EXPECT_FALSE(SystemRestore::isWallpaperPrefConsistent(wallpaperPref(m_dir.file("gone.png"))));
}

TEST_F(WallpaperConsistencyTest, AFileThatIsNoImageIsNot)
{
	ASSERT_TRUE(TempDir::write(m_dir.file("text.png"), "not an image"));
	EXPECT_FALSE(SystemRestore::isWallpaperPrefConsistent(wallpaperPref(m_dir.file("text.png"))));
}

TEST_F(WallpaperConsistencyTest, ASettingThatIsNoWallpaperIsNot)
{
	EXPECT_FALSE(SystemRestore::isWallpaperPrefConsistent(""));
	EXPECT_FALSE(SystemRestore::isWallpaperPrefConsistent("not json"));
	EXPECT_FALSE(SystemRestore::isWallpaperPrefConsistent("[]"));
	EXPECT_FALSE(SystemRestore::isWallpaperPrefConsistent("{\"wallpaperName\":\"w.png\"}"));
	EXPECT_FALSE(SystemRestore::isWallpaperPrefConsistent("{\"wallpaperFile\":42}"));
}
