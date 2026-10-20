# luna-sysservice tests

Two layers:

- `unit/`: a gtest suite for the code bus input reaches first. It runs on
  the build host.
- `hardening/`: the static analysers, a warning gate across arches and
  compilers, and the runtime checkers (ASan/UBSan, TSan, valgrind).

Everything below runs against an OE build tree, outside bitbake. The examples
use these names:

```sh
OE_TMPDIR=/path/to/webos-ports/tmp                 # the build's TMPDIR
W=$OE_TMPDIR/work/corei7-64-webos-linux/luna-sysservice/4.4.0-32
S=$W/recipe-sysroot
B=../build-tests                                   # any directory outside the tree
```

`W` must still hold `temp/run.do_configure`, `toolchain.cmake` and its
sysroots. If rm_work removed them, run `bitbake -c configure luna-sysservice`.

## Building with the tests

The tests are only built with `-DWEBOS_CONFIG_BUILD_TESTS=TRUE`. Without that
flag the build and the installed daemon are unchanged. gtest comes from the
sysroot through pkg-config.

```sh
tests/hardening/oe-build.sh $W $B -DWEBOS_CONFIG_BUILD_TESTS=TRUE
```

`oe-build.sh` replays the recipe's own cmake call and environment from
`$W/temp/run.do_configure`: the same toolchain file, sysroots and flags. It
uses this checkout as the source tree and builds into `$B`. It also writes the
`compile_commands.json` that the hardening scripts read.

## Unit tests (`unit/`)

The tests run on the build host, with the sysroot's own loader and libraries:

```sh
mkdir -p $B/tmp
TMPDIR=$B/tmp/ $S/usr/lib/ld-linux-x86-64.so.2 --library-path $S/usr/lib \
    $B/tests/sysservice_unittests
```

Each test makes its scratch files in a fresh directory under `$TMPDIR` and
removes them afterwards. The TzParser tests read the host's
`/usr/share/zoneinfo`; a zone the host lacks is skipped and reported as such.

| suite | covers |
|---|---|
| `test_urlrep.cpp` | `UrlRep::fromUrl` on the urls apps send (wallpaper, ringtone, image paths): components, query parsing, unescaping, invalid input; `escape`/`unescape` |
| `test_tzparser.cpp` | `parseTimeZone` against real tzdata: transitions, offsets, abbreviations, the `Etc/` fallback, and that a zone name cannot leave `/usr/share/zoneinfo` (`..`, `/.`, leading `.`) |
| `test_json.cpp` | the schema macros every method validates its payload with (strict, relaxed, nested), the `JsonMessageParser` getters, `createJsonReply`; the timeZone json helpers of `TimePrefsHandler` |
| `test_prefsdb.cpp` | `PrefsDb` on a scratch database: set/get, `getPrefs`, `getAllPrefs`, `copyKeys`, `merge`, with keys, values and file names that attempt SQL injection |
| `test_utils.cpp` | splitting and trimming, `fileCopy` (never truncates the target on a missing source, never writes through a symlink, reports a write that fails on the final flush), `createTempFile`, `extractFromJson` |
| `test_time.cpp` | `TimePrefsHandler::convertDate` (conversion, bad input, TZ left set or unset as it was, repeated conversions) and `zoneAbbreviation` |
| `test_prefsfactory.cpp` | `PrefsFactory::valueFromDbString`: stored json, bare strings (with quotes and backslashes), numbers and literals |
| `test_settings.cpp` | the schema error option read from the config file, the `-l` option |
| `test_systemrestore.cpp` | the system token: written, and never through a symlink |

Every test was mutation-checked. The behaviour it guards was broken on a
committed tree, the test was seen to fail, and the tree was restored. For the
suite as first added, 47 mutations were made and 45 were caught (tests of
functions since removed as unused went with them). The two that survived
change nothing observable:

- dropping the `..` check in `TzParser`. Every `..` that could climb out
  already starts the name or follows a `/`, and those are refused by the
  leading-`.` and `/.` checks.
