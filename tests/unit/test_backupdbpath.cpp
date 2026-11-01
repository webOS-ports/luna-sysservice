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

#include <unistd.h>

#include <gtest/gtest.h>
#include <glib/gstdio.h>

#include "BackupManager.h"
#include "PrefsDb.h"
#include "TempDir.h"

// postRestore's "files" entries come from the backup engine, which read them
// from a backup anyone with USB access can write. Only the backup database
// preBackup wrote, under tempDir, may be read.

class BackupDbPathTest : public testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_FALSE(m_dir.path().empty());
		m_tempDir = m_dir.file("restore");
		ASSERT_EQ(0, g_mkdir(m_tempDir.c_str(), 0700));
		m_backupFile = m_tempDir + "/" + PrefsDb::s_tempBackupDbFilenameOnly;
		writeBackupAt(m_backupFile);
		ASSERT_EQ(0, g_mkdir((m_tempDir + "/sub").c_str(), 0700));
		writeBackupAt(m_tempDir + "/sub/" + PrefsDb::s_tempBackupDbFilenameOnly);
		// same name, outside tempDir
		writeBackupAt(m_dir.file(PrefsDb::s_tempBackupDbFilenameOnly));
		// a sibling of tempDir whose name starts with tempDir's
		ASSERT_EQ(0, g_mkdir((m_tempDir + "-evil").c_str(), 0700));
		writeBackupAt(m_tempDir + "-evil/" + PrefsDb::s_tempBackupDbFilenameOnly);
	}

	static void writeBackupAt(const std::string& file)
	{
		std::unique_ptr<PrefsDb> db(PrefsDb::createStandalone(file));
		ASSERT_NE(nullptr, db);
	}

	std::string path(const std::string& entry) const
	{
		return BackupManager::backupDbPath(m_tempDir, entry);
	}

	TempDir m_dir;
	std::string m_tempDir;
	std::string m_backupFile;
};

TEST_F(BackupDbPathTest, TheBackupDbUnderTempDirIsAccepted)
{
	EXPECT_EQ(m_backupFile, path(PrefsDb::s_tempBackupDbFilenameOnly));
	EXPECT_EQ(m_backupFile, path(m_backupFile));
	EXPECT_EQ(m_backupFile, path("./systemprefs_backup.db"));
	EXPECT_EQ(m_tempDir + "/sub/systemprefs_backup.db", path("sub/systemprefs_backup.db"));
	// a trailing slash on tempDir changes nothing
	EXPECT_EQ(m_backupFile, BackupManager::backupDbPath(m_tempDir + "/", "systemprefs_backup.db"));
}

TEST_F(BackupDbPathTest, OnlyTheExactFileNameIsAccepted)
{
	// each of these is there, as a real database
	ASSERT_EQ(0, g_mkdir((m_tempDir + "/x").c_str(), 0700));
	const char* lookAlikes[] = {"x/systemprefs_backup.db.evil", "systemprefs_backup.db.evil",
	                            "evil_systemprefs_backup.db", "systemprefs_backup",
	                            "SYSTEMPREFS_BACKUP.DB"};
	for (const char* entry : lookAlikes) {
		writeBackupAt(m_tempDir + "/" + entry);
		EXPECT_EQ("", path(entry)) << entry;
		EXPECT_EQ("", path(m_tempDir + "/" + entry)) << entry;
	}
	for (const char* entry : {"systemprefs_backup.db/", "sub", "x", "sub/"})
		EXPECT_EQ("", path(entry)) << entry;
}

TEST_F(BackupDbPathTest, DotDotIsRefused)
{
	EXPECT_EQ("", path("../systemprefs_backup.db"));
	EXPECT_EQ("", path("../restore-evil/systemprefs_backup.db"));
	// even where it would resolve back under tempDir
	EXPECT_EQ("", path("sub/../systemprefs_backup.db"));
	EXPECT_EQ("", path(m_tempDir + "/sub/../systemprefs_backup.db"));
}

TEST_F(BackupDbPathTest, PathsOutsideTempDirAreRefused)
{
	EXPECT_EQ("", path(m_dir.file("systemprefs_backup.db")));
	EXPECT_EQ("", path(m_tempDir + "-evil/systemprefs_backup.db"));
	EXPECT_EQ("", path("/systemprefs_backup.db"));
}

TEST_F(BackupDbPathTest, SymlinksAreRefused)
{
	// the file itself
	ASSERT_EQ(0, g_unlink(m_backupFile.c_str()));
	ASSERT_EQ(0, symlink(m_dir.file("systemprefs_backup.db").c_str(), m_backupFile.c_str()));
	EXPECT_EQ("", path("systemprefs_backup.db"));

	// a directory under tempDir that leads out of it
	ASSERT_EQ(0, symlink(m_dir.path().c_str(), (m_tempDir + "/out").c_str()));
	EXPECT_EQ("", path("out/systemprefs_backup.db"));

	// a symlink to a file inside tempDir is still a symlink
	ASSERT_EQ(0, g_mkdir((m_tempDir + "/d").c_str(), 0700));
	ASSERT_EQ(0, symlink((m_tempDir + "/sub/systemprefs_backup.db").c_str(),
	                     (m_tempDir + "/d/systemprefs_backup.db").c_str()));
	EXPECT_EQ("", path("d/systemprefs_backup.db"));
}

TEST_F(BackupDbPathTest, ADirectoryOrAMissingFileIsRefused)
{
	ASSERT_EQ(0, g_unlink(m_backupFile.c_str()));
	EXPECT_EQ("", path("systemprefs_backup.db"));
	ASSERT_EQ(0, g_mkdir(m_backupFile.c_str(), 0700));
	EXPECT_EQ("", path("systemprefs_backup.db"));
}

TEST_F(BackupDbPathTest, NulBytesAreRefused)
{
	// c_str() would stop at the NUL and read a file the caller did not name
	EXPECT_EQ("", BackupManager::backupDbPath(m_tempDir + std::string(1, '\0') + "/elsewhere",
	                                          "systemprefs_backup.db"));
	EXPECT_EQ("", path(std::string("sub") + '\0' + "/systemprefs_backup.db"));
}

TEST_F(BackupDbPathTest, ABadTempDirIsRefused)
{
	EXPECT_EQ("", BackupManager::backupDbPath("", "systemprefs_backup.db"));
	// a relative tempDir, even one that resolves from the current directory
	gchar* cwd = g_get_current_dir();
	ASSERT_EQ(0, g_chdir(m_dir.path().c_str()));
	std::string relative = BackupManager::backupDbPath("restore", "systemprefs_backup.db");
	ASSERT_EQ(0, g_chdir(cwd));
	g_free(cwd);
	EXPECT_EQ("", relative);
	EXPECT_EQ("", BackupManager::backupDbPath(m_dir.file("missing"), "systemprefs_backup.db"));
	EXPECT_EQ("", path(""));
}
