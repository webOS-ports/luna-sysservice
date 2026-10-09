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

#include <cstring>
#include <list>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Utils.h"
#include "TempDir.h"

using namespace Utils;

TEST(UtilsTrim, DropsLeadingAndTrailingWhitespaceOnly)
{
	EXPECT_EQ("a b", trimWhitespace("  \t a b \r\n"));
	EXPECT_EQ("", trimWhitespace(" \t\r\n"));
	EXPECT_EQ("", trimWhitespace(""));
	EXPECT_EQ("x", trimWhitespace("--x--", "-"));

	std::string s = "\t value \n";
	trimWhitespace_inplace(s);
	EXPECT_EQ("value", s);
	s = "   ";
	trimWhitespace_inplace(s);
	EXPECT_EQ("", s);
}

TEST(UtilsSubstring, PicksTheNthField)
{
	std::string out;
	EXPECT_TRUE(getNthSubstring(2, out, "one two  three"));
	EXPECT_EQ("two", out);
	EXPECT_TRUE(getNthSubstring(3, out, "one two  three"));
	EXPECT_EQ("three", out);
	// 0 is taken as the first field
	EXPECT_TRUE(getNthSubstring(0, out, "  one two"));
	EXPECT_EQ("one", out);

	out = "untouched";
	EXPECT_FALSE(getNthSubstring(4, out, "one two  three"));
	EXPECT_FALSE(getNthSubstring(1, out, " \t "));
	EXPECT_EQ("untouched", out);
}

TEST(UtilsSplit, SkipsEmptyFieldsInVectorAndList)
{
	std::vector<std::string> v;
	EXPECT_EQ(3, splitStringOnKey(v, " a,,b ,c ", ","));
	ASSERT_EQ(3u, v.size());
	EXPECT_EQ("a", v[0]);
	EXPECT_EQ("b ", v[1]);
	EXPECT_EQ("c", v[2]);

	std::list<std::string> l;
	EXPECT_EQ(2, splitStringOnKey(l, "x;y;", ";"));
	EXPECT_EQ((std::list<std::string>{"x", "y"}), l);

	v.clear();
	EXPECT_EQ(0, splitStringOnKey(v, ",,,", ","));
	EXPECT_TRUE(v.empty());
}

TEST(UtilsSplit, FileAndPath)
{
	std::string path, file;
	EXPECT_EQ(3, splitFileAndPath("/usr/lib/file.txt", path, file));
	EXPECT_EQ("/usr/lib/", path);
	EXPECT_EQ("file.txt", file);

	path.clear(); file.clear();
	EXPECT_EQ(1, splitFileAndPath("/file", path, file));
	EXPECT_EQ("/", path);
	EXPECT_EQ("file", file);

	path.clear(); file.clear();
	EXPECT_EQ(1, splitFileAndPath("file", path, file));
	EXPECT_EQ("", path);
	EXPECT_EQ("file", file);

	path.clear(); file.clear();
	EXPECT_EQ(1, splitFileAndPath("dir/", path, file));
	EXPECT_EQ("dir/", path);
	EXPECT_EQ("", file);

	// nothing to split must not touch the outputs (or read past the string)
	path = "p"; file = "f";
	EXPECT_EQ(0, splitFileAndPath("", path, file));
	EXPECT_EQ("p", path);
	EXPECT_EQ("f", file);
}

TEST(UtilsSplit, FileAndExtension)
{
	std::string file, ext;
	EXPECT_EQ(3, splitFileAndExtension("archive.tar.gz", file, ext));
	EXPECT_EQ("archive.tar", file);
	EXPECT_EQ("gz", ext);

	file.clear(); ext.clear();
	EXPECT_EQ(1, splitFileAndExtension("noext", file, ext));
	EXPECT_EQ("noext", file);
	EXPECT_EQ("", ext);
}

static std::string b64(const std::string& s)
{
	return base64_encode(reinterpret_cast<const unsigned char*>(s.data()),
	                     static_cast<unsigned int>(s.size()));
}

