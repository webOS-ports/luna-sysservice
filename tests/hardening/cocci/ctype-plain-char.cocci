// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
/// The <ctype.h> classifiers take an int that must be EOF or an unsigned
/// char value. A plain char above 0x7f is negative where char is signed
/// (x86; ARM's char is unsigned, so it only bites on some targets) and
/// passing it is undefined behaviour: glibc indexes before its table.
/// Cast to unsigned char first.
//
// Confidence: High
// Options: --c++=17

virtual report

@r@
char c;
identifier f = {isalnum,isalpha,isascii,isblank,iscntrl,isdigit,isgraph,islower,isprint,ispunct,isspace,isupper,isxdigit,tolower,toupper};
position p;
@@
f@p(c)

@script:python depends on report@
p << r.p;
f << r.f;
@@
coccilib.report.print_report(p[0], "%s() on a plain char: cast to unsigned char" % f)
