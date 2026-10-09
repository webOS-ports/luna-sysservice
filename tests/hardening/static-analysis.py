#!/usr/bin/env python3
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
#
# The work behind static-analysis.sh; see there and tests/README.md.

import argparse
import collections
import concurrent.futures
import json
import os
import re
import resource
import shlex
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BASELINE = os.path.join(HERE, "static-analysis.baseline")
COCCI_DIR = os.path.join(HERE, "cocci")
# The sources that are ours; everything else (sysroot headers) is context.
OWN = ("Src/", "Inc/", "include/", "tests/")

DIAG = re.compile(r"^(?P<file>[^:\s][^:]*):(?P<line>\d+):(?:(?P<col>\d+):)? "
                  r"(?P<sev>warning|error|fatal error): (?P<msg>.*?)"
                  r"(?: \[(?P<id>[^\]\s]+)\])?$")

TIDY_CHECKS = ",".join([
    "clang-analyzer-*", "bugprone-*", "cert-*", "concurrency-*", "misc-*",
    "performance-*", "portability-*", "readability-misleading-indentation",
    "readability-suspicious-call-argument",
    # noise for this code base rather than defects:
    "-bugprone-easily-swappable-parameters", "-misc-include-cleaner",
    "-misc-unused-parameters", "-misc-non-private-member-variables-in-classes",
    "-misc-use-anonymous-namespace", "-misc-use-internal-linkage",
    "-bugprone-reserved-identifier", "-cert-dcl37-c", "-cert-dcl51-cpp",
    "-cert-err58-cpp", "-misc-const-correctness",
])

CLANG_CHECKERS = "core,cplusplus,unix,deadcode,security,nullability,optin.portability,optin.core,optin.cplusplus"
CLANG_ALPHA = ("alpha.unix,alpha.security,alpha.core.PointerArithm,alpha.core.CastSize,"
               "alpha.core.Conversion,alpha.unix.cstring,alpha.cplusplus")

# gcc options clang does not know
GCC_ONLY = re.compile(r"^-fcanon-prefix-map$")


class Finding(collections.namedtuple("Finding", "tool sev file line id msg")):
    def key(self):
        # Line numbers are left out on purpose: unrelated edits move them.
        msg = re.sub(r"\b(line|col(umn)?) \d+", r"\1 N", self.msg)
        msg = re.sub(r":\d+(:\d+)?", ":N", msg)
        return "\t".join((self.tool, self.sev, self.file, self.id, msg))


def rel(path, directory):
    p = os.path.normpath(os.path.join(directory, path))
    if not p.startswith(ROOT + os.sep):
        return None
    r = os.path.relpath(p, ROOT)
    return r if r.startswith(OWN) else None


def mem_available_gb():
    try:
        for line in open("/proc/meminfo"):
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) // (1024 * 1024)
    except OSError:
        pass
    return None


def wait_for_memory(min_gb):
    # Never start a heavy job on a machine that is already short.
    while min_gb:
        avail = mem_available_gb()
        if avail is None or avail >= min_gb:
            return
        print("   waiting: %d GB available, need %d GB" % (avail, min_gb), flush=True)
        time.sleep(30)


def run(cmd, cwd=None, timeout=None, mem_gb=None):
    def cap():
        # A hard address-space cap: the process fails its allocation
        # instead of pushing the machine into swap.
        if mem_gb:
            limit = mem_gb * 1024 ** 3
            resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, errors="replace", timeout=timeout,
                           preexec_fn=cap if mem_gb else None)
    except subprocess.TimeoutExpired as e:
        out = e.output.decode(errors="replace") if isinstance(e.output, bytes) else (e.output or "")
        return 124, out + "\nTIMEOUT after %d s\n" % timeout
    return p.returncode, p.stdout


def load_db(build):
    path = os.path.join(build, "compile_commands.json")
    if not os.path.isfile(path):
        sys.exit("static-analysis: no %s - configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "
                 "(the OE cmake class does)" % path)
    seen, units = set(), []
    for e in json.load(open(path)):
        f = os.path.normpath(os.path.join(e["directory"], e["file"]))
        if f in seen or rel(f, "/") is None:
            continue
        seen.add(f)
        args = e.get("arguments") or shlex.split(e["command"])
        flags, skip = [], False
        for a in args[1:]:
            if skip:
                skip = False
                continue
            if a == "-o":
                skip = True
                continue
            if a == "-c" or os.path.normpath(os.path.join(e["directory"], a)) == f:
                continue
            flags.append(a)
        units.append({"file": f, "dir": e["directory"], "cc": args[0], "flags": flags})
    return units


