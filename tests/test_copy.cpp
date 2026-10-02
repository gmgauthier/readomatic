/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "lastread.hpp"

#include <filesystem>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

class TempDir {
 public:
  TempDir()
  {
    char tmpl[] = "/tmp/readomatic-copy-XXXXXX";
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

}  // namespace

int main()
{
  const std::string tale = "/library/tale.epub";
  readomatic::Settings live;
  readomatic::BookRecord rec;
  rec.path = tale;
  rec.href = "ch1.xhtml";
  rec.fragment = "p";
  rec.scroll = 0.25;
  rec.highlights.push_back({"ch1.xhtml", 1, 4, "yellow", "once"});
  live.books["tale"] = rec;

  const readomatic::Settings snap = readomatic::books_for_transfer(live);
  live.books["tale"].href = "ch9.xhtml";
  live.books["tale"].highlights.clear();
  readomatic::BookRecord late;
  late.path = "/library/late.epub";
  late.href = "late.xhtml";
  live.books["late"] = late;
  live.books.erase("tale");

  const readomatic::LastRead from_snap = readomatic::lastread_from_settings(snap, tale);
  CHECK(from_snap.present);
  CHECK(from_snap.href == "ch1.xhtml");
  CHECK(from_snap.fragment == "p");
  CHECK(from_snap.scroll == 0.25);
  CHECK(!readomatic::lastread_from_settings(live, tale).present);
  CHECK(!readomatic::lastread_from_settings(snap, "/library/late.epub").present);

  const readomatic::LastRead resolved = readomatic::resolve_lastread(tale, &snap, "");
  CHECK(resolved.present);
  CHECK(resolved.href == "ch1.xhtml");
  CHECK(readomatic::same_last_page(from_snap, resolved));

  TempDir dir;
  CHECK(!dir.path().empty());
  const std::string book = dir.path() + "/tale.epub";
  readomatic::LastRead side;
  side.present = true;
  side.href = "side.xhtml";
  side.fragment = "s";
  side.scroll = 0.5;
  readomatic::save_lastread(book, side);
  const readomatic::LastRead from_file = readomatic::resolve_lastread(book, &snap, "");
  CHECK(from_file.present);
  CHECK(from_file.href == "side.xhtml");
  CHECK(!readomatic::same_last_page(from_snap, from_file));

  return suite_test::done("copy");
}
