#!/bin/sh
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
# Run the unit suite under the runtime checkers:
#
#   asan-gcc    host g++,     -fsanitize=address,undefined
#   asan-clang  host clang++, -fsanitize=address,undefined,integer,nullability
#   tsan        host clang++, -fsanitize=thread
#   valgrind    memcheck on the suite exactly as the OE toolchain built it
#
#   tests/hardening/run-sanitizers.sh <build-dir> <recipe-sysroot> [variant...]
#
# <build-dir> is a corei7-64 build configured with -DWEBOS_CONFIG_BUILD_TESTS=TRUE
# (its compile_commands.json and tests/sysservice_unittests are used);
# <recipe-sysroot> is that recipe workdir's recipe-sysroot.
#
# The image toolchain ships no sanitizer runtimes, so the sanitizer variants
# are rebuilt with the host's compilers from the same compile commands: our
# sources and the test suite are instrumented, the webOS libraries (glib,
# pbnjson, luna-service2, Qt, ...) are the sysroot's own, uninstrumented.
# That needs the host's glibc to be at least the sysroot's. Every variant must
# finish without a report; any finding, or a variant that cannot be built or
# run, fails the script.
set -u

BUILD=$(cd "${1:?build dir}" && pwd)
SYSROOT=$(cd "${2:?recipe sysroot}" && pwd)
shift 2
VARIANTS=${*:-asan-gcc asan-clang tsan valgrind}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$BUILD/sanitizers"
JOBS=${JOBS:-8}	# parallel compiles; keep it modest, sanitizer builds are big
mkdir -p "$OUT"
rc=0
summary=""