- dropping `tzCityNameFromJsonValue`'s `isString()` check. pbnjson's
  `asString()` on a non-string returns `""`, the same as the check. pbnjson
  documents that case as undefined; its implementation returns `""`.

The tests added with the analyser fixes were checked the same way: putting
back the old code makes `test_time`, `test_prefsfactory`, `test_settings`,
`test_systemrestore` and the two new `fileCopy` tests fail. The old `set_tz()`
(a freed buffer left in `environ`) is caught by valgrind, not by ASan, as the
stale read happens inside the C library. Two fixes have no failing test:
`isspace()`/`tolower()` on a plain `char` behave the same on glibc, whose
ctype tables accept negative indices.

## Static analysis (`hardening/static-analysis.sh`)

```sh
tests/hardening/static-analysis.sh $B              # check
tests/hardening/static-analysis.sh $B --only cppcheck,cocci
tests/hardening/static-analysis.sh $B --update-baseline
```

The script runs these tools on every source in `$B/compile_commands.json`,
which covers both the daemon and the tests:

- `gcc -fanalyzer` at `-O0` and `-O2`, using the recipe's own cross g++;
- `clang --analyze` with the core/unix/security/cplusplus checkers and their
  alpha variants;
- `clang-tidy` with the `clang-analyzer`, `bugprone`, `cert`, `concurrency`,
  `misc`, `performance` and `portability` checks;
- `cppcheck` with all checks, exhaustive, inconclusive results included;
- `flawfinder` at level 1 and up;
- the Coccinelle rules in `hardening/cocci/`.

`sparse` parses C only, so for this C++ code base it is reported as not
applicable rather than run on nothing.

Findings that already existed when the gate was introduced are listed in
`hardening/static-analysis.baseline`. The gate fails (exit 1) only on:

- a finding that is not in the baseline;
- a file a tool could not analyse.

The baseline lists only findings triaged as false positives (or as places a
tool cannot see past), each group under a `## <reason>` line. The gate also
fails on a baseline entry without a reason. `--update-baseline` keeps the
reasons of findings that are still reported and puts new ones under
`## UNDOCUMENTED`, which fails the next check until they are triaged: fix the
code, mark the line (`NOLINT(check) // why`, `// cppcheck-suppress id`,
`// Flawfinder: ignore`), or replace `UNDOCUMENTED` with the reason.

The baseline holds no line numbers, so unrelated edits do not disturb it.
Findings the tools no longer report are counted, as a hint to refresh the
baseline. A tool that is not installed is reported as SKIPPED, and the run
then exits 77 instead of 0. Logs go to `$B/static-analysis/`, and every
finding with its line number is in `findings.tsv`.

Notes:

- clang does not know OE's sysroot layout, so the script gives it the
  sysroot's libstdc++ headers explicitly. A file that fails to compile counts
  as a failure, never as clean.
- The gcc analyzer runs one file at a time (`--gcc-jobs`, default 1), with
  its default budget except for the files listed in
  `hardening/analyzer-budget`. Those get a raised budget
  (`analyzer-max-enodes-per-program-point=32`,
  `analyzer-bb-explosion-factor=20`), with which each was seen to finish
  inside the memory cap without stopping early. For the rest the raised
  budget did not help: the analyzer still stopped early, ran past the time
  limit or the memory cap (`TimePrefsHandler.cpp`), or crashed GCC 15.3 at
  `-O2` (an internal compiler error). Each run is capped at 20 GB of address space
  (`--gcc-mem-gb`), and a file waits until 30 GB are free before it starts
  (`--min-free-gb`). On these C++ files a single cc1plus can take 10-18 GB;
  eight of them in parallel nearly exhausted a 125 GB machine. A file that
  hits the cap is recorded as "analyzer ran out of memory" and is not
  retried. The other tools run at most 8 jobs (`-j`), one tool after the
  other.
- Where the analyzer stops at its budget, it records one
  `-Wanalyzer-too-complex` finding for that file. A file that exceeds
  `--gcc-timeout` (default 1800 s) fails the run.
