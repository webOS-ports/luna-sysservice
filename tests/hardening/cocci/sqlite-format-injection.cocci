// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
/// SQL built with sqlite3_mprintf()'s %s pastes the argument in as is: a
/// quote in a key, value or file name from the bus ends the literal and
/// the rest is SQL. %Q is the quoting form.
//
// Confidence: High
// Options: --c++=17

virtual report

@r@
constant char[] fmt;
expression list args;
position p;
@@
sqlite3_mprintf@p(fmt, args)

@script:python depends on report@
p << r.p;
fmt << r.fmt;
@@
import re
if re.search(r"%[-+ 0#]*[0-9]*s", fmt) and re.search(r"\b(SELECT|INSERT|UPDATE|DELETE|REPLACE|ATTACH|WHERE|VALUES)\b", fmt.upper()):
    coccilib.report.print_report(p[0], "sqlite3_mprintf() builds SQL with %s: use %Q")
