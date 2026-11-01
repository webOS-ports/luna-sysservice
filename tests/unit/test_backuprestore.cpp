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

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>

#include <sqlite3.h>
#include <unistd.h>

#include <gtest/gtest.h>
#include <glib/gstdio.h>

#include "BackupManager.h"
#include "PrefsDb.h"
#include "TempDir.h"

// postRestore reads a backup that sat on storage anyone with USB access can
// write, so the backup database, its file name and every row in it are
// hostile. Only the keys preBackup backs up may come back.

static const char kInjection[] = "x'); DROP TABLE Preferences; --";
static const char kWidening[] = "x' OR '1'='1";

class BackupRestoreTest : public testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_FALSE(m_dir.path().empty());
		m_tempDir = m_dir.file("restore");
		ASSERT_EQ(0, g_mkdir(m_tempDir.c_str(), 0700));
		m_backupFile = m_tempDir + "/" + PrefsDb::s_tempBackupDbFilenameOnly;
		m_keylist = m_dir.file("backupkeys.json");

		m_live.reset(PrefsDb::createStandalone(m_dir.file("live.db")));
		ASSERT_NE(nullptr, m_live);
		ASSERT_TRUE(m_live->setPref("timeZone", "live-tz"));
		ASSERT_TRUE(m_live->setPref("developerMode", "false"));
	}

	// a backup database holding rows, the way preBackup writes one
	void writeBackup(const std::map<std::string, std::string>& rows)
	{
		std::unique_ptr<PrefsDb> backup(PrefsDb::createStandalone(m_backupFile));
		ASSERT_NE(nullptr, backup);
		for (const auto& row : rows)
			ASSERT_TRUE(backup->setPref(row.first, row.second));
	}

	// a database built by hand, for shapes preBackup never writes
	static void writeRawDb(const std::string& file, const char* sql)
	{
		sqlite3* db = nullptr;
		ASSERT_EQ(SQLITE_OK, sqlite3_open(file.c_str(), &db));
		char* err = nullptr;
		int ret = sqlite3_exec(db, sql, nullptr, nullptr, &err);
		std::string msg = err ? err : "";
		sqlite3_free(err);
		sqlite3_close(db);
		ASSERT_EQ(SQLITE_OK, ret) << msg;
	}

	int restore(const PrefsDb::RestoreFilter& accept = PrefsDb::RestoreFilter())
	{
		return BackupManager::restoreFromBackupDb(*m_live, m_backupFile, m_keylist, accept);
	}

	TempDir m_dir;
	std::string m_tempDir;
	std::string m_backupFile;
	std::string m_keylist;
	std::unique_ptr<PrefsDb> m_live;
};

TEST_F(BackupRestoreTest, RestoresTheListedKeys)
{
	ASSERT_TRUE(TempDir::write(m_keylist, R"(["timeZone", "useNetworkTime", "notInTheBackup"])"));
	writeBackup({{"timeZone", "backup-tz"}, {"useNetworkTime", "true"}});

	EXPECT_EQ(2, restore());
	EXPECT_EQ("backup-tz", m_live->getPref("timeZone"));
	EXPECT_EQ("true", m_live->getPref("useNetworkTime"));
	std::string v;
	EXPECT_FALSE(m_live->getPref("notInTheBackup", v));
}

TEST_F(BackupRestoreTest, LeavesKeysThatAreNotListedAlone)
{
	ASSERT_TRUE(TempDir::write(m_keylist, R"(["timeZone"])"));
	writeBackup({{"timeZone", "backup-tz"},
	             {"developerMode", "true"},
	             {"x_webos_security_policy", "off"},
	             {"databaseVersion", "99.0"}});

	EXPECT_EQ(1, restore());
	EXPECT_EQ("backup-tz", m_live->getPref("timeZone"));
	// a key the live db has keeps its value, one it lacks stays absent
	EXPECT_EQ("false", m_live->getPref("developerMode"));
	std::string v;
	EXPECT_FALSE(m_live->getPref("x_webos_security_policy", v));
	EXPECT_NE("99.0", m_live->getPref("databaseVersion"));
}

TEST_F(BackupRestoreTest, KeyNamesAreData)
{
	ASSERT_TRUE(TempDir::write(m_keylist, "[\"x'); DROP TABLE Preferences; --\", \"x' OR '1'='1\"]"));
	writeBackup({{kInjection, "injected-key-value"}, {"developerMode", "true"}, {"other", "1"}});

	EXPECT_EQ(1, restore());
	EXPECT_EQ("injected-key-value", m_live->getPref(kInjection));
	// the widening key matched nothing, and the table is still there
	std::string v;
	EXPECT_FALSE(m_live->getPref(kWidening, v));
	EXPECT_FALSE(m_live->getPref("other", v));
	EXPECT_EQ("false", m_live->getPref("developerMode"));
	EXPECT_EQ("live-tz", m_live->getPref("timeZone"));
}

TEST_F(BackupRestoreTest, AMissingKeylistRestoresNothing)
{
	writeBackup({{"timeZone", "backup-tz"}});

	EXPECT_EQ(-1, restore());
	EXPECT_EQ("live-tz", m_live->getPref("timeZone"));
}

TEST_F(BackupRestoreTest, AKeylistThatIsNotAnArrayRestoresNothing)
{
	writeBackup({{"timeZone", "backup-tz"}});

	for (const char* content : {R"({"timeZone": true})", "timeZone", "", "[\"timeZone\""}) {
		ASSERT_TRUE(TempDir::write(m_keylist, content));
		EXPECT_EQ(-1, restore()) << content;
		EXPECT_EQ("live-tz", m_live->getPref("timeZone")) << content;
	}

	ASSERT_TRUE(TempDir::write(m_keylist, "[]"));
	EXPECT_EQ(0, restore());
	EXPECT_EQ("live-tz", m_live->getPref("timeZone"));
}