- So a file that was only partly analysed is never counted as clean.
- cppcheck reads `hardening/cppcheck.cfg` for the two macros it would
  otherwise stop at (`G_GNUC_PRINTF`, `timercmp`).
- spatch's C++ support is partial: it skips functions it cannot parse
  (`new T[n]`, namespaces, some templates). The run prints how much of `Src/`
  it actually parsed.

### Coccinelle rules (`hardening/cocci/`)

| rule | flags |
|---|---|
| `sqlite-format-injection` | `sqlite3_mprintf()` SQL built with `%s` instead of `%Q` |
| `ctype-plain-char` | `<ctype.h>` classifiers called on a plain `char`: undefined for bytes above 0x7f where `char` is signed |
| `nullable-to-string` | a `std::string` initialised from `getenv()`, `LSMessageGetSender*()`, `LSMessageGetApplicationID()` and similar, which can return NULL |
| `dangling-c_str` | `c_str()` of a local `std::string` returned |
| `lserror-leak` | a luna-service2 call fails and the function returns without `LSErrorFree()` |

Each rule must report exactly the lines marked `cocci-expect` in
`cocci/samples/cocci-samples.cpp` before its results on the real sources
are used. Run one rule by hand like this:

```sh
spatch --c++=17 -D report --sp-file tests/hardening/cocci/<rule>.cocci Src/<file>.cpp
```

Use one `spatch` per file. spatch 1.3 reports nothing when it is given
several `--dir` arguments or `-j`.

## Warnings across arches and compilers (`hardening/build-matrix.sh`)

```sh
tests/hardening/build-matrix.sh $B $OE_TMPDIR
tests/hardening/build-matrix.sh $B $OE_TMPDIR --arch armv7 --compiler clang
```

This compiles the daemon and the unit tests for armv7
(cortexa8t2hf-neon-halium), aarch64 and x86-64 (corei7-64). Each arch is built
twice:

- with the tree's GCC cross compiler (`sysroots-components/x86_64/gcc-cross-*`);
- with clang (`--target=<triple>`).

The flags are the recipe's, read from `$B`'s compile commands, with each arch's
tune flags. `-Wall -Wextra` go first, so the component's own `-Wno-*` still
win.

The sysroot for each arch is put together from `$OE_TMPDIR/sysroots-components`,
so recipe workdirs that rm_work cleaned do not matter. The script only
compiles: linking for the other arches adds no warnings in our code.

`hardening/build-matrix.baseline` lists accepted warnings in `Src/` and
`Inc/`; it is empty. The run fails on:

- any warning not in the baseline;
- any warning at all in `tests/`;
- a file that does not compile.

At most 8 files compile at once (`-j`); `oe-build.sh` and
`run-sanitizers.sh` also build with 8 jobs unless `JOBS` says otherwise.

Logs and `warnings.tsv` go to `$B/build-matrix/`.

## Runtime checkers (`hardening/run-sanitizers.sh`)

```sh
tests/hardening/run-sanitizers.sh $B $S                       # all four
tests/hardening/run-sanitizers.sh $B $S asan-clang valgrind
```

| variant | build |
|---|---|
| `asan-gcc` | host g++, `-fsanitize=address,undefined`, LeakSanitizer on |
| `asan-clang` | host clang++, `-fsanitize=address,undefined,integer,nullability` (unsigned wrap-around excluded) |
| `tsan` | host clang++, `-fsanitize=thread` |
| `valgrind` | memcheck on `$B/tests/sysservice_unittests` exactly as the OE toolchain built it |

The image toolchain ships no sanitizer runtimes. The sanitizer variants are
therefore rebuilt from the same compile commands with the host's compilers,
against the host C library. Our code is instrumented; the webOS libraries are
the sysroot's own and are not instrumented. This needs the host's glibc to be
at least the sysroot's.

`hardening/ubsan.ignorelist` is where an exemption from clang's checks would
go, with its reason. It holds none: every report halts the run.

A variant fails on any of these:

- a sanitizer report, a valgrind error or a definite leak;
- a failing test;
- a run of zero tests;
- a build that does not work.

The unit suite is single-threaded, so TSan has little to observe there.
