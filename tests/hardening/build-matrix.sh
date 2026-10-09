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
# build-matrix.sh <build-dir> <yocto-tmpdir> [--arch armv7,aarch64,x86-64]
#                 [--compiler gcc,clang] [--update-baseline]
#
# Compiles the daemon and the unit tests for armv7, aarch64 and x86-64, each
# with the tree's GCC cross compiler and with clang, at the recipe's flags
# (taken from <build-dir>'s compile_commands.json, with each arch's tune
# flags) plus -Wall -Wextra, and counts the warnings in our own sources.
#
# Warnings already present in Src/ and Inc/ are listed in
# build-matrix.baseline; any other warning there, any warning at all in
# tests/, or a file that does not compile fails the run.
#
# <build-dir>    a corei7-64 build of this tree configured with
#                -DWEBOS_CONFIG_BUILD_TESTS=TRUE (see tests/README.md)
# <yocto-tmpdir> the build tree's TMPDIR: the per-arch sysroots are put
#                together from its sysroots-components (so a recipe workdir
#                cleaned by rm_work does not matter), the cross compilers come
#                from sysroots-components/x86_64/gcc-cross-*.
#
# Only compiles (-c): linking for the other arches would need their complete
# runtime sysroots and adds no warnings in our code.
exec python3 "$(dirname "$0")/build-matrix.py" "$@"
