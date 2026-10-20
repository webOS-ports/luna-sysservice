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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include "UrlRep.h"
#include "Utils.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace Utils
{

using namespace pbnjson;

std::string trimWhitespace(const std::string& s,const std::string& drop)
{
 std::string::size_type first = s.find_first_not_of( drop );
 std::string::size_type last  = s.find_last_not_of( drop );

 if( first == std::string::npos || last == std::string::npos ) return std::string( "" );
   return s.substr( first, last - first + 1 );
}

int splitFileAndPath(const std::string& srcPathAndFile,std::string& pathPart,std::string& filePart) {

	std::vector<std::string> parts;
	//printf("splitFileAndPath - input [%s]\n",srcPathAndFile.c_str());
	int s = splitStringOnKey(parts,srcPathAndFile,std::string("/"));
	if ((s == 1) && (srcPathAndFile.at(srcPathAndFile.length()-1) == '/')) {
		//only path part
		pathPart = srcPathAndFile;
		filePart = "";
	}
	else if (s == 1) {
		//only file part
		if (srcPathAndFile.at(0) == '/') {
			pathPart = "/";
		}
		else {
			pathPart = "";
		}
		filePart = parts.at(0);
	}
	else if (s >= 2) {
		for (int i=0;i<s-1;i++) {
			if ((parts.at(i)).size() == 0)
				continue;
			pathPart += std::string("/")+parts.at(i);
			//printf("splitFileAndPath - path is now [%s]\n",pathPart.c_str());
		}
		pathPart += std::string("/");
		filePart = parts.at(s-1);
	}

	return s;
}

int splitStringOnKey(std::vector<std::string>& returnSplitSubstrings,const std::string& baseStr,const std::string& delims) {

	const std::string base = trimWhitespace(baseStr);
	std::string::size_type start = 0;
	std::string::size_type mark = 0;

	int i=0;
	while (start < base.size()) {
		//find the start of a non-delims
		start = base.find_first_not_of(delims,mark);
		if (start == std::string::npos)
			break;
		//find the end of the current substring (where the next instance of delim lives, or end of the string)
		mark = base.find_first_of(delims,start);
		if (mark == std::string::npos)
			mark = base.size();

		const std::string extracted = base.substr(start,mark-start);
		if (extracted.size() > 0) {
			//valid string...add it
			returnSplitSubstrings.push_back(extracted);
			++i;
		}
		start=mark;
	}

	return i;
}

void trimWhitespace_inplace(std::string& s_mod,const std::string& drop)
{
 std::string::size_type first = s_mod.find_first_not_of( drop );
 std::string::size_type last  = s_mod.find_last_not_of( drop );

 if( first == std::string::npos || last == std::string::npos )
	 s_mod= std::string( "" );
 else
	 s_mod = s_mod.substr( first, last - first + 1 );
}

int splitStringOnKey(std::list<std::string>& returnSplitSubstrings,const std::string& baseStr,const std::string& delims) {

	const std::string base = trimWhitespace(baseStr);
	std::string::size_type start = 0;
	std::string::size_type mark = 0;

	int i=0;
	while (start < base.size()) {
		//find the start of a non-delims
		start = base.find_first_not_of(delims,mark);
		if (start == std::string::npos)
			break;
		//find the end of the current substring (where the next instance of delim lives, or end of the string)
		mark = base.find_first_of(delims,start);
		if (mark == std::string::npos)
			mark = base.size();

		const std::string extracted = base.substr(start,mark-start);
		if (extracted.size() > 0) {
			//valid string...add it
			returnSplitSubstrings.push_back(extracted);
			++i;
		}
		start=mark;
	}

	return i;
}

bool doesExistOnFilesystem(const char * pathAndFile) {

	if (pathAndFile == NULL)
		return false;

	struct stat buf;
	if (-1 == ::stat(pathAndFile, &buf ) )
		return false;
	return true;


}

int fileCopy(const char * srcFileAndPath,const char * dstFileAndPath)
{
	if ((srcFileAndPath == NULL) || (dstFileAndPath == NULL))
		return -1;

	// open the source first: opening the destination truncates it, which
	// must not happen when the source turns out to be missing.
	// Flawfinder: ignore - the source is the caller's choice, read only
	FILE * infp = fopen(srcFileAndPath,"rb");
	if (infp == NULL)
		return -1;
	// Destinations are on the user-writable media partition: never write
	// through a symlink planted there.
	// Flawfinder: ignore - O_NOFOLLOW
	int outfd = open(dstFileAndPath, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0666);
	FILE * outfp = (outfd == -1) ? NULL : fdopen(outfd, "wb");
	if (outfp == NULL) {
		if (outfd != -1)
			(void) close(outfd);
		(void) fclose(infp);
		return -1;
	}

	char buffer[2048];
	bool success=true;
	for (;;) {
		size_t r = fread(buffer, 1, sizeof(buffer), infp);
		if (r > 0 && fwrite(buffer, 1, r, outfp) != r) {
			success=false;
			break;
		}
		if (r < sizeof(buffer)) {	// end of file, or an error
			if (ferror(infp))
				success=false;
			break;
		}
	}

	(void) fclose(infp);	// read only: nothing to lose
	// a write error can surface only when the buffer is flushed
	if (fflush(outfp) != 0)	//apparently our filesystem doesn't like to commit even on close
		success=false;
	if (fclose(outfp) != 0)
		success=false;
	if (!success) // incomplete copy
	{
		(void) unlink(dstFileAndPath);
		return -1;
	}
	return 1;
}

long long filesizeOnFilesystem(const char * pathAndFile)
{
	if (pathAndFile == NULL)
		return 0;

	struct stat buf;
	if (-1 == ::stat(pathAndFile, &buf ) )
		return 0;
	return static_cast<long long>(buf.st_size);
}

//// wrappers for now, in case more processing/error checking is needed on the inp/outp values (probably should)
// return > 0 if coding was successful
int urlDecodeFilename(const std::string& encodedName,std::string& decodedName)
{
	decodedName = unescape(encodedName);
	return 1;
}

int urlEncodeFilename(std::string& encodedName,const std::string& decodedName)
{
	encodedName = escape(decodedName);
	return 1;
}

bool extractFromJson(const std::string& jsonString,const std::string& key,std::string& r_value)
{
	JValue root = JDomParser::fromString(jsonString);

	return root.isObject() ? extractFromJson(root, key, r_value) : false;
}

bool extractFromJson(const JValue &root,const std::string& key, std::string& r_value)
{
	JValue label = root[key.c_str()];
	if (!label.isString()) {
		return false;
	}

	r_value = label.asString();
	return true;
}

/*
 * returns 0 on error, >0 otherwise
 *
 * Dangerous on its own! Doesn't check for baseDir validity so could technically overwrite things in sensitive places
 * (though unlikely since the temp filenames wont be anything like other, vital files on the filesystem(
 */
int createTempFile(const std::string& baseDir,const std::string& tag,const std::string& extension,std::string& r_fileAndPath)
{

	std::string templateStr = baseDir + std::string("/file_")+std::string(tag)+std::string("_XXXXXX");
	std::vector<char> templateFileName(templateStr.begin(), templateStr.end());
	templateFileName.push_back('\0');
	// glibc's mkstemp creates the file 0600 and O_EXCL, whatever the umask
	int fd = mkstemp(templateFileName.data()); // Flawfinder: ignore
	templateStr = std::string(templateFileName.data());

	if (fd == -1) {
		return 0;
	}
	//write nothing and close, to assure file is on disk; this will prevent mkstemp from assigning the same name by chance (if the filename isn't used to create a file in the meantime)
	if (write (fd,"",0) == -1) {
		close(fd);
		return 0;
	}
	close(fd);

	r_fileAndPath = templateStr+extension;
	if (rename(templateStr.c_str(),r_fileAndPath.c_str()) == -1) {
		//error...unlink the base file
		unlink(templateStr.c_str());
		return 0;
	}
	return 1;
}

} //end Utils namespace

