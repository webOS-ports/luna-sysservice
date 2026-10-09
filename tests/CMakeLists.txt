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

# Unit tests, built only with -DWEBOS_CONFIG_BUILD_TESTS=TRUE so the default
# build is unchanged. gtest comes prebuilt from the sysroot, through
# pkg-config, rather than from source as webos_use_gtest() expects.
if(NOT WEBOS_CONFIG_BUILD_TESTS)
    return()
endif()

pkg_check_modules(GTEST REQUIRED gtest_main)
include_directories(${GTEST_INCLUDE_DIRS})

# The service's own sources without its main(), so the tests link against
# exactly the code the daemon runs.
set(COMPONENT_SOURCES ${SOURCE_FILES})
list(FILTER COMPONENT_SOURCES EXCLUDE REGEX "Src/Main\\.cpp$")
list(TRANSFORM COMPONENT_SOURCES PREPEND ${CMAKE_SOURCE_DIR}/)

add_library(sysservice-core STATIC ${COMPONENT_SOURCES})
target_link_libraries(sysservice-core
                      ${GLIB2_LDFLAGS}
                      ${GXML2_LDFLAGS}
                      ${SQLITE3_LDFLAGS}
                      ${PBNJSON_C_LDFLAGS}
                      ${PBNJSON_CPP_LDFLAGS}
                      ${LS2_LDFLAGS}
                      ${LS2++_LDFLAGS}
                      ${QtGui_LDFLAGS}
                      ${URIPARSER_LDFLAGS}
                      ${PMLOG_LDFLAGS}
                      ${NYXLIB_LDFLAGS}
                      ${WEBOSI18N_LDFLAGS}
                      rt
                      )
if (Qt6_FOUND)
    target_link_libraries(sysservice-core Qt::Core Qt::Gui)
endif()

file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_*.cpp)

webos_add_test(sysservice_unittests
    SOURCES ${TEST_SOURCES}
    LIBRARIES sysservice-core ${GTEST_LDFLAGS}
)
