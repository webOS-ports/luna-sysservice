// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
/// These return NULL for a missing value: a message without a sender or
/// application id, an unset variable, a NULL column. Initialising a
/// std::string from NULL is undefined behaviour (a throw or a crash with
/// libstdc++).
//
// Confidence: Moderate
// Options: --c++=17

virtual report

@r@
identifier g = {getenv,g_getenv,secure_getenv,LSMessageGetSender,LSMessageGetSenderServiceName,LSMessageGetApplicationID,LSMessageGetCategory,LSMessageGetMethod,LSHandleGetName};
type T;
identifier s;
expression list args;
position p;
@@
T s = g@p(args);

@script:python depends on report@
p << r.p;
t << r.T;
g << r.g;
@@
if "string" in t.replace(" ", ""):
    coccilib.report.print_report(p[0], "std::string initialised from %s(), which can return NULL" % g)
