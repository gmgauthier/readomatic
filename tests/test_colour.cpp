/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "topic_view.hpp"

#include <string>

int main()
{
  CHECK(std::string(readomatic::link_foreground(0)) == "#0B3A96");
  CHECK(std::string(readomatic::link_foreground(1)) == "#0B3A96");
  CHECK(std::string(readomatic::link_foreground(2)) == "#8CB4E8");
  CHECK(std::string(readomatic::link_foreground(3)) == "#0B3A96");
  CHECK(std::string(readomatic::link_foreground(-1)) == "#0B3A96");
  return suite_test::done("colour");
}
