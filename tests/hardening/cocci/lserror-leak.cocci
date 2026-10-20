// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
/// A luna-service2 call that fails fills the LSError with an allocated
/// message; leaving the function without LSErrorFree() (or one of the
/// Print-and-free helpers) leaks it on every failed call.
//
// Confidence: Moderate
// Options: --c++=17

virtual report

@r exists@
identifier e, f;
expression list es;
position p;
@@
LSErrorInit(&e);
...
if (!f(es, &e)) {
  ... when != LSErrorFree(&e)
      when != LSErrorPrintAndFree(&e)
      when != LSErrorLogAndFree(...)
  return@p ...;
}

@script:python depends on report@
p << r.p;
e << r.e;
f << r.f;
@@
coccilib.report.print_report(p[0], "%s() failed and %s is not freed on this return" % (f, e))
