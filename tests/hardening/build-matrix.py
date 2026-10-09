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
# The work behind build-matrix.sh; see there and tests/README.md.

import argparse
import collections
import concurrent.futures
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BASELINE = os.path.join(HERE, "build-matrix.baseline")
OWN = ("Src/", "Inc/", "include/", "tests/")

# arch: sysroots-components dir, triple, gcc-cross dir, the recipe's tune
# flags (TUNE_CCARGS, as in the recipe workdirs' run.do_configure)
ARCHES = collections.OrderedDict([
    ("armv7", ("cortexa8t2hf-neon-halium", "arm-webos-linux-gnueabi", "gcc-cross-arm",
               "-mthumb -mfpu=neon -mfloat-abi=hard -mcpu=cortex-a8 -D_FILE_OFFSET_BITS=64")),
    ("aarch64", ("aarch64", "aarch64-webos-linux", "gcc-cross-aarch64",
                 "-mbranch-protection=standard")),
    ("x86-64", ("corei7-64", "x86_64-webos-linux", "gcc-cross-x86_64",
                "-m64 -march=nehalem -mtune=generic -mfpmath=sse -msse4.2")),
])
# The recipe's x86-64 tune flags, taken out of the template's commands
X86_TUNE = {"-m64", "-march=nehalem", "-mtune=generic", "-mfpmath=sse", "-msse4.2"}

# What luna-sysservice and its tests include, with the headers those pull in.
COMPONENTS = ["glibc", "linux-libc-headers", "libgcc", "gcc-runtime", "glib-2.0", "libpcre2",
              "libffi", "zlib", "libxml2", "xz", "icu", "sqlite3", "libpbnjson", "yajl",
              "luna-service2", "libwebosi18n", "boost", "pmloglib", "qtbase", "nyx-lib",
              "uriparser", "googletest"]

DIAG = re.compile(r"^(?P<file>[^:\s][^:]*):(?P<line>\d+):(?:\d+:)? (?P<sev>warning|error|fatal error): "
                  r"(?P<msg>.*?)(?: \[(?P<id>-W[^\]]+|-Werror[^\]]*)\])?$")


def run(cmd, cwd=None):
    p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, errors="replace")
    return p.returncode, p.stdout


def rel(path, directory):
    p = os.path.normpath(os.path.join(directory, path))
    if not p.startswith(ROOT + os.sep):
        return None
    r = os.path.relpath(p, ROOT)
    return r if r.startswith(OWN) else None


def make_sysroot(tmpdir, arch, out):
    comp, _triple, _gcc, _tune = ARCHES[arch]
    src = os.path.join(tmpdir, "sysroots-components", comp)
    dst = os.path.join(out, "sysroot-" + arch)
    stamp = os.path.join(dst, ".complete")
    if os.path.exists(stamp):
        return dst, []
    shutil.rmtree(dst, ignore_errors=True)
    os.makedirs(dst)
    missing = []
    for c in COMPONENTS:
        d = os.path.join(src, c)
        if not os.path.isdir(d):
            missing.append(c)
            continue
        # symlinks to the components, merged the way bitbake stages them
        subprocess.run(["cp", "-rs", "--update=none", d + "/.", dst + "/"], check=True)
    open(stamp, "w").close()
    return dst, missing


def load_template(build):
    db = json.load(open(os.path.join(build, "compile_commands.json")))
    units, seen = [], set()
    for e in db:
        o = os.path.relpath(os.path.join(e["directory"], e["output"]), build)
        # the daemon's objects and the test suite's; sysservice-core is
        # the daemon's sources a second time
        if not o.startswith(("CMakeFiles/LunaSysService.dir/", "tests/CMakeFiles/sysservice_unittests.dir/")):
            continue
        f = os.path.normpath(os.path.join(e["directory"], e["file"]))
        if f in seen:
            continue
        seen.add(f)
        args = shlex.split(e["command"])
        sysroot = next(a.split("=", 1)[1] for a in args if a.startswith("--sysroot="))
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
            if a.startswith(("--sysroot=", "-ffile-prefix-map=")) or a in X86_TUNE:
                continue
            flags.append(a)
        units.append({"file": f, "dir": e["directory"], "flags": flags, "sysroot": sysroot})
    return units


def commands(unit, arch, compiler, sysroot, tmpdir, tools):
    _comp, triple, gccdir, tune = ARCHES[arch]
    old = unit["sysroot"]
    flags = [a.replace(old, sysroot) for a in unit["flags"]]
    # -Wall -Wextra first, so the component's own -Wno-* still win in both
    # compilers (clang applies warning options in order).
    warn = ["-Wall", "-Wextra"]
    if compiler == "gcc":
        cxx = os.path.join(tmpdir, "sysroots-components/x86_64", gccdir, "usr/bin", triple, triple + "-g++")
        return [cxx, "--sysroot=" + sysroot, "-B" + tools, *warn, *tune.split(), *flags,
                "-c", "-o", "/dev/null", unit["file"]]
    gccver = sorted(os.listdir(os.path.join(sysroot, "usr/include/c++")))[-1]
    cxxinc = os.path.join(sysroot, "usr/include/c++", gccver)
    return ["clang++", "--target=" + triple, "--sysroot=" + sysroot, "-nostdinc++",
            "-isystem", cxxinc, "-isystem", os.path.join(cxxinc, triple),
            *warn, *tune.split(), *[a for a in flags if a != "-fcanon-prefix-map"],
            "-c", "-o", "/dev/null", unit["file"]]