note() { summary="$summary
   $(printf '%-11s %s' "$1" "$2")"; }

# The sysroot's libraries without its glibc, for the host loader to use.
SHIM="$OUT/libs"
rm -rf "$SHIM"; mkdir -p "$SHIM"
for f in "$SYSROOT"/usr/lib/*.so*; do
	case $(basename "$f") in
	ld-linux*|libc.so*|libm.so*|libmvec.so*|libpthread.so*|libdl.so*|librt.so*|\
	libresolv.so*|libutil.so*|libanl.so*|libnss_*|libBrokenLocale.so*|libc_malloc_debug.so*|\
	libasan*|libubsan*|libtsan*|liblsan*) ;;
	*) ln -s "$f" "$SHIM/" ;;
	esac
done

# Writes a build.ninja that recompiles the unit suite with $1 and flags $2.
generate() {
	python3 - "$BUILD" "$SYSROOT" "$1" "$2" "$3" <<'EOF'
import json, os, re, shlex, sys
build, sysroot, cxx, sanflags, out = sys.argv[1:]
db = json.load(open(os.path.join(build, "compile_commands.json")))
objs, lines = [], []
for e in db:
    o = os.path.relpath(os.path.join(e["directory"], e["output"]), build)
    if not o.startswith(("tests/CMakeFiles/sysservice-core.dir/",
                         "tests/CMakeFiles/sysservice_unittests.dir/")):
        continue
    args = shlex.split(e["command"])[1:]
    flags, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ("-o", "-c"):
            skip = a == "-o"
            continue
        if a == e["file"] or a.startswith(("--sysroot=", "-ffile-prefix-map=", "-D_FORTIFY_SOURCE")):
            continue
        if a in ("-fcanon-prefix-map", "-O2", "-O3", "-pipe"):
            continue
        # The sysroot's own glibc and libstdc++ headers stay behind the
        # host's: the code is built against the host C library and runs on it.
        if a == "-I" + sysroot + "/usr/include":
            flags += ["-idirafter", sysroot + "/usr/include"]
            continue
        flags.append(a)
    obj = os.path.join("obj", re.sub(r"[^A-Za-z0-9._-]", "_", o))
    objs.append(obj)
    lines.append("build %s: cxx %s\n  flags = %s\n" % (obj, e["file"], " ".join(shlex.quote(f) for f in flags)))
libs = ["gtest_main", "gtest", "glib-2.0", "xml2", "sqlite3", "pbnjson_c", "pbnjson_cpp",
        "luna-service2", "luna-service2++", "PmLogLib", "uriparser", "nyx", "webosi18n",
        "Qt6Gui", "Qt6Core"]
libpaths = []
for l in libs:
    # gtest is a static library in the sysroot, the rest shared
    for ext in (".so", ".a"):
        p = os.path.join(sysroot, "usr/lib", "lib%s%s" % (l, ext))
        if os.path.exists(p):
            libpaths.append(p)
            break
    else:
        sys.exit("lib%s not in %s/usr/lib" % (l, sysroot))
with open(os.path.join(out, "build.ninja"), "w") as n:
    n.write("cxx = %s\nsan = %s\n" % (cxx, sanflags))
    n.write("rule cxx\n  command = $cxx $flags $san -O1 -g -fno-omit-frame-pointer -c $in -o $out\n"
            "  description = CXX $out\n")
    n.write("rule link\n  command = $cxx $san -o $out $in %s -Wl,-rpath-link,%s/usr/lib -lrt\n"
            "  description = LINK $out\n" % (" ".join(libpaths), sysroot))
    n.writelines(lines)
    n.write("build sysservice_unittests: link %s\n" % " ".join(objs))
EOF
}

run_variant() {
	name=$1 cxx=$2 flags=$3
	dir="$OUT/$name"
	echo "== $name"
	if ! command -v "$cxx" >/dev/null 2>&1; then
		echo "$cxx not installed, SKIPPED"; note "$name" "SKIPPED ($cxx not installed)"; rc=1; return
	fi
	rm -rf "$dir"; mkdir -p "$dir"
	if ! generate "$cxx" "$flags" "$dir" || ! ninja -C "$dir" -j "$JOBS" >"$dir/build.log" 2>&1; then
		tail -20 "$dir/build.log" 2>/dev/null
		note "$name" "FAILED to build (see $dir/build.log)"; rc=1; return
	fi
	# Each test in a temp dir of its own under $TMPDIR (testing::TempDir()).
	mkdir -p "$dir/tmp"
	if env LD_LIBRARY_PATH="$SHIM" TMPDIR="$dir/tmp/" \
	       ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:halt_on_error=1:detect_stack_use_after_return=1:strict_string_checks=1:check_initialization_order=1" \
	       UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
	       TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1" \
	       "$dir/sysservice_unittests" >"$dir/run.log" 2>&1 \
	   && ! grep -qE "runtime error:|ERROR: (Address|Leak|Thread)Sanitizer|WARNING: ThreadSanitizer" "$dir/run.log" \
	   && grep -qE '^\[  PASSED  \] [1-9]' "$dir/run.log"; then
		note "$name" "clean ($(grep -E '^\[  PASSED  \]' "$dir/run.log" | sed 's/\[  PASSED  \] //'))"
	else
		grep -E "runtime error:|Sanitizer|FAILED|SUMMARY" "$dir/run.log" | head -40
		note "$name" "FAILED (see $dir/run.log)"; rc=1
	fi
}

for v in $VARIANTS; do
	case $v in
	asan-gcc)
		run_variant asan-gcc "${SAN_GXX:-g++}" \
			"-fsanitize=address,undefined -fno-sanitize-recover=all" ;;
	asan-clang)
		# unsigned wrap-around is well defined and used on purpose (tz
		# decoding); everything else in 'integer' is reported
		run_variant asan-clang "${SAN_CLANGXX:-clang++}" \
			"-fsanitize=address,undefined,integer,nullability -fno-sanitize=unsigned-integer-overflow,unsigned-shift-base -fno-sanitize-recover=all -fsanitize-ignorelist=$HERE/ubsan.ignorelist" ;;
	tsan)
		run_variant tsan "${SAN_CLANGXX:-clang++}" "-fsanitize=thread" ;;
	valgrind)
		echo "== valgrind"
		if ! command -v valgrind >/dev/null 2>&1; then
			note valgrind "SKIPPED (not installed)"; rc=1; continue
		fi
		mkdir -p "$OUT/valgrind/tmp"
		if TMPDIR="$OUT/valgrind/tmp/" valgrind --error-exitcode=42 --leak-check=full \
		       --show-leak-kinds=definite --errors-for-leak-kinds=definite \
		       --track-origins=yes \
		       "$SYSROOT/usr/lib/ld-linux-x86-64.so.2" --library-path "$SYSROOT/usr/lib" \
		       "$BUILD/tests/sysservice_unittests" >"$OUT/valgrind/run.log" 2>&1 \
		   && grep -qE '^\[  PASSED  \] [1-9]' "$OUT/valgrind/run.log"; then
			note valgrind "clean ($(grep -E '^\[  PASSED  \]' "$OUT/valgrind/run.log" | sed 's/\[  PASSED  \] //'), $(grep -E 'ERROR SUMMARY' "$OUT/valgrind/run.log" | sed 's/^==[0-9]*== //'))"
		else
			grep -E "^==[0-9]+== (Invalid|Conditional|.*definitely|ERROR SUMMARY)|FAILED" "$OUT/valgrind/run.log" | head -40
			note valgrind "FAILED (see $OUT/valgrind/run.log)"; rc=1
		fi ;;
	*)
		echo "unknown variant $v"; rc=1 ;;
	esac
done

echo "== summary$summary"
exit $rc
