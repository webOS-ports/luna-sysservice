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


#ifndef UTILS_H
#define UTILS_H

#undef min
#undef max

#include <sstream>
#include <iostream>
#include <list>
#include <string>
#include <vector>
#include <memory>

#include <glib.h>
#include <pbnjson.hpp>
#include <luna-service2/lunaservice.h>

namespace Utils {

class gstring {
public:
	explicit gstring(gchar *str): m_ptr(str, g_free) {}
	const gchar *get() { return m_ptr.get(); }

private:
	std::unique_ptr<gchar, void(*)(void *)> m_ptr;
};

template <class T>
std::string toSTLString(const T &arg) {
	std::ostringstream	out;
	out << arg;
	return(out.str());
}

#define LSREPORT(lse) PmLogCritical(sysServiceLogContext(), "LS2_ERROR", 0, "LS2 Error: %s => %s", (lse).func, (lse).message )


std::string trimWhitespace(const std::string& s,const std::string& drop = "\r\n\t ");
void trimWhitespace_inplace(std::string& s_mod,const std::string& drop = "\r\n\t ");

int splitFileAndPath(const std::string& srcPathAndFile,std::string& pathPart,std::string& filePart);
int splitStringOnKey(std::vector<std::string>& returnSplitSubstrings,const std::string& baseStr,const std::string& delims);
int splitStringOnKey(std::list<std::string>& returnSplitSubstrings,const std::string& baseStr,const std::string& delims);

bool doesExistOnFilesystem(const char * pathAndFile);
int fileCopy(const char * srcFileAndPath,const char * dstFileAndPath);

long long filesizeOnFilesystem(const char * pathAndFile);

int urlDecodeFilename(const std::string& encodedName,std::string& decodedName);
int urlEncodeFilename(std::string& encodedName,const std::string& decodedName);

bool extractFromJson(const std::string& jsonString, const std::string& key, std::string& r_value);
bool extractFromJson(const pbnjson::JValue &root, const std::string& key, std::string& r_value);

int createTempFile(const std::string& baseDir,const std::string& tag,const std::string& extension,std::string& r_fileAndPath);

}

#endif /* UTILS_H */
