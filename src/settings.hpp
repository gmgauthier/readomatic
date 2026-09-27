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
  double scroll = 0;
};

struct Highlight {
  std::string href;
  int start = 0;
  int end = 0;
  std::string colour = "yellow";
  std::string excerpt;
};

struct BookRecord {
  std::string path;
  std::string title;
  std::string href;
  std::string fragment;
  double scroll = 0;
  std::vector<Bookmark> bookmarks;
  std::vector<Highlight> highlights;
};

const char* highlight_colour_id(const std::string& colour);
const char* highlight_colour_label(const std::string& colour);
const char* highlight_tag_name(const std::string& colour);
const char* highlight_bg(const std::string& colour, int palette);

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
  std::string device_uri;
  std::string device_dir;
  std::string font_family = "Serif";
  int font_size = 12;
  int font_weight = 400;
  int palette = 1;  // 0 white, 1 eggshell, 2 dark
  std::vector<RecentItem> recent;
  std::map<std::string, BookRecord> books;

  static std::string key_for(const std::string& id);

  void load();
  void save() const;

  void touch_recent(const std::string& path, const std::string& title);
  BookRecord& book(const std::string& key);
};

}  // namespace readomatic