def triple(cc):
    m = re.match(r"(.*)-(g\+\+|c\+\+|gcc|clang\+\+)$", os.path.basename(cc))
    return m.group(1) if m else None


def clang_flags(u):
    t = triple(u["cc"])
    flags = (["--target=" + t] if t else [])
    # Upstream clang does not know OE's sysroot layout and finds no
    # libstdc++ there: name its headers.
    sysroot = next((a.split("=", 1)[1] for a in u["flags"] if a.startswith("--sysroot=")), None)
    cxxdir = os.path.join(sysroot or "/", "usr/include/c++")
    if sysroot and t and os.path.isdir(cxxdir):
        inc = os.path.join(cxxdir, sorted(os.listdir(cxxdir))[-1])
        flags += ["-nostdinc++", "-isystem", inc, "-isystem", os.path.join(inc, t)]
    return flags + [a for a in u["flags"] if not GCC_ONLY.match(a) and not a.startswith("-Wl,")]


def parse_diags(tool, out, directory, keep):
    found = []
    for line in out.splitlines():
        m = DIAG.match(line)
        if not m:
            continue
        f = rel(m.group("file"), directory)
        ident = m.group("id") or ""
        if f is None or not keep(m.group("sev"), ident):
            continue
        found.append(Finding(tool, m.group("sev"), f, int(m.group("line")), ident, m.group("msg")))
    return found


