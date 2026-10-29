// Copyright (c) 2010-2024 LG Electronics, Inc.
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

#include "BackupManager.h"

#include <cstdlib>
#include <memory>

#include <sys/stat.h>

#include <pbnjson.hpp>
#include <luna-service2++/error.hpp>

#include "PrefsDb.h"
#include "PrefsFactory.h"
#include "PrefsHandler.h"

#include "Logging.h"
#include "Utils.h"
#include "Settings.h"
#include "JSONUtils.h"
#include "TimeChangeLaunch.h"

using namespace pbnjson;

/* BackupManager implementation is based on the API documented at https://wiki.palm.com/display/ServicesEngineering/Backup+and+Restore+2.0+API
 * Backs up the systemprefs database
 */

std::string BackupManager::s_backupKeylistFilename = WEBOS_INSTALL_WEBOS_SYSCONFDIR "/sysservice-backupkeys.json";

/*!
 * \page com_palm_systemservice_backup Service API com.webos.service.systemservice/backup/
 *
 * Public methods:
 *
 * - \ref com_palm_systemservice_pre_backup
 * - \ref com_palm_systemservice_post_restore
 */

/**
 * These are the methods that the backup service can call when it's doing a
 * backup or restore.
 */
LSMethod BackupManager::s_BackupServerMethods[]  = {
	{ "preBackup"  , BackupManager::preBackupCallback, LUNA_METHOD_FLAGS_NONE },
	{ "postRestore", BackupManager::postRestoreCallback, LUNA_METHOD_FLAGS_NONE },
	{ "restoreFinished", BackupManager::restoreFinishedCallback, LUNA_METHOD_FLAGS_NONE },
	{ nullptr, nullptr, LUNA_METHOD_FLAGS_NONE }
};


BackupManager::BackupManager()
	: m_doBackupFiles(true)
	, m_p_backupDb(nullptr)
{
}

void BackupManager::setServiceHandle(LSHandle* serviceHandle)
{
	LS::Error error;
	if (!LSRegisterCategory(serviceHandle, "/backup", s_BackupServerMethods,
		nullptr, nullptr, error.get()))
	{
		PmLogCritical(sysServiceLogContext(), "LSREGISTER_CATEGORY_FAILED", 0, "Failed to register backup methods: %s", error.what());
	}
}

bool BackupManager::readBackupKeylist(const std::string& keylistFile, std::list<std::string>& keys)
{
	keys.clear();

	JValue backupKeysJson = JDomParser::fromFile(keylistFile.c_str());
	if (!backupKeysJson.isArray()) {
		PmLogWarning(sysServiceLogContext(), "STRING_KEY_NOT_EXIST", 0, "[%s] does not contain an array of string keys", keylistFile.c_str());
		return false;
	}

	for (const JValue key: backupKeysJson.items()) {
		if (!key.isString() || key.asString().empty()) {
			PmLogWarning(sysServiceLogContext(),"INVALID_KEY",0,"Invalid key (skipping)");
			continue;
		}

		keys.push_back(key.asString());
	}
	return true;
}

void BackupManager::copyKeysToBackupDb()
{
	if (!m_p_backupDb)
		return;

	//open the backup keys list to figure out what to copy
	std::list<std::string> keylist;
	if (!readBackupKeylist(BackupManager::s_backupKeylistFilename, keylist))
		return;

	m_p_backupDb->copyKeys(PrefsDb::instance(), keylist);
}

static const char s_timeChangeLaunchKey[] = "timeChangeLaunch";

namespace {

std::string realPath(const std::string& path)
{
	// realpath() allocates the result when given no buffer, so there is no
	// fixed-size buffer to overflow.
	std::unique_ptr<char, decltype(&free)> resolved(realpath(path.c_str(), nullptr), &free); // Flawfinder: ignore
	return resolved ? std::string(resolved.get()) : std::string();
}

} // namespace

