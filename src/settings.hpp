/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <map>
#include <string>
#include <vector>

namespace readomatic {

struct Bookmark {
  std::string label;
  std::string href;
  std::string fragment;
};

struct BookRecord {
  std::string path;
  std::string title;
  std::string href;
  std::string fragment;
  double scroll = 0;
  std::vector<Bookmark> bookmarks;
};

struct RecentItem {
  std::string path;
  std::string title;
};

struct Settings {
  int window_x = -1;
  int window_y = -1;
  int window_w = 800;
  int window_h = 560;
  int paned = 220;
  std::string library_dir;
  std::vector<RecentItem> recent;
  std::map<std::string, BookRecord> books;

  static std::string key_for(const std::string& id);

  void load();
  void save() const;

  void touch_recent(const std::string& path, const std::string& title);
  BookRecord& book(const std::string& key);
};

}  // namespace readomatic