class Runner:
    def __init__(self, args):
        self.args = args
        self.out = args.out
        self.findings = []
        self.status = collections.OrderedDict()  # tool -> clean / N findings / skipped / failed

    def log(self, name, text):
        with open(os.path.join(self.out, re.sub(r"[^A-Za-z0-9.+-]+", "_", name) + ".log"), "a") as f:
            f.write(text)

    def have(self, name, binary):
        if shutil.which(binary):
            return True
        print("== %s: %s not installed, SKIPPED" % (name, binary))
        self.status[name] = "SKIPPED (not installed)"
        return False

    def per_unit(self, name, units, make_cmd, keep, jobs=None, timeout=None, mem_gb=None):
        failed, found = [], []

        def one(u):
            wait_for_memory(self.args.min_free_gb if mem_gb else 0)
            return run(make_cmd(u), u["dir"], timeout, mem_gb)

        with concurrent.futures.ThreadPoolExecutor(jobs or self.args.jobs) as ex:
            futs = {ex.submit(one, u): u for u in units}
            for fut in concurrent.futures.as_completed(futs):
                u = futs[fut]
                rc, out = fut.result()
                self.log(name, "### %s (exit %d)\n%s\n" % (u["file"], rc, out))
                found += parse_diags(name, out, u["dir"], keep)
                # gcc says where it gave up only as "cc1plus: warning": keep
                # one finding per file, so a file analysed only in part is
                # never silently counted as clean.
                if "[-Wanalyzer-too-complex]" in out and rel(u["file"], "/"):
                    found.append(Finding(name, "warning", rel(u["file"], "/"), 0, "-Wanalyzer-too-complex",
                                         "analysis stopped at its budget for some program points"))
                # Out of memory under the cap: recorded for the file, not
                # retried, and never counted as clean.
                if mem_gb and re.search(r"out of memory|virtual memory exhausted|std::bad_alloc", out) \
                        and rel(u["file"], "/"):
                    found.append(Finding(name, "warning", rel(u["file"], "/"), 0, "analyzer-out-of-memory",
                                         "analyzer ran out of memory (cap %dG)" % mem_gb))
                    continue
                # a file that does not compile is not analysed, whatever else
                # the tool printed
                if rc == 124 or (rc != 0 and not re.search(r"\b(warning|error):", out)) \
                        or re.search(r": (fatal )?error: .*(\[clang-diagnostic-error\])?$", out, re.M):
                    failed.append(u["file"])
        self.finish(name, found, failed)

    def finish(self, name, found, failed=()):
        found = sorted(set(found))
        self.findings += found
        if failed:
            self.status[name] = "FAILED to analyse %d file(s): %s" % (
                len(failed), ", ".join(sorted(os.path.relpath(f, ROOT) for f in failed)))
        else:
            self.status[name] = "%d finding(s)" % len(found)
        print("== %s: %s" % (name, self.status[name]))

    # -- the tools ---------------------------------------------------------

    def gcc_analyzer(self, units):
        for opt in ("-O0", "-O2"):
            name = "gcc -fanalyzer " + opt
            cc = self.args.gxx or units[0]["cc"]
            if not self.have(name, cc):
                continue
            # At -O2 inlining hides some paths, at -O0 others: run both.
            # The analyzer's default budget: on these C++ files (Qt, pbnjson
            # and libstdc++ headers) a raised one takes many GB and hours per
            # file. Where it gives up, -Wanalyzer-too-complex says so, and
            # that is recorded like any other finding.
            self.per_unit(name, units, lambda u: [
                cc, *u["flags"], opt, "-fanalyzer", "-Wanalyzer-too-complex",
                "-c", "-o", "/dev/null", u["file"]],
                lambda sev, ident: ident.startswith("-Wanalyzer") or sev != "warning",
                jobs=self.args.gcc_jobs, timeout=self.args.gcc_timeout, mem_gb=self.args.gcc_mem_gb)

    def clang_analyzer(self, units):
        name = "clang analyzer"
        if not self.have(name, self.args.clangxx):
            return
        self.per_unit(name, units, lambda u: [
            self.args.clangxx, "--analyze", "-Xanalyzer", "-analyzer-output=text",
            "-Xanalyzer", "-analyzer-checker=" + CLANG_CHECKERS,
            "-Xanalyzer", "-analyzer-checker=" + CLANG_ALPHA,
            *clang_flags(u), "-o", "/dev/null", u["file"]],
            lambda sev, ident: "." in ident or sev != "warning")

    def clang_tidy(self, units):
        name = "clang-tidy"
        if not self.have(name, "clang-tidy"):
            return
        db = os.path.join(self.out, "tidy")
        os.makedirs(db, exist_ok=True)
        json.dump([{"directory": u["dir"], "file": u["file"],
                    "arguments": [self.args.clangxx, *clang_flags(u), "-c", u["file"]]}
                   for u in units], open(os.path.join(db, "compile_commands.json"), "w"), indent=1)
        own = "^" + re.escape(ROOT) + "/(Src|Inc|include|tests)/"
        self.per_unit(name, units, lambda u: [
            "clang-tidy", "--quiet", "-p", db, "--checks=" + TIDY_CHECKS,
            "--header-filter=" + own, u["file"]],
            lambda sev, ident: not ident.startswith("clang-diagnostic-") or sev != "warning")

    def cppcheck(self):
        name = "cppcheck"
        if not self.have(name, "cppcheck"):
            return
        # Our sources and headers only: the sysroot's are not ours to lint
        # and only there so types resolve.
        rc, out = run(["cppcheck", "--enable=all", "--check-level=exhaustive", "--inconclusive",
                       "--std=c++17", "--language=c++", "--library=posix", "--library=qt",
                       "--library=" + os.path.join(HERE, "cppcheck.cfg"),
                       "--force", "--inline-suppr", "--quiet", "-j", str(self.args.jobs),
                       "--suppress=missingIncludeSystem", "--suppress=missingInclude",
                       "--suppress=unmatchedSuppression", "--suppress=checkersReport",
                       "--suppress=unusedFunction", "-D__linux__", "-D__GNUC__",
                       "-I", "Inc", "-I", "include/public",
                       "--template={file}:{line}:{column}: {severity}: {message} [{id}]",
                       "Src", "tests/unit"], cwd=ROOT)
        self.log(name, out)
        found = []
        for line in out.splitlines():
            m = re.match(r"^(.*?):(\d+):\d+: (\w+): (.*) \[(\w+)\]$", line)
            if m and rel(m.group(1), ROOT):
                found.append(Finding(name, m.group(3), rel(m.group(1), ROOT), int(m.group(2)),
                                     m.group(5), m.group(4)))
        self.finish(name, found, [] if rc == 0 else ["(cppcheck exit %d)" % rc])

    def flawfinder(self):
        name = "flawfinder"
        if not self.have(name, "flawfinder"):
            return
        rc, out = run(["flawfinder", "--minlevel=1", "--falsepositive", "--columns", "--dataonly", "--singleline",
                       "--quiet", "Src", "Inc", "tests/unit"], cwd=ROOT)
        self.log(name, out)
        found = []
        for line in out.splitlines():
            m = re.match(r"^(.*?):(\d+):\d+: +\[(\d)\] \((\w+)\) (\S+):(.*)$", line)
            if m and rel(m.group(1), ROOT):
                found.append(Finding(name, "level-" + m.group(3), rel(m.group(1), ROOT),
                                     int(m.group(2)), m.group(4) + "/" + m.group(5),
                                     m.group(6).strip()))
        self.finish(name, found, [] if rc == 0 else ["(flawfinder exit %d)" % rc])

    def sparse(self):
        # sparse parses C only; every source here is C++. It is reported as
        # not applicable rather than run on nothing and counted as clean.
        print("== sparse: NOT APPLICABLE (C only, luna-sysservice is C++)")
        self.status["sparse"] = "NOT APPLICABLE (C only)"

    def coccinelle(self):
        name = "coccinelle"
        if not self.have(name, "spatch"):
            return
        rules = sorted(f for f in os.listdir(COCCI_DIR) if f.endswith(".cocci"))
        sample = os.path.join(COCCI_DIR, "samples", "cocci-samples.cpp")
        expect = collections.defaultdict(set)
        for n, line in enumerate(open(sample), 1):
            m = re.search(r"cocci-expect: (\S+)", line)
            if m:
                expect[m.group(1)].add(n)
        found, failed = [], []
        for r in rules:
            rule = r[:-len(".cocci")]
            base = ["spatch", "--c++=17", "--very-quiet", "-D", "report",
                    "--sp-file", os.path.join(COCCI_DIR, r)]
            # A rule that misses its own sample says nothing about the code.
            rc, out = run(base + [sample], cwd=ROOT)
            got = {int(m.group(1)) for m in re.finditer(r"^[^:]+:(\d+):", out, re.M)}
            if got != expect[rule]:
                failed.append("%s (self-test: expected lines %s, got %s)"
                              % (rule, sorted(expect[rule]), sorted(got)))
                continue
            # One spatch per file: with several --dir or -j it reports nothing
            # (spatch 1.3).
            files = sorted(os.path.join(d, f) for d in ("Src", "Inc", "tests/unit")
                           for f in os.listdir(os.path.join(ROOT, d))
                           if f.endswith((".cpp", ".h")))
            with concurrent.futures.ThreadPoolExecutor(self.args.jobs) as ex:
                results = list(ex.map(lambda f: run(base + [f], cwd=ROOT), files))
            for f, (rc, out) in zip(files, results):
                self.log(name, "### %s %s (exit %d)\n%s\n" % (r, f, rc, out))
                if rc != 0:
                    failed.append("%s on %s" % (rule, f))
                for m in re.finditer(r"^(.*?):(\d+):[\d-]+: (.*)$", out, re.M):
                    fr = rel(m.group(1), ROOT)
                    if fr:
                        found.append(Finding(name, "report", fr, int(m.group(2)), rule, m.group(3)))
        # spatch's C++ support is partial: functions it cannot parse are
        # silently skipped, so say how much of the code it actually saw.
        srcs = [os.path.join("Src", f) for f in sorted(os.listdir(os.path.join(ROOT, "Src")))
                if f.endswith(".cpp")]
        with concurrent.futures.ThreadPoolExecutor(self.args.jobs) as ex:
            outs = list(ex.map(lambda f: run(["spatch", "--c++=17", "--parse-c", f], cwd=ROOT)[1], srcs))
        good = bad = 0
        for out in outs:
            m = re.search(r"nb good = (\d+),\s+nb bad = (\d+)", out)
            if m:
                good, bad = good + int(m.group(1)), bad + int(m.group(2))
        if good + bad:
            print("   coccinelle parsed %d%% of the lines in Src (the rest is invisible to it)"
                  % (100 * good // (good + bad)))
        self.finish(name, found, failed)


def compare(findings, update):
    current = collections.Counter(f.key() for f in findings)
    if update:
        with open(BASELINE, "w") as b:
            b.write("# Pre-existing static-analysis findings; static-analysis.sh fails only on\n"
                    "# findings that are not listed here. One line per finding (repeated when\n"
                    "# reported more than once), without line numbers:\n"
                    "# tool<TAB>severity<TAB>file<TAB>check<TAB>message\n"
                    "# Regenerate with: tests/hardening/static-analysis.sh <build> --update-baseline\n")
            for k in sorted(current.elements()):
                b.write(k + "\n")
        print("baseline written: %d finding(s) in %s" % (sum(current.values()), os.path.relpath(BASELINE, ROOT)))
        return [], collections.Counter()
    base = collections.Counter()
    if os.path.isfile(BASELINE):
        base.update(l.rstrip("\n") for l in open(BASELINE) if l.strip() and not l.startswith("#"))
    new_keys = current - base
    gone = base - current
    new = []
    budget = collections.Counter(new_keys)
    for f in findings:
        if budget[f.key()] > 0:
            budget[f.key()] -= 1
            new.append(f)
    return new, gone


def main():
    ap = argparse.ArgumentParser(description="static analysis gate for luna-sysservice")
    ap.add_argument("build", help="build directory holding compile_commands.json")
    ap.add_argument("--update-baseline", action="store_true",
                    help="record the current findings as the accepted baseline")
    ap.add_argument("--only", default="", help="comma separated: gcc,clang,tidy,cppcheck,flawfinder,sparse,cocci")
    ap.add_argument("--jobs", "-j", type=int, default=min(8, os.cpu_count() or 4),
                    help="parallel runs of the lighter tools (default 8)")
    ap.add_argument("--gxx", default=os.environ.get("ANALYZER_GXX"),
                    help="g++ for -fanalyzer (default: the compiler of the compile database)")
    ap.add_argument("--gcc-jobs", type=int, default=1,
                    help="parallel gcc -fanalyzer runs; each can take 10-18 GB on these "
                         "C++ files, so the default is one at a time")
    ap.add_argument("--gcc-mem-gb", type=int, default=20,
                    help="address-space cap per gcc -fanalyzer run (default 20); a file "
                         "that hits it is recorded as out of memory, not retried")
    ap.add_argument("--min-free-gb", type=int, default=30,
                    help="wait before each gcc -fanalyzer run until this much memory "
                         "is available (default 30, 0 = do not wait)")
    ap.add_argument("--gcc-timeout", type=int, default=1800,
                    help="seconds per file for gcc -fanalyzer; a timeout fails the run")
    ap.add_argument("--clangxx", default=os.environ.get("ANALYZER_CLANGXX", "clang++"))
    ap.add_argument("--out", help="where the logs go (default <build>/static-analysis)")
    args = ap.parse_args()
    args.out = args.out or os.path.join(args.build, "static-analysis")
    shutil.rmtree(args.out, ignore_errors=True)
    os.makedirs(args.out)

    only = set(filter(None, args.only.split(",")))
    want = lambda t: not only or t in only
    units = load_db(args.build)
    r = Runner(args)
    if want("gcc"):
        r.gcc_analyzer(units)
    if want("clang"):
        r.clang_analyzer(units)
    if want("tidy"):
        r.clang_tidy(units)
    if want("cppcheck"):
        r.cppcheck()
    if want("flawfinder"):
        r.flawfinder()
    if want("sparse"):
        r.sparse()
    if want("cocci"):
        r.coccinelle()

    with open(os.path.join(args.out, "findings.tsv"), "w") as f:
        for x in sorted(r.findings, key=lambda x: (x.file, x.line, x.tool)):
            f.write("%s:%d\t%s\t%s\t%s\t%s\n" % (x.file, x.line, x.tool, x.sev, x.id, x.msg))

    if only and args.update_baseline:
        sys.exit("--update-baseline needs every tool; drop --only")
    new, gone = compare(r.findings, args.update_baseline)

    print("\n== summary")
    for tool, st in r.status.items():
        print("   %-22s %s" % (tool, st))
    if gone:
        print("   %d baseline finding(s) no longer reported - fixed? refresh with --update-baseline"
              % sum(gone.values()))
    rc = 0
    if new:
        print("\n== NEW findings (%d), not in %s:" % (len(new), os.path.relpath(BASELINE, ROOT)))
        for x in sorted(new, key=lambda x: (x.file, x.line)):
            print("%s:%d: [%s] %s: %s [%s]" % (x.file, x.line, x.tool, x.sev, x.msg, x.id))
        rc = 1
    if any(s.startswith("FAILED") for s in r.status.values()):
        rc = 1
    skipped = [t for t, s in r.status.items() if s.startswith("SKIPPED")]
    if rc == 0 and skipped:
        print("\nno new findings, but not every tool ran (skipped: %s) - not a pass" % ", ".join(skipped))
        rc = 77
    elif rc == 0:
        print("\nno new findings")
    print("logs: %s" % args.out)
    sys.exit(rc)


if __name__ == "__main__":
    main()
