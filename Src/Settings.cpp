// Copyright (c) 2010-2021 LG Electronics, Inc.
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

#include "Settings.h"

#include <memory>
#include <algorithm>

#include "Utils.h"

static const char* kSettingsFile = WEBOS_INSTALL_WEBOS_SYSCONFDIR "/sysservice.conf";
static const char* kSettingsFilePlatform = WEBOS_INSTALL_WEBOS_SYSCONFDIR "/sysservice-platform.conf";

Settings::Settings()
	: m_turnNovacomOnAtStartup(false)
	, m_saveLastBackedUpTempDb(false)
	, m_saveLastRestoredTempDb(false)
	, m_logLevel()
	, m_useComPalmImage2(false)
	, m_image2svcAvailable(false)
	, m_comPalmImage2BinaryFile("/usr/bin/acuteimaging")
	, schemaValidationOption(EIgnore)
	, switchTimezoneOnManualTime(false)
        , useLocalizedTZ(false)
{
	(void)load(kSettingsFile);
	(void)load(kSettingsFilePlatform);
}

namespace {

// Each reader leaves var as it is when the key is missing or malformed.
void readKey(GKeyFile* keyfile, const char* group, const char* key, std::string& var)
{
	GError* error = nullptr;
	gchar* value = g_key_file_get_string(keyfile, group, key, &error);
	if (!error && value)
		var = value;
	g_free(value);
	if (error)
		g_error_free(error);
}

void readKey(GKeyFile* keyfile, const char* group, const char* key, bool& var)
{
	GError* error = nullptr;
	gboolean value = g_key_file_get_boolean(keyfile, group, key, &error);
	if (!error)
		var = value;
	else
		g_error_free(error);
}

void readKey(GKeyFile* keyfile, const char* group, const char* key, ESchemaErrorOptions& var)
{
	GError* error = nullptr;
	int value = g_key_file_get_integer(keyfile, group, key, &error);
	if (error) {
		g_error_free(error);
		return;
	}
	(void)schemaErrorOptionFromInt(value, var);
}

} // namespace

bool schemaErrorOptionFromInt(int value, ESchemaErrorOptions& option)
{
	// Only a value the enum defines: anything else would be cast to an
	// enumerator that does not exist.
	if (value < EIgnore || value > EDefault)
		return false;
	option = static_cast<ESchemaErrorOptions>(value);
	return true;
}

bool Settings::load(const char* settingsFile)
{
	GKeyFile* keyfile;
	GKeyFileFlags flags;
	GError* error = 0;

	keyfile = g_key_file_new();
	if(!keyfile)
		return false;
	flags = GKeyFileFlags( G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS);

	if( !g_key_file_load_from_file( keyfile, settingsFile, flags, &error ) )
	{
		g_key_file_free( keyfile );
		if (error) g_error_free(error);
		return false;
	}

	readKey(keyfile, "Debug","turnOnNovacomAtStart",m_turnNovacomOnAtStartup);
	readKey(keyfile, "Debug","saveLastBackedUpTempDb",m_saveLastBackedUpTempDb);
	readKey(keyfile, "Debug","saveLastRestoredTempDb",m_saveLastRestoredTempDb);
	readKey(keyfile, "Debug","logLevel",m_logLevel);

	readKey(keyfile, "ImageService","useComPalmImage2",m_useComPalmImage2);
	readKey(keyfile, "ImageService","comPalmImage2Binary",m_comPalmImage2BinaryFile);

	readKey(keyfile, "General", "schemaValidationOption", schemaValidationOption);
	readKey(keyfile, "General", "switchTimezoneOnManualTime", switchTimezoneOnManualTime);

	g_key_file_free( keyfile );
	return true;
}

bool Settings::parseCommandlineOptions(int argc, char** argv)
{
	gchar* logLevelStr = nullptr;
	GError* err = nullptr;

	std::unique_ptr<GError*, void(*)(GError**)>
			error(&err, [](GError** ptr) { if (*ptr) g_error_free(*ptr); });
	std::unique_ptr<GOptionContext, void(*)(GOptionContext*)>
			context(g_option_context_new(nullptr), g_option_context_free);

	GOptionEntry entries[] = {
		{ "logger", 'l', 0, G_OPTION_ARG_STRING, static_cast<gpointer>(&logLevelStr), "log level", "level" },
		{ nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr }
	};

	g_option_context_add_main_entries(context.get(), entries, nullptr);
	if (!g_option_context_parse (context.get(), &argc, &argv, error.get()))
	{
		g_printerr("Error: %s\n", err ? err->message : "unknown");
		g_free(logLevelStr);
		return false;
	}

	m_logLevel = logLevelStr ? logLevelStr : "";
	g_free(logLevelStr);
	std::transform(m_logLevel.begin(), m_logLevel.end(), m_logLevel.begin(),
	               [](unsigned char c) { return static_cast<char>(::tolower(c)); });

	return true;
}
