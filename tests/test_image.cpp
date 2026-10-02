/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "topic_view.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

class TempDir {
 public:
  TempDir()
  {
    char tmpl[] = "/tmp/readomatic-image-XXXXXX";
    if (char* made = mkdtemp(tmpl))
      path_ = made;
  }

  ~TempDir()
  {
    if (!path_.empty())
      fs::remove_all(path_);
  }

  const std::string& path() const
  {
    return path_;
  }

 private:
  std::string path_;
};

void write_file(const std::string& path, const std::string& body)
{
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream out(path);
  out << body;
}

std::string read_file(const std::string& path)
{
  std::ifstream in(path);
  std::string body;
  std::getline(in, body);
  return body;
}

}  // namespace

int main()
{
  TempDir tmp;
  CHECK(!tmp.path().empty());
  const std::string root = tmp.path() + "/book";
  const std::string base = root + "/OEBPS";
  const std::string picture = base + "/pic.png";
  const std::string secret = tmp.path() + "/secret.png";
  write_file(picture, "INSIDE");
  write_file(secret, "OUTSIDE");

  const std::string shown = readomatic::topic_image_path("pic.png", base, root);
  CHECK(shown.rfind(root, 0) == 0);
  CHECK(read_file(shown) == "INSIDE");
  CHECK(readomatic::topic_image_path("pic.png#frag", base, root) == shown);

  const fs::path from(base);
  int ups = 0;
  for (auto it = from.begin(); it != from.end(); ++it)
    ++ups;
  if (ups > 0)
    --ups;
  std::string escaped;
  for (int i = 0; i < ups; ++i)
    escaped += "../";
  escaped += secret.substr(1);
  CHECK(readomatic::topic_image_path(escaped, base, root).empty());
  CHECK(readomatic::topic_image_path("../../../../../../secret.png", base, root).empty());

  const std::string absolute = readomatic::topic_image_path("/etc/passwd", base, root);
  CHECK(!absolute.empty());
  CHECK(absolute != "/etc/passwd");
  CHECK(absolute.rfind(root, 0) == 0);

  CHECK(readomatic::topic_image_path("", base, root).empty());
  CHECK(readomatic::topic_image_path("pic.png", base, "").empty());

  const std::string alias = base + "/alias.png";
  CHECK(symlink(picture.c_str(), alias.c_str()) == 0);
  const std::string aliased = readomatic::topic_image_path("alias.png", base, root);
  CHECK(aliased.rfind(root, 0) == 0);
  CHECK(read_file(aliased) == "INSIDE");

  const std::string leak = base + "/leak.png";
  CHECK(symlink(secret.c_str(), leak.c_str()) == 0);
  CHECK(readomatic::topic_image_path("leak.png", base, root).empty());

  return suite_test::done("image");
}
