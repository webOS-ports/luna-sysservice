// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
//
// Each rule in this directory must report exactly the lines marked
// "cocci-expect: <rule>" in this file and nothing else; static-analysis.sh
// checks that before it trusts a rule's silence on the real sources.

#include <cctype>
#include <cstdlib>
#include <string>

bool ctypeBad(char c)
{
	return isalnum(c); // cocci-expect: ctype-plain-char
}

bool ctypeGood(char c)
{
	return isalnum((unsigned char) c);
}

const char *danglingBad()
{
	std::string s = "x";
	return s.c_str(); // cocci-expect: dangling-c_str
}

const char *danglingGood(const std::string& s)
{
	return s.c_str();
}

void nullableBad()
{
	std::string home = getenv("HOME"); // cocci-expect: nullable-to-string
	(void) home;
}

void nullableGood()
{
	const char *home = getenv("HOME");
	std::string h = home ? home : "";
	(void) h;
}

void sqliteBad(const char *key)
{
	char *q = sqlite3_mprintf("SELECT value FROM Preferences WHERE key='%s'", key); // cocci-expect: sqlite-format-injection
	sqlite3_free(q);
}

void sqliteGood(const char *key)
{
	char *q = sqlite3_mprintf("SELECT value FROM Preferences WHERE key=%Q", key);
	sqlite3_free(q);
	char *r = sqlite3_mprintf("%s", key);
	sqlite3_free(r);
}

bool lserrorBad(LSHandle *h, LSMessage *m)
{
	LSError lsError;
	LSErrorInit(&lsError);
	if (!LSMessageReply(h, m, "{}", &lsError)) {
		return false; // cocci-expect: lserror-leak
	}
	return true;
}

bool lserrorGood(LSHandle *h, LSMessage *m)
{
	LSError lsError;
	LSErrorInit(&lsError);
	if (!LSMessageReply(h, m, "{}", &lsError)) {
		LSErrorFree(&lsError);
		return false;
	}
	return true;
}
