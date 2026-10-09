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

#ifndef SYSTEMRESTORE_H
#define SYSTEMRESTORE_H

#include <string>

#include "Singleton.h"

struct LSHandle;
struct LSMessage;

/*
 * Relies COMPLETELY on PrefsDb having been initialized. Don't call anything in here before that
 * happens!
 */
class SystemRestore : public Singleton<SystemRestore>
{
	friend class Singleton<SystemRestore>;

public:
	static int startupConsistencyCheck();
	static int runtimeConsistencyCheck();
	static int createSpecialDirectories();
	
	int restoreDefaultRingtoneSetting();
	int restoreDefaultWallpaperSetting();

	static bool isRingtoneSettingConsistent();
	bool isWallpaperSettingConsistent();
	// The check isWallpaperSettingConsistent() makes on the stored value: a
	// JSON object whose "wallpaperFile" is an image that can be read.
	static bool isWallpaperPrefConsistent(const std::string& wallpaperRawPref);
	
	void refreshDefaultSettings();
	
	static bool msmAvailCallback(LSHandle* handle, LSMessage* message, void* ctxt);
	static bool msmProgressCallback(LSHandle* handle, LSMessage* message, void* ctxt);
	static bool msmEntryCallback(LSHandle* handle, LSMessage* message, void* ctxt);
	static bool msmFsckingCallback(LSHandle* handle, LSMessage* message, void* ctxt);
	static bool msmPartitionAvailCallback(LSHandle* handle, LSMessage* message, void* ctxt);

	/**
	 * Writes the current time to path, which is on the user-writable media
	 * partition: a symlink there is refused rather than followed. Returns
	 * whether the whole file was written.
	 */
	static bool writeTokenFile(const char* path);

protected:
	SystemRestore();

	//these defaults should be picked out of the defaults file when this object gets constructed
	//they are string-encoded Json objects, so they must be further parsed (this will be done by the restore__ functions)
	//refreshDefaultSettings() will also load them
	std::string defaultWallpaperString;
	std::string defaultRingtoneString;
	
	//stored when the above are parsed
	std::string defaultWallpaperFileAndPath;
	std::string defaultRingtoneFileAndPath;

	int restoreDefaultRingtoneToMediaPartition();
	int restoreDefaultWallpaperToMediaPartition();
	
	static bool msmAvail(LSMessage* message);
	static bool msmProgress(LSMessage* message);
	static bool msmEntry(LSMessage* message);
	static bool msmPartitionAvailable(LSMessage* message);
};

#endif //SYSTEMRESTORE_H
