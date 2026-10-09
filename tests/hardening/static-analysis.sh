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
# static-analysis.sh <build-dir> [--update-baseline] [--only tool,...] [-j N]
#
# Static-analysis gate for luna-sysservice. Runs, on every source the build's
# compile_commands.json lists (the daemon's and the unit tests'):
#
#   gcc -fanalyzer    -O0 and -O2: use after free, double free, leaks of memory
#                     and file descriptors, NULL dereference, uninitialised reads
#   clang analyzer    the same from a second engine, with the alpha checkers
#                     for unix/security/core/cplusplus
#   clang-tidy        clang-analyzer-*, bugprone-*, cert-*, concurrency-*,
#                     misc-*, performance-*, portability-* and the like
#   cppcheck          all checks, exhaustive, inconclusive included
#   flawfinder        race-prone calls (TOCTOU), unsafe string and buffer
#                     functions; level 1 and up
#   coccinelle        the rules in cocci/ (spatch --c++), each first checked
#                     against cocci/samples so a broken rule cannot pass
#   sparse            reported as not applicable: it parses C only
#
# Findings already present are listed in static-analysis.baseline; the gate
# fails (exit 1) only on findings that are not, or when a tool cannot analyse
# a file. A tool that is not installed is reported SKIPPED and the run exits
# 77 instead of 0: skipped is never passed.
#
# <build-dir> is a cmake build of this tree with compile_commands.json, e.g.
# the corei7-64 recipe's workdir build (OE's cmake class exports it), or one
# made with tests/README.md's out-of-bitbake recipe. Logs go to
# <build-dir>/static-analysis/.
exec python3 "$(dirname "$0")/static-analysis.py" "$@"
