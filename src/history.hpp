/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>
#include <vector>

namespace readomatic {

class History {
 public:
  struct Entry {
    std::string href;
    std::string fragment;
    double scroll = 0;
  };

  void clear() { stack_.clear(); }

  void push(const std::string& href, const std::string& fragment)
  {
    if (!stack_.empty() && stack_.back().href == href && stack_.back().fragment == fragment)
      return;
    stack_.push_back({href, fragment, 0});
  }

  void update_scroll(double scroll)
  {
    if (!stack_.empty())
      stack_.back().scroll = scroll;
  }

  bool back(Entry& out)
  {
    if (stack_.size() < 2)
      return false;
    stack_.pop_back();
    out = stack_.back();
    return true;
  }

  bool empty() const { return stack_.empty(); }

 private:
  std::vector<Entry> stack_;
};

}  // namespace readomatic
