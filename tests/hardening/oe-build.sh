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
# oe-build.sh <recipe-workdir> <build-dir> [cmake args...]
#
# Configures and builds this tree outside bitbake, exactly as the recipe's
# do_configure/do_compile would - same toolchain file, sysroots, flags and
# cmake options, read from the workdir's temp/run.do_configure - but from this
# checkout and into <build-dir>. Extra arguments go to cmake, e.g.
#
#   tests/hardening/oe-build.sh \
#       $TMPDIR/work/corei7-64-webos-linux/luna-sysservice/<ver> \
#       ../build-tests -DWEBOS_CONFIG_BUILD_TESTS=TRUE
#
# The workdir needs temp/run.do_configure, toolchain.cmake and its
# recipe-sysroot(-native): run "bitbake -c configure luna-sysservice" first
# if rm_work cleaned it.
set -eu

W=$(cd "${1:?recipe workdir}" && pwd)
B=${2:?build dir}
shift 2
SRC=$(cd "$(dirname "$0")/../.." && pwd)
RUN="$W/temp/run.do_configure"
[ -f "$RUN" ] || { echo "no $RUN - bitbake -c configure the recipe first" >&2; exit 1; }
mkdir -p "$B"
B=$(cd "$B" && pwd)

# The task's environment, and its cmake call with this tree as the source
# (the recipe's source path is the line after $oecmake_sitefile).
grep '^export ' "$RUN" > "$B/oe-env.sh"
awk -v src="$SRC" '
	/^cmake_do_configure\(\)/ { infn = 1 }
	infn && /^\tcmake \\$/ { on = 1 }
	on && prev ~ /oecmake_sitefile/ { print "  \"" src "\" \\"; prev = $0; next }
	on { prev = $0; sub(/\$oecmake_sitefile/, ""); print }
	on && /-Wno-dev$/ { exit }
' "$RUN" > "$B/oe-cmake.sh"
grep -q -- '-Wno-dev$' "$B/oe-cmake.sh" || { echo "cannot find the cmake call in $RUN" >&2; exit 1; }

# The task environment has its own PATH; run in a subshell so the caller's
# stays as it is.
(
	. "$B/oe-env.sh"
	cd "$B"
	if [ ! -f build.ninja ]; then
		eval "$(sed 's/-Wno-dev$/-Wno-dev "$@"/' oe-cmake.sh)"
	fi
	ninja -j"${JOBS:-8}"
)
