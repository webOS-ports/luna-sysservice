// Copyright (c) 2010-2018 LG Electronics, Inc.
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

#ifndef BACKUP_MANAGER_H
#define BACKUP_MANAGER_H

#include <functional>
#include <list>
#include <string>
#include <memory>
#include <vector>

#include "Singleton.h"
#include "TimeChangeLaunch.h"

#include <luna-service2/lunaservice.h>

class PrefsDb;

class BackupManager :  public Singleton<BackupManager>
{
	friend class Singleton<BackupManager>;

public:
	static void setServiceHandle(LSHandle* serviceHandle);

	void turnOffBackup() { m_doBackupFiles = false; }
	void turnOnBackup() { m_doBackupFiles = true; }
	bool isBackupOn() const { return m_doBackupFiles; }

	/// The keys a backup holds: the strings in the json array in
	/// keylistFile (other entries are skipped). false, with keys empty, when
	/// the file is missing or does not hold an array.
	static bool readBackupKeylist(const std::string& keylistFile, std::list<std::string>& keys);

	/// The backup database a postRestore "files" entry names, as a real path
	/// under tempDir, or "" when the entry is not one to restore from: its
	/// name is not exactly the backup db's, it has a ".." or a NUL, it is
	/// not under tempDir once resolved, or it is not a regular file (a
	/// symlink is refused). A relative entry is taken relative to tempDir.
	static std::string backupDbPath(const std::string& tempDir, const std::string& entry);

	/// Restores into target the keys listed in keylistFile - the same list
	/// preBackup copies - from the backup database at backupDb. Nothing is
	/// restored when the list cannot be read. Returns the number of keys
	/// restored, or -1 when the list or the database cannot be read.
	static int restoreFromBackupDb(PrefsDb& target, const std::string& backupDb,
	                               const std::string& keylistFile,
	                               const std::function<bool(const std::string&, const std::string&)>& accept =
	                                   std::function<bool(const std::string&, const std::string&)>());

	/// The filter postRestore passes to restoreFromBackupDb(): a value has
	/// to pass its key's handler, as with setPreferences. timeChangeLaunch is
	/// never stored as it is: its valid entries (TimeChangeLaunch::parse) are
	/// held back for restoreTimeChangeLaunch().
	static bool acceptRestoredPref(const std::string& key, const std::string& value);

	/// Adds the held-back timeChangeLaunch entries whose app is installed in
	/// one of appDirs to db, through TimeChangeLaunch::add() - what
	/// setTimeChangeLaunch does. The others stay held back, or, when final
	/// (restoreFinished), are dropped with a log line.
	void restoreTimeChangeLaunch(PrefsDb& db, const std::vector<std::string>& appDirs, bool final);

	/// The timeChangeLaunch entries held back now.
	const std::vector<TimeChangeLaunch::Entry>& pendingTimeChangeLaunch() const { return m_pendingTimeChangeLaunch; }

private:
	BackupManager();

	static LSMethod	s_BackupServerMethods[];

	bool	m_doBackupFiles;

	std::list<std::string>	m_backupFiles;	///< List of items managing the backup/restore of.

	std::unique_ptr<PrefsDb> m_p_backupDb;

	/// timeChangeLaunch entries from the last postRestore whose app was not
	/// installed yet; retried, then dropped, on restoreFinished
	std::vector<TimeChangeLaunch::Entry> m_pendingTimeChangeLaunch;

	void copyKeysToBackupDb();
	void initFilesForBackup(bool useFullPath);

	static bool preBackupCallback( LSHandle* lshandle, LSMessage *message, void *user_data);
	static bool postRestoreCallback( LSHandle* lshandle, LSMessage *message, void *user_data);
	static bool restoreFinishedCallback( LSHandle* lshandle, LSMessage *message, void *user_data);

	/// sendPreBackupResponse - made to take a fileList param so as to allow m_backupFiles to be unmodded in case
	// 							a temporary alternate list is to be used; this gives max flexibility
	void sendPreBackupResponse(LSHandle* lshandle, LSMessage *message,const std::list<std::string>& fileList) const;
	static void sendPostRestoreResponse(LSHandle* lshandle, LSMessage *message);


	static std::string s_backupKeylistFilename;
};

#endif // BACKUP_MANAGER_H
