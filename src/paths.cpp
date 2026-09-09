/* SPDX-License-Identifier: Unlicense */

#include "paths.hpp"
#include "config.hpp"

#include <glib.h>
#include <glibmm.h>

#include <vector>

namespace readomatic {
namespace {

bool exists_regular(const std::string& path)
{
  return Glib::file_test(path, Glib::FILE_TEST_IS_REGULAR);
}

}  // namespace

std::string find_data_file(const std::string& relative)
{
  std::vector<std::string> roots;

  if (const char* env = g_getenv("READOMATIC_DATA"))
    roots.emplace_back(env);

  roots.emplace_back(SOURCE_ROOT);
  roots.emplace_back(std::string(SOURCE_ROOT) + "/data");
  roots.emplace_back(DATADIR);

  for (const auto& root : roots) {
    const std::string candidate = Glib::build_filename(root, relative);
    if (exists_regular(candidate))
      return candidate;
  }
  return {};
}

}  // namespace readomatic