TEST_F(BackupRestoreTest, KeylistEntriesThatAreNotStringsAreSkipped)
{
	ASSERT_TRUE(TempDir::write(m_keylist, R"([1, null, {"timeZone": 1}, ["developerMode"], "", "useNetworkTime"])"));
	writeBackup({{"timeZone", "backup-tz"}, {"developerMode", "true"}, {"useNetworkTime", "true"}});

	std::list<std::string> keys;
	EXPECT_TRUE(BackupManager::readBackupKeylist(m_keylist, keys));
	EXPECT_EQ(std::list<std::string>{"useNetworkTime"}, keys);

	EXPECT_EQ(1, restore());
	EXPECT_EQ("live-tz", m_live->getPref("timeZone"));
	EXPECT_EQ("false", m_live->getPref("developerMode"));
	EXPECT_EQ("true", m_live->getPref("useNetworkTime"));
}

TEST_F(BackupRestoreTest, AValueTheFilterRefusesIsNotRestored)
{
	ASSERT_TRUE(TempDir::write(m_keylist, R"(["timeZone", "useNetworkTime"])"));
	writeBackup({{"timeZone", "backup-tz"}, {"useNetworkTime", "true"}});

	std::list<std::string> asked;
	EXPECT_EQ(1, restore([&asked](const std::string& key, const std::string& value) {
		asked.push_back(key + "=" + value);
		return key != "timeZone";
	}));
	EXPECT_EQ((std::list<std::string>{"timeZone=backup-tz", "useNetworkTime=true"}), asked);
	EXPECT_EQ("live-tz", m_live->getPref("timeZone"));
	EXPECT_EQ("true", m_live->getPref("useNetworkTime"));
}

TEST_F(BackupRestoreTest, TheBackupFileIsOnlyRead)
{
	ASSERT_TRUE(TempDir::write(m_keylist, R"(["timeZone"])"));
	// no databaseVersion row: opening this as a PrefsDb would drop and
	// recreate its table
	writeRawDb(m_backupFile, "CREATE TABLE Preferences (key TEXT, value TEXT);"
	                         "INSERT INTO Preferences VALUES ('timeZone', 'backup-tz');");
	const std::string before = TempDir::contents(m_backupFile);
	ASSERT_FALSE(before.empty());

	EXPECT_EQ(1, restore());
	EXPECT_EQ("backup-tz", m_live->getPref("timeZone"));
	EXPECT_EQ(before, TempDir::contents(m_backupFile));
}

TEST_F(BackupRestoreTest, ADatabaseThatFailsTheQuickCheckIsRefused)
{
	ASSERT_TRUE(TempDir::write(m_keylist, R"(["timeZone"])"));
	writeBackup({{"timeZone", "backup-tz"}});

	// one more page than the b-trees use: the rows still read back, but
	// sqlite's quick check reports the page as never used
	std::string db = TempDir::contents(m_backupFile);
	ASSERT_GE(db.size(), 100u);
	const size_t pageSize = (static_cast<unsigned char>(db[16]) << 8) | static_cast<unsigned char>(db[17]);
	ASSERT_EQ(0u, db.size() % pageSize);
	const uint32_t pages = static_cast<uint32_t>(db.size() / pageSize) + 1;
	for (int i = 0; i < 4; ++i)
		db[28 + i] = static_cast<char>((pages >> (8 * (3 - i))) & 0xff);
	db.append(pageSize, '\0');
	ASSERT_TRUE(TempDir::write(m_backupFile, db));

	EXPECT_EQ(-1, restore());
	EXPECT_EQ("live-tz", m_live->getPref("timeZone"));
}

TEST_F(BackupRestoreTest, APreferencesViewIsRefused)
{
	ASSERT_TRUE(TempDir::write(m_keylist, R"(["timeZone"])"));
	writeRawDb(m_backupFile, "CREATE TABLE t (key TEXT, value TEXT);"
	                         "INSERT INTO t VALUES ('timeZone', 'from-a-view');"
	                         "CREATE VIEW Preferences AS SELECT key, value FROM t;");

	EXPECT_EQ(-1, restore());
	EXPECT_EQ("live-tz", m_live->getPref("timeZone"));
}

TEST_F(BackupRestoreTest, AFileThatIsNotADatabaseIsRefused)
{
	ASSERT_TRUE(TempDir::write(m_keylist, R"(["timeZone"])"));
	ASSERT_TRUE(TempDir::write(m_backupFile, "SQLite format 3 - or not"));
	EXPECT_EQ(-1, restore());

	ASSERT_EQ(0, g_unlink(m_backupFile.c_str()));
	EXPECT_EQ(-1, restore());
	// and nothing was created in its place
	EXPECT_FALSE(g_file_test(m_backupFile.c_str(), G_FILE_TEST_EXISTS));
	EXPECT_EQ("live-tz", m_live->getPref("timeZone"));
}

TEST_F(BackupRestoreTest, RestoreKeysDoesNotFollowASymlink)
{
	ASSERT_TRUE(TempDir::write(m_keylist, R"(["timeZone"])"));
	const std::string elsewhere = m_dir.file("elsewhere.db");
	{
		std::unique_ptr<PrefsDb> other(PrefsDb::createStandalone(elsewhere));
		ASSERT_NE(nullptr, other);
		ASSERT_TRUE(other->setPref("timeZone", "via-symlink"));
	}
	ASSERT_EQ(0, symlink(elsewhere.c_str(), m_backupFile.c_str()));

	EXPECT_EQ(-1, restore());
	EXPECT_EQ("live-tz", m_live->getPref("timeZone"));
}
