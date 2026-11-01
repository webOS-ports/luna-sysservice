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


#ifndef PREFSDB_H
#define PREFSDB_H

#include <functional>
#include <string>
#include <map>
#include <list>

#include <sqlite3.h>

#include "Singleton.h"

class BackupManager;

class PrefsDb : public Singleton<PrefsDb>
{
	friend class BackupManager;			//because it operates on db directly
	friend class Singleton<PrefsDb>;

public:
	static PrefsDb* createStandalone(const std::string& dbFilename,bool deleteExisting=true);

	bool setPref(const std::string& key, const std::string& value);

	std::string getPref(const std::string& key);
	bool getPref(const std::string& key,std::string& r_val);

	std::map<std::string, std::string> getPrefs(const std::list<std::string>& keys);	
	std::map<std::string,std::string> getAllPrefs();

	// merge() copies every row of the other database, so it is only for a
	// database this service wrote itself. A backup is restored with
	// restoreKeys().
	int merge(const PrefsDb * p_sourceDb,bool overwriteSameKeys=true);
	int merge(const std::string& sourceDbFilename,bool overwriteSameKeys=true);

	int copyKeys(PrefsDb * p_sourceDb,const std::list<std::string>& keys,bool overwriteSameKeys=true);

	/// Asked for each (key, value) a restore found; false skips the key.
	typedef std::function<bool(const std::string& key, const std::string& value)> RestoreFilter;

	/// Copies the listed keys from an untrusted database file into this one,
	/// overwriting what is there. The file is opened read-only, a symlink is
	/// not followed, and its schema is not trusted (no views, triggers or
	/// unsafe functions). Every other row is ignored. Returns the number of
	/// keys stored, or -1 when the file cannot be read.
	int restoreKeys(const std::string& backupDbFilename, const std::list<std::string>& keys,
	                const RestoreFilter& accept = RestoreFilter());

	const std::string& databaseFile() const
	{ return m_dbFilename; }

	//keeping all this in one place so that all of system service has one place to look it up in, rather than all over the other source files
	static const char* s_defaultPrefsFile;
	static const char* s_defaultPlatformPrefsFile;
	static const char* s_customizationOverridePrefsFile;
	static const char* s_custCareNumberFile;
	static const char* s_prefsDbPath;
	static const char* s_tempBackupDbFilenameOnly;
	static const char* s_prefsPath;
	static const char* s_logChannel;
	static const char* s_mediaPartitionPath;
	static const char* s_mediaPartitionWallpapersDir;
	static const char* s_mediaPartitionWallpaperThumbsDir;
	static const char* s_mediaPartitionRingtonesDir;
	static const char* s_mediaPartitionTempDir;
	static const char* s_sysserviceDir;
	static const char* s_systemTokenFileAndPath;
	static const char* s_volumeIconFile;
	static const char* s_volumeIconFileAndPathSrc;
	static const char* s_volumeIconFileAndPathDest;
	static const char* s_sysDefaultWallpaperKey;
	static const char* s_sysDefaultRingtoneKey;

	~PrefsDb() override;
private:
	PrefsDb();
	explicit PrefsDb(const std::string& standaloneDbFilename);

	void openPrefsDb();
	void closePrefsDb();

	bool checkTableConsistency();
	bool integrityCheckDb();
	void loadDefaultPrefs();
	void loadDefaultPlatformPrefs();
	void backupDefaultPrefs();

	void synchronizeDefaults();
	void synchronizePlatformDefaults();
	void synchronizeCustomerCareInfo();
	
	void updateWithCustomizationPrefOverrides();

	// MUST RUN sqlite3_finalize(x);  on return value 'x' from runSqlQuery(..) unless x == 0
	sqlite3_stmt* runSqlQuery(const std::string& queryStr);
	// (___Command is the same except does an sql exec)
	bool runSqlCommand(const std::string& cmdStr);

private:
	sqlite3* m_prefsDb;
	bool m_standalone;
	std::string m_dbFilename;
	bool m_deleteOnDestroy;
};

#endif /* PREFSDB_H */
