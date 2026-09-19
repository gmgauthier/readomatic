/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "settings.hpp"

#include <string>

namespace readomatic {

struct LastRead {
  bool present = false;
  std::string href;
  std::string fragment;
  double scroll = 0;
  std::string pbr;
  int cpage = 0;
  int npage = 0;
};

std::string lastread_path(const std::string& book_path);
std::string canonical_lastread(const LastRead& pos);
LastRead load_lastread(const std::string& book_path);
void save_lastread(const std::string& book_path, const LastRead& pos);
LastRead lastread_from_settings(const Settings& settings, const std::string& book_path);
LastRead lastread_from_pocketbook(const std::string& device_root, const std::string& book_path);
LastRead resolve_lastread(const std::string& book_path, const Settings* settings,
                          const std::string& device_root);

}  // namespace readomatic