def tools_dir(tmpdir, arch, out):
    _comp, triple, gccdir, _tune = ARCHES[arch]
    t = os.path.join(out, "tools-" + arch)
    os.makedirs(t, exist_ok=True)
    bu = os.path.join(tmpdir, "sysroots-components/x86_64", "binutils-cross-" + gccdir[len("gcc-cross-"):],
                      "usr/bin", triple)
    # the cross gcc looks for an unprefixed 'as'
    for name in ("as", "ld"):
        link = os.path.join(t, name)
        if os.path.lexists(link):
            os.unlink(link)
        os.symlink(os.path.join(bu, triple + "-" + name), link)
    return t


def main():
    ap = argparse.ArgumentParser(description="warning gate for luna-sysservice across arches and compilers")
    ap.add_argument("build", help="corei7-64 build with tests (compile_commands.json is the template)")
    ap.add_argument("tmpdir", help="the Yocto TMPDIR (holding sysroots-components)")
    ap.add_argument("--arch", default=",".join(ARCHES), help="comma separated: " + ",".join(ARCHES))
    ap.add_argument("--compiler", default="gcc,clang", help="comma separated: gcc,clang")
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--out", help="default <build>/build-matrix")
    args = ap.parse_args()
    out = os.path.abspath(args.out or os.path.join(args.build, "build-matrix"))
    os.makedirs(out, exist_ok=True)
    units = load_template(args.build)
    if not units:
        sys.exit("build-matrix: no LunaSysService / unit test sources in %s/compile_commands.json "
                 "(configure with -DWEBOS_CONFIG_BUILD_TESTS=TRUE)" % args.build)

    found, status, rc = [], collections.OrderedDict(), 0
    for arch in args.arch.split(","):
        sysroot, missing = make_sysroot(args.tmpdir, arch, out)
        tools = tools_dir(args.tmpdir, arch, out)
        for compiler in args.compiler.split(","):
            cell = "%s %s" % (arch, compiler)
            cmds = [commands(u, arch, compiler, sysroot, args.tmpdir, tools) for u in units]
            if not shutil.which(cmds[0][0]):
                status[cell] = "SKIPPED (%s not found)" % cmds[0][0]
                print("== %s: %s" % (cell, status[cell]))
                rc = rc or 77
                continue
            print("== %s: %s" % (cell, run([cmds[0][0], "--version"])[1].splitlines()[0]))
            with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
                results = list(ex.map(lambda c: run(c, ROOT), cmds))
            log = open(os.path.join(out, "%s-%s.log" % (arch, compiler)), "w")
            failed, cellfound = [], set()
            for u, c, (code, text) in zip(units, cmds, results):
                log.write("### %s (exit %d)\n%s\n%s\n" % (u["file"], code, " ".join(map(shlex.quote, c)), text))
                if code != 0:
                    failed.append(os.path.relpath(u["file"], ROOT))
                for line in text.splitlines():
                    m = DIAG.match(line)
                    if m:
                        f = rel(m.group("file"), ROOT)
                        if f and m.group("sev") == "warning":
                            cellfound.add((arch, compiler, f, int(m.group("line")), m.group("id") or "", m.group("msg")))
            log.close()
            found += sorted(cellfound)
            own = sum(1 for x in cellfound if not x[2].startswith("tests/"))
            tests = sum(1 for x in cellfound if x[2].startswith("tests/"))
            status[cell] = "%d warning(s) in Src/Inc, %d in tests/" % (own, tests)
            if failed:
                status[cell] += "; FAILED to compile: " + ", ".join(failed)
                rc = 1
                if missing:
                    status[cell] += " (sysroot lacks: %s)" % ", ".join(missing)
            print("   " + status[cell])

    key = lambda x: "\t".join((x[0], x[1], x[2], x[4], re.sub(r"\b(line|col) \d+", r"\1 N", x[5])))
    with open(os.path.join(out, "warnings.tsv"), "w") as w:
        for x in found:
            w.write("%s:%d\t%s %s\t%s\t%s\n" % (x[2], x[3], x[0], x[1], x[4], x[5]))
    current = collections.Counter(key(x) for x in found if not x[2].startswith("tests/"))
    if args.update_baseline:
        with open(BASELINE, "w") as b:
            b.write("# Pre-existing compiler warnings (-Wall -Wextra on top of the recipe flags)\n"
                    "# in luna-sysservice's own sources; build-matrix.sh fails on any other.\n"
                    "# arch<TAB>compiler<TAB>file<TAB>option<TAB>message, no line numbers.\n"
                    "# Never listed: warnings in tests/, which must stay at zero.\n")
            for k in sorted(current.elements()):
                b.write(k + "\n")
        print("baseline written: %d warning(s)" % sum(current.values()))
    base = collections.Counter()
    if os.path.isfile(BASELINE):
        base.update(l.rstrip("\n") for l in open(BASELINE) if l.strip() and not l.startswith("#"))
    budget = current - base
    new = []
    for x in found:
        if x[2].startswith("tests/"):
            new.append(x)
        elif budget[key(x)] > 0:
            budget[key(x)] -= 1
            new.append(x)

    print("\n== summary")
    for cell, st in status.items():
        print("   %-15s %s" % (cell, st))
    gone = base - current
    if gone and not args.update_baseline:
        print("   %d baseline warning(s) no longer seen - fixed? refresh with --update-baseline" % sum(gone.values()))
    if new and not args.update_baseline:
        print("\n== NEW warnings (%d):" % len(new))
        for x in new:
            print("%s:%d: [%s %s] %s [%s]" % (x[2], x[3], x[0], x[1], x[5], x[4]))
        rc = 1
    print("logs: %s" % out)
    sys.exit(rc)


if __name__ == "__main__":
    main()
