/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <glib.h>

#include <map>
#include <string>
#include <vector>

namespace readomatic {

struct CatalogBook {
  std::string rel;
  std::string abs;
  std::string group;
  std::string name;
  guint64 size = 0;
  std::vector<std::string> tags;
};

struct LibraryCatalog {
  std::vector<std::string> groups;
  std::map<std::string, std::vector<std::string>> tags_by_rel;

  static std::string sidecar_path(const std::string& library_dir);
  static LibraryCatalog load(const std::string& library_dir);
  void save(const std::string& library_dir) const;

  std::vector<std::string> tags_for(const std::string& rel) const;
  void set_tags(const std::string& rel, const std::vector<std::string>& tags);
  void forget(const std::string& rel);
};

bool is_ebook_name(const std::string& name);
std::string join_tags(const std::vector<std::string>& tags);
std::vector<std::string> split_tags(const std::string& text);
std::string sanitize_group(const std::string& name);
std::vector<CatalogBook> scan_library(const std::string& library_dir, const LibraryCatalog& cat);

}  // namespace readomatic