// RFC 4648 section 10
TEST(UtilsBase64, EncodesTheRfc4648Vectors)
{
	EXPECT_EQ("", b64(""));
	EXPECT_EQ("Zg==", b64("f"));
	EXPECT_EQ("Zm8=", b64("fo"));
	EXPECT_EQ("Zm9v", b64("foo"));
	EXPECT_EQ("Zm9vYg==", b64("foob"));
	EXPECT_EQ("Zm9vYmE=", b64("fooba"));
	EXPECT_EQ("Zm9vYmFy", b64("foobar"));
}

TEST(UtilsBase64, DecodesTheRfc4648Vectors)
{
	EXPECT_EQ("", base64_decode(""));
	EXPECT_EQ("f", base64_decode("Zg=="));
	EXPECT_EQ("fo", base64_decode("Zm8="));
	EXPECT_EQ("foo", base64_decode("Zm9v"));
	EXPECT_EQ("foob", base64_decode("Zm9vYg=="));
	EXPECT_EQ("fooba", base64_decode("Zm9vYmE="));
	EXPECT_EQ("foobar", base64_decode("Zm9vYmFy"));
}

TEST(UtilsBase64, RoundTripsEveryByteValue)
{
	std::string all;
	for (int i = 0; i < 256; i++)
		all += static_cast<char>(i);
	for (size_t len = 0; len <= all.size(); len += 37)
		EXPECT_EQ(all.substr(0, len), base64_decode(b64(all.substr(0, len))));
	EXPECT_EQ(all, base64_decode(b64(all)));
}

TEST(UtilsBase64, DecodeStopsAtTheFirstForeignCharacter)
{
	EXPECT_EQ("foo", base64_decode("Zm9v!YmFy"));
	// bytes >= 0x80 are not base64 and must not be looked up as such
	EXPECT_EQ("foo", base64_decode("Zm9v\xc3\xa9YmFy"));
}

TEST(UtilsFiles, ReadFile)
{
	TempDir dir;
	ASSERT_FALSE(dir.path().empty());

	EXPECT_EQ(nullptr, readFile(nullptr));
	EXPECT_EQ(nullptr, readFile(dir.file("missing").c_str()));

	ASSERT_TRUE(TempDir::write(dir.file("empty"), ""));
	EXPECT_EQ(nullptr, readFile(dir.file("empty").c_str()));

	const std::string content = "line one\nline two\n";
	ASSERT_TRUE(TempDir::write(dir.file("text"), content));
	char *data = readFile(dir.file("text").c_str());
	ASSERT_NE(nullptr, data);
	EXPECT_EQ(content, std::string(data));
	EXPECT_EQ(content.size(), strlen(data));
	delete[] data;
}

TEST(UtilsFiles, FileCopyCopiesEverythingAndKeepsTheTargetOnAMissingSource)
{
	TempDir dir;
	ASSERT_FALSE(dir.path().empty());

	// more than fileCopy's 2 KiB buffer
	std::string content;
	for (int i = 0; i < 5000; i++)
		content += static_cast<char>('a' + i % 26);
	ASSERT_TRUE(TempDir::write(dir.file("src"), content));

	EXPECT_EQ(1, fileCopy(dir.file("src").c_str(), dir.file("dst").c_str()));
	EXPECT_EQ(content, TempDir::read(dir.file("dst")));
	EXPECT_EQ(5000, filesizeOnFilesystem(dir.file("dst").c_str()));

	ASSERT_TRUE(TempDir::write(dir.file("keep"), "precious"));
	EXPECT_EQ(-1, fileCopy(dir.file("missing").c_str(), dir.file("keep").c_str()));
	EXPECT_EQ("precious", TempDir::read(dir.file("keep")));

	EXPECT_EQ(-1, fileCopy(nullptr, dir.file("dst").c_str()));
	EXPECT_EQ(-1, fileCopy(dir.file("src").c_str(), nullptr));
}

