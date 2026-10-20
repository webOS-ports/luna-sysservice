// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
/// The pointer from c_str() lives only as long as its string: returning
/// it from a local std::string hands out freed memory.
//
// Confidence: High
// Options: --c++=17

virtual report

@r@
type T;
identifier s;
position p;
@@
T s;
...
return s.c_str@p();

@script:python depends on report@
p << r.p;
t << r.T;
s << r.s;
@@
if "string" in t.replace(" ", ""):
    coccilib.report.print_report(p[0], "c_str() of the local %s returned" % s)