std::string BackupManager::backupDbPath(const std::string& tempDir, const std::string& entry)
{
	// c_str() would cut a name at a NUL, so it would name another file than
	// the caller sent
	if (tempDir.empty() || tempDir[0] != '/' || tempDir.find('\0') != std::string::npos ||
	    entry.empty() || entry.find('\0') != std::string::npos)
		return std::string();

	const std::string path = (entry[0] == '/') ? entry : tempDir + "/" + entry;

	const std::string::size_type slash = path.rfind('/');
	const std::string base = path.substr(slash + 1);
	if (base != PrefsDb::s_tempBackupDbFilenameOnly)
		return std::string();

	// no ".." component anywhere, even where it would resolve back under tempDir
	std::string::size_type start = 0;
	while (start <= path.size()) {
		std::string::size_type end = path.find('/', start);
		if (end == std::string::npos)
			end = path.size();
		if (path.compare(start, end - start, "..") == 0)
			return std::string();
		start = end + 1;
	}

	const std::string dir = realPath(slash == 0 ? std::string("/") : path.substr(0, slash));
	std::string root = realPath(tempDir);
	if (dir.empty() || root.empty())
		return std::string();
	if (root.back() != '/')
		root += '/';
	if (dir + '/' != root && dir.compare(0, root.size(), root) != 0)
		return std::string();

	// the file itself must not be a symlink; the database is then opened
	// with SQLITE_OPEN_NOFOLLOW, so a symlink put there after this check is
	// refused as well
	std::string resolved = (dir == "/" ? dir : dir + "/") + base;
	struct stat st;
	if (lstat(resolved.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
		return std::string();

	return resolved;
}

int BackupManager::restoreFromBackupDb(PrefsDb& target, const std::string& backupDb,
                                       const std::string& keylistFile,
                                       const std::function<bool(const std::string&, const std::string&)>& accept)
{
	std::list<std::string> keys;
	if (!readBackupKeylist(keylistFile, keys)) {
		PmLogWarning(sysServiceLogContext(), "RESTORE_NO_KEYLIST", 0,
		             "cannot read the backup key list [%s]; nothing restored from [%s]",
		             keylistFile.c_str(), backupDb.c_str());
		return -1;
	}
	if (keys.empty()) {
		PmLogWarning(sysServiceLogContext(), "RESTORE_NO_KEYLIST", 0,
		             "the backup key list [%s] is empty; nothing restored", keylistFile.c_str());
		return 0;
	}
	return target.restoreKeys(backupDb, keys, accept);
}

// A restored value has to pass the same check setPreferences applies: the
// key's handler validates it, as if an app had sent it (non-empty origin).
// A key without a handler is stored as it is, as setPreferences does.
// timeChangeLaunch, which setPreferences never accepts, is not stored here:
// its entries are checked one by one and added once their app is installed
// (restoreTimeChangeLaunch()).
bool BackupManager::acceptRestoredPref(const std::string& key, const std::string& value)
{
	if (key == s_timeChangeLaunchKey) {
		BackupManager::instance()->m_pendingTimeChangeLaunch = TimeChangeLaunch::parse(value);
		return false;
	}

	auto handler = PrefsFactory::instance()->getPrefsHandler(key);
	if (!handler)
		return true;
	return handler->validate(key, PrefsFactory::valueFromDbString(value), "backup/postRestore");
}

void BackupManager::initFilesForBackup(bool useFullPath)
{
	// rebuild the list from scratch - preBackup can be called repeatedly and
	// entries must not accumulate across calls
	m_backupFiles.clear();

	if (m_p_backupDb)
	{
		if (g_file_test(m_p_backupDb->databaseFile().c_str(), G_FILE_TEST_EXISTS))
		{
			if (useFullPath)
			{
				m_backupFiles.push_back(m_p_backupDb->m_dbFilename.c_str());
			}
			else
			{
				Utils::gstring base {g_path_get_basename(m_p_backupDb->databaseFile().c_str())};
				std::string filename = base.get() ? std::string(base.get()) : std::string();
				if (filename.find('/') != std::string::npos)
					filename = std::string("");			///all for safety
				m_backupFiles.push_back(filename);
			}

			if (Settings::instance()->m_saveLastBackedUpTempDb)
			{
				Utils::fileCopy(m_p_backupDb->databaseFile().c_str(),
						(std::string(PrefsDb::s_mediaPartitionPath)+std::string(PrefsDb::s_sysserviceDir)+std::string("/lastBackedUpTempDb.db")).c_str());
			}
		}
	}
}

/*! \page com_palm_systemservice_backup
\n
\section com_palm_systemservice_pre_backup preBackup

\e Public.

com.webos.service.systemservice/backup/preBackup

Make a backup of LunaSysService preferences.

\subsection com_palm_systemservice_pre_backup_syntax Syntax:
\code
{
	"incrementalKey": object,
	"maxTempBytes":   int,
	"tempDir":        string
}
\endcode

\param incrementalKey This is used primarily for mojodb, backup service will handle other incremental backups.
\param maxTempBytes The allowed size of upload, currently 10MB (more than enough for our backups).
\param tempDir Directory to store temporarily generated files.

\subsection com_palm_systemservice_pre_backup_returns Returns:
\code
{
	"description": string,
	"version": string,
	"files": string array
}
\endcode

\param description Describes the backup.
\param version Version of the backup.
\param files List of files included in the backup.

\subsection com_palm_systemservice_pre_backup_examples Examples:
\code
luna-send -n 1 -f luna://com.webos.service.systemservice/backup/preBackup '{}'
\endcode

Example response for a succesful call:
\code
{
	"description": "Backup of LunaSysService, containing the systemprefs sqlite3 database",
	"version": "1.0",
	"files": [
		"\/var\/luna\/preferences\/systemprefs_backup.db"
	]
}
\endcode
*/
/**
 * Called by the backup service for all four of our callback functions: preBackup,
 * postBackup, preRestore, postRestore.
 */
bool BackupManager::preBackupCallback( LSHandle* lshandle, LSMessage *message, void * /*user_data*/)
{
	PMLOG_TRACE("%s:starting",__FUNCTION__);
	if (LSMessageIsHubErrorMessage(message)) {  // returns false if message is NULL
		PmLogWarning(sysServiceLogContext(),"HUB_ERROR_MESSAGE",0,"The message received is an error message from the hub");
		return true;
	}

	// payload is expected to have the following fields -
	// incrementalKey - this is used primarily for mojodb, backup service will handle other incremental backups
	// maxTempBytes - this is the allowed size of upload, currently 10MB (more than enough for our backups)
	// tempDir - directory to store temporarily generated files
	LSMessageJsonParser parser(message, RELAXED_SCHEMA(PROPS_1(PROPERTY(tempDir, string))));

	if (!parser.parse(__FUNCTION__, lshandle, EValidateAndErrorAlways))
		return true;

	//grab the temp dir
	JValue tempDirLabel = parser.get()["tempDir"];

	std::string tempDir;
	bool myTmp = false;
	if (tempDirLabel.isValid())
	{
		tempDir = tempDirLabel.isString() ? tempDirLabel.asString() : "";
	}
	else
	{
		PmLogDebug(sysServiceLogContext(),"No tempDir specified in preBackup message");
		tempDir = PrefsDb::s_prefsPath;
		myTmp = true;
	}

	BackupManager *self = BackupManager::instance();

	// try and create it
	std::string dbfile = tempDir;
	if (dbfile.empty() || *dbfile.rbegin() != '/')
		dbfile += '/';
	dbfile += PrefsDb::s_tempBackupDbFilenameOnly;

	self->m_p_backupDb.reset(PrefsDb::createStandalone(dbfile));
	if (!self->m_p_backupDb)
	{
		//failed to create temp db
		PmLogWarning(sysServiceLogContext(),"DB_ERROR",0,"unable to create a temporary backup db at [%s]...aborting!",dbfile.c_str());
		self->sendPreBackupResponse(lshandle,message,std::list<std::string>());
		return true;
	}

	// Attempt to copy relevant keys into the temporary backup database
	self->copyKeysToBackupDb();
	// adding the files for backup at the time of request.
	self->initFilesForBackup(myTmp);

	if (!self->m_doBackupFiles)
	{
		PmLogWarning(sysServiceLogContext(),"NO_BACKUP",0,"opted not to do a backup at this time due to doBackup internal var");
		self->sendPreBackupResponse(lshandle,message,std::list<std::string>());
		return true;
	}

	self->sendPreBackupResponse(lshandle, message, self->m_backupFiles);
	return true;
}

/*! \page com_palm_systemservice_backup
\n
\section com_palm_systemservice_post_restore postRestore

\e Public.

com.webos.service.systemservice/backup/postRestore

Restore a LunaSysService backup.

\subsection com_palm_systemservice_post_restore_syntax Syntax:
\code
{
	"tempDir": string,
	"files":   string array
}
\endcode

\param tempDir Directory to store temporarily generated files. Required.
\param files List of files to restore. Required. Only the backup database
       preBackup wrote is read: an entry named exactly systemprefs_backup.db,
       relative to tempDir or an absolute path under it, with no "..", and
       not a symlink. Only the keys listed in sysservice-backupkeys.json
       (the keys preBackup copies) are restored from it, and each value has
       to pass the same handler check as setPreferences.

\subsection com_palm_systemservice_post_restore_returns Returns:
\code
{
	"returnValue": boolean
}
\endcode

\param returnValue Indicates if the call was succesful.

\subsection com_palm_systemservice_post_restore_examples Examples:
\code
luna-send -n 1 -f luna://com.webos.service.systemservice/backup/postRestore '{"tempDir": "/var/luna/preferences/restore", "files": ["systemprefs_backup.db"] }'
\endcode

Example response for a succesful call:
\code
{
	"returnValue": true
}
\endcode
*/
bool BackupManager::postRestoreCallback( LSHandle* lshandle, LSMessage *message, void * /*user_data*/)
{
	// {"tempDir": string, "files": array}
	LSMessageJsonParser parser(message, STRICT_SCHEMA(PROPS_2(PROPERTY(tempDir, string),
															  PROPERTY(files, array))
													  REQUIRED_2(tempDir, files)));

	if (!parser.parse(__FUNCTION__, lshandle, EValidateAndErrorAlways))
		return true;

	JValue root = parser.get();

	std::string tempDir = root["tempDir"].asString();
	JValue files = root["files"];

	// a new restore starts with nothing pending from an earlier one
	BackupManager::instance()->m_pendingTimeChangeLaunch.clear();

	for (const JValue &file: files.items())
	{
		std::string path = file.isString() ? file.asString() : "";
		PmLogDebug(sysServiceLogContext(),"array file: %s", path.c_str());

		if (path.empty())
		{
			PmLogWarning(sysServiceLogContext(),"FILE_PATH_EMPTY",0,"array object is a file path that is empty (skipping)");
			continue;
		}
		// only the backup db preBackup wrote, by its exact name, under tempDir
		std::string dbPath = backupDbPath(tempDir, path);
		if (dbPath.empty())
		{
			PmLogWarning(sysServiceLogContext(),"RESTORE_FILE_IGNORED",0,"[%s] is not %s under tempDir [%s] (skipping)",
			             path.c_str(), PrefsDb::s_tempBackupDbFilenameOnly, tempDir.c_str());
			continue;
		}

		if (Settings::instance()->m_saveLastBackedUpTempDb)
		{
			std::string lastRestored = PrefsDb::s_mediaPartitionPath;
			lastRestored += PrefsDb::s_sysserviceDir;
			lastRestored += "/lastRestoredTempDb.db";
			Utils::fileCopy(dbPath.c_str(), lastRestored.c_str());
		}

		// only the keys a backup holds, never the whole file
		int rc = restoreFromBackupDb(*PrefsDb::instance(), dbPath, s_backupKeylistFilename, BackupManager::acceptRestoredPref);
		if (rc <= 0)
		{
			PmLogWarning(sysServiceLogContext(),"ERROR_OR_EMPTY_BACKUP",0,"nothing restored from [%s]...could be an error or just an empty backup db",dbPath.c_str());
		}
	}

	// if for whatever reason the main db got closed, reopen it (the function will act ok if already open)
	PrefsDb::instance()->openPrefsDb();

	// timeChangeLaunch entries for apps that are installed now; the others
	// wait for restoreFinished, as the backup service reinstalls apps after
	// postRestore
	BackupManager::instance()->restoreTimeChangeLaunch(*PrefsDb::instance(), TimeChangeLaunch::defaultAppDirs(), false);

	//now refresh all the keys
	PrefsFactory::instance()->refreshAllKeys();

	sendPostRestoreResponse(lshandle,message);
	return true;
}

void BackupManager::restoreTimeChangeLaunch(PrefsDb& db, const std::vector<std::string>& appDirs, bool final)
{
	if (m_pendingTimeChangeLaunch.empty())
		return;

	m_pendingTimeChangeLaunch = TimeChangeLaunch::addInstalled(db, m_pendingTimeChangeLaunch, appDirs);
	if (!final) {
		if (!m_pendingTimeChangeLaunch.empty())
			PmLogInfo(sysServiceLogContext(), "TIME_CHANGE_LAUNCH_PENDING", 0,
			          "%zu timeChangeLaunch entries wait for their app to be reinstalled (restoreFinished)",
			          m_pendingTimeChangeLaunch.size());
		return;
	}

	for (const TimeChangeLaunch::Entry& entry : m_pendingTimeChangeLaunch)
		PmLogWarning(sysServiceLogContext(), "TIME_CHANGE_LAUNCH_ENTRY_DROPPED", 0,
		             "%s is not installed after the restore; its timeChangeLaunch entry is dropped",
		             entry.appId.c_str());
	m_pendingTimeChangeLaunch.clear();
}

/*! \page com_palm_systemservice_backup
\n
\section com_palm_systemservice_restore_finished restoreFinished

\e Public.

com.webos.service.systemservice/backup/restoreFinished

Called by the backup service when a restore is complete, after it has
reinstalled apps. Restores the timeChangeLaunch entries postRestore held back
because their app was not installed yet; entries whose app is still not
installed are dropped.

\subsection com_palm_systemservice_restore_finished_syntax Syntax:
\code
{
}
\endcode

\subsection com_palm_systemservice_restore_finished_returns Returns:
\code
{
	"returnValue": boolean
}
\endcode
*/
bool BackupManager::restoreFinishedCallback(LSHandle* lshandle, LSMessage *message, void * /*user_data*/)
{
	LSMessageJsonParser parser(message, RELAXED_SCHEMA(""));
	if (!parser.parse(__FUNCTION__, lshandle, EValidateAndErrorAlways))
		return true;

	BackupManager::instance()->restoreTimeChangeLaunch(*PrefsDb::instance(), TimeChangeLaunch::defaultAppDirs(), true);
	PrefsFactory::instance()->refreshAllKeys();

	sendPostRestoreResponse(lshandle, message);
	return true;
}

void BackupManager::sendPreBackupResponse(LSHandle* lshandle, LSMessage *message,
										  const std::list<std::string>& fileList) const
{
	std::string versionDb = PrefsDb::instance()->getPref("databaseVersion");
	if (versionDb.empty())
		versionDb = "0.0"; //signifies a problem

	// the response has to contain
	// description - what is being backed up
	// files - array of files to be backed up
	// version - version of the service
	JObject response {{"description", "Backup of LunaSysService, containing the systemprefs sqlite3 database"},
					  {"version", versionDb}};

	JArray files;
	if (m_doBackupFiles)
	{
		for (const std::string &file: fileList) {
			files.append(file);
			PMLOG_TRACE("added file %s to the backup list", file.c_str());
		}
	}
	else
	{
		PmLogWarning(sysServiceLogContext(),"NO_BACKUP",0,"opted not to do a backup at this time due to doBackup internal var");
	}
	response.put("files", files);

	PmLogDebug(sysServiceLogContext(),"Sending response to preBackupCallback: %s", response.stringify().c_str());

	LS::Error error;
	if (!LSMessageReply (lshandle, message, response.stringify().c_str(), error)) {
		PmLogWarning(sysServiceLogContext(),"PRE_BACKUP_CALLBACK_ERROR",0,"Can't send reply to preBackupCallback error: %s",error.what());
	}
}

void BackupManager::sendPostRestoreResponse(LSHandle* lshandle, LSMessage *message)
{
	PmLogDebug(sysServiceLogContext(),"Sending response to postRestoreCallback: %s", R"({"returnValue": true})");

	LS::Error error;
	if (!LSMessageReply (lshandle, message, R"({"returnValue": true})", error)) {
		PmLogWarning(sysServiceLogContext(),"POST_RESTORE_CALLBACK_ERROR",0,"Can't send reply to postRestoreCallback error: %s",error.what());
	}
}