TEST(UtilsFiles, ExistenceAndSize)
{
	TempDir dir;
	ASSERT_FALSE(dir.path().empty());
	ASSERT_TRUE(TempDir::write(dir.file("f"), "12345"));

	EXPECT_TRUE(doesExistOnFilesystem(dir.file("f").c_str()));
	EXPECT_FALSE(doesExistOnFilesystem(dir.file("nope").c_str()));
	EXPECT_FALSE(doesExistOnFilesystem(nullptr));
	EXPECT_EQ(5, filesizeOnFilesystem(dir.file("f").c_str()));
	EXPECT_EQ(0, filesizeOnFilesystem(dir.file("nope").c_str()));
	EXPECT_EQ(0, filesizeOnFilesystem(nullptr));
}

TEST(UtilsFiles, CreateTempFileMakesAnEmptyFileWithTheExtension)
{
	TempDir dir;
	ASSERT_FALSE(dir.path().empty());

	std::string a, b;
	ASSERT_EQ(1, createTempFile(dir.path(), "tag", ".jpg", a));
	ASSERT_EQ(1, createTempFile(dir.path(), "tag", ".jpg", b));
	EXPECT_NE(a, b);

	const std::string prefix = dir.path() + "/file_tag_";
	EXPECT_EQ(0u, a.compare(0, prefix.size(), prefix));
	ASSERT_GT(a.size(), 4u);
	EXPECT_EQ(".jpg", a.substr(a.size() - 4));
	EXPECT_TRUE(doesExistOnFilesystem(a.c_str()));
	EXPECT_EQ(0, filesizeOnFilesystem(a.c_str()));
	// the mkstemp name without the extension is not left behind
	EXPECT_FALSE(doesExistOnFilesystem(a.substr(0, a.size() - 4).c_str()));

	std::string c;
	EXPECT_EQ(0, createTempFile(dir.file("no/such/dir"), "tag", ".jpg", c));
}

TEST(UtilsFormat, AppendFormatHandlesShortAndLongOutput)
{
	std::string s = "x";
	append_format(s, "=%d,%s", 42, "y");
	EXPECT_EQ("x=42,y", s);

	// longer than the 1 KiB stack buffer
	const std::string longArg(3000, 'z');
	std::string l = "<";
	append_format(l, "%s>", longArg.c_str());
	EXPECT_EQ("<" + longArg + ">", l);

	std::string u = "same";
	append_format(u, nullptr);
	EXPECT_EQ("same", u);
}

TEST(UtilsFormat, StringToLower)
{
	std::string s = "HeLLo World 123";
	string_to_lower(s);
	EXPECT_EQ("hello world 123", s);
}

TEST(UtilsJson, ExtractFromJsonOnlyReturnsStrings)
{
	std::string v = "untouched";
	EXPECT_TRUE(extractFromJson(std::string("{\"a\":\"b\",\"n\":1}"), "a", v));
	EXPECT_EQ("b", v);

	v = "untouched";
	EXPECT_FALSE(extractFromJson(std::string("{\"a\":\"b\",\"n\":1}"), "n", v));
	EXPECT_FALSE(extractFromJson(std::string("{\"a\":\"b\"}"), "missing", v));
	EXPECT_FALSE(extractFromJson(std::string("[\"a\"]"), "a", v));
	EXPECT_FALSE(extractFromJson(std::string("not json"), "a", v));
	EXPECT_FALSE(extractFromJson(std::string(""), "a", v));
	EXPECT_EQ("untouched", v);
}

TEST(UtilsUrl, FilenameEncodingRoundTrips)
{
	std::string decoded;
	EXPECT_EQ(1, urlDecodeFilename("a%20b%2Fc", decoded));
	EXPECT_EQ("a b/c", decoded);

	const std::string name = "My file #1 (copy)&more.jpg";
	std::string encoded;
	EXPECT_EQ(1, urlEncodeFilename(encoded, name));
	EXPECT_EQ(std::string::npos, encoded.find(' '));
	EXPECT_EQ(std::string::npos, encoded.find('#'));
	EXPECT_EQ(1, urlDecodeFilename(encoded, decoded));
	EXPECT_EQ(name, decoded);
}
