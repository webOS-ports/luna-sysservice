// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SYSSERVICE_TESTS_TEMPDIR_H
#define SYSSERVICE_TESTS_TEMPDIR_H

#include <string>

#include <gtest/gtest.h>
#include <glib.h>
#include <glib/gstdio.h>

// A fresh directory under the test temp dir, removed with its contents when
// the test ends.
class TempDir
{
public:
	TempDir()
	{
		std::string tmpl = testing::TempDir() + "sysservice-test-XXXXXX";
		gchar *dir = g_mkdtemp(&tmpl[0]);
		if (dir)
			m_path = dir;
	}
	~TempDir()
	{
		if (!m_path.empty())
			removeTree(m_path);
	}
	TempDir(const TempDir&) = delete;
	TempDir& operator=(const TempDir&) = delete;

	const std::string& path() const { return m_path; }
	std::string file(const std::string& name) const { return m_path + "/" + name; }

	static bool write(const std::string& path, const std::string& content)
	{
		return g_file_set_contents(path.c_str(), content.data(),
		                           static_cast<gssize>(content.size()), nullptr);
	}

	static std::string read(const std::string& path)
	{
		gchar *data = nullptr;
		gsize len = 0;
		if (!g_file_get_contents(path.c_str(), &data, &len, nullptr))
			return std::string();
		std::string content(data, len);
		g_free(data);
		return content;
	}

private:
	static void removeTree(const std::string& path)
	{
		GDir *dir = g_dir_open(path.c_str(), 0, nullptr);
		if (dir) {
			const gchar *name;
			while ((name = g_dir_read_name(dir)) != nullptr) {
				std::string child = path + "/" + name;
				if (g_file_test(child.c_str(), G_FILE_TEST_IS_DIR) &&
				    !g_file_test(child.c_str(), G_FILE_TEST_IS_SYMLINK))
					removeTree(child);
				else
					g_unlink(child.c_str());
			}
			g_dir_close(dir);
		}
		g_rmdir(path.c_str());
	}

	std::string m_path;
};

#endif // SYSSERVICE_TESTS_TEMPDIR_H
