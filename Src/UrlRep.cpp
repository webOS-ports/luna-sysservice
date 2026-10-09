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


#include <uriparser/Uri.h> 
#include "UrlRep.h"

#include <vector>

static std::string URI_TEXT_RANGE_TO_STRING(const UriTextRangeA& textRange)
{
	return textRange.first ? std::string(textRange.first, textRange.afterLast - textRange.first)
	                       : std::string();
}

UrlRep UrlRep::fromUrl(const char* uri)
{
	UrlRep urlRep;

	if (!uri)
		return urlRep;
	
	UriParserStateA state;
	UriUriA uriA;
	UriQueryListA* queryList = NULL;
	int queryCount;

	state.uri = &uriA;
	if (uriParseUriA(&state, uri) != URI_SUCCESS) {
		// leave urlRep.valid == false so callers reject the input
		uriFreeUriMembersA(&uriA);
		return urlRep;
	}

	urlRep.query.clear();
	
	urlRep.scheme = URI_TEXT_RANGE_TO_STRING(uriA.scheme);
	urlRep.userInfo = URI_TEXT_RANGE_TO_STRING(uriA.userInfo);
	urlRep.host = URI_TEXT_RANGE_TO_STRING(uriA.hostText);
	urlRep.port = URI_TEXT_RANGE_TO_STRING(uriA.portText);
	urlRep.fragment = URI_TEXT_RANGE_TO_STRING(uriA.fragment);

	UriPathSegmentA* tmpPath = uriA.pathHead;
	while (tmpPath) {
		if (tmpPath == uriA.pathTail) {
			urlRep.pathOnly = urlRep.path;
		}
		urlRep.path += "/";
		urlRep.path += URI_TEXT_RANGE_TO_STRING(tmpPath->text);
		tmpPath = tmpPath->next;
	}

	if (uriA.pathTail) {
		urlRep.resource = URI_TEXT_RANGE_TO_STRING((uriA.pathTail)->text);
	}
	
	if (uriA.query.first) {
		if (uriDissectQueryMallocA(&queryList, &queryCount,
								   uriA.query.first,
								   uriA.query.afterLast) == URI_SUCCESS) {
			UriQueryListA* tmpQueryList = queryList;
			while (tmpQueryList) {
				if (tmpQueryList->key) {
					urlRep.query[tmpQueryList->key] = tmpQueryList->value ?
													  tmpQueryList->value : std::string();
				}
				tmpQueryList = tmpQueryList->next;
			}

			uriFreeQueryListA(queryList);
		}
	}
	
	uriFreeUriMembersA(&uriA);	       

	urlRep.resource = unescape(urlRep.resource);
	urlRep.path = unescape(urlRep.path);
	urlRep.pathOnly = unescape(urlRep.pathOnly);
	
	//TODO: maybe a more stringent check on validity???
	urlRep.valid = true;
	return urlRep;
}

std::string unescape(const std::string& encoded) {
	
	// up to the first NUL, as the C string always was
	const std::string text = encoded.substr(0, encoded.find('\0'));
	std::vector<char> buffer(text.begin(), text.end());
	buffer.push_back('\0');
	uriUnescapeInPlaceA(buffer.data());
	return std::string(buffer.data());
}

std::string escape(const std::string& source) {
	
	if (source.length() == 0)
		return std::string("");
	// uriEscapeA needs room for 6 output characters per input one
	std::vector<char> buffer(6 * source.length() + 1);
	const char * term = uriEscapeA(source.c_str(), buffer.data(), false, false);
	if (term == NULL)
		return std::string("");
	return std::string(buffer.data());
}

  

