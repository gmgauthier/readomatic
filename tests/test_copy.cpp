/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "lastread.hpp"

#include <glibmm.h>

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
  TempDir cache;
  CHECK(!cache.path().empty());
  setenv("XDG_CACHE_HOME", cache.path().c_str(), 1);

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
  CHECK(readomatic::lastread_path(book) == book + ".lastread");
  CHECK(fs::is_regular_file(book + ".lastread"));

  CHECK(readomatic::book_location("/books/a.epub", "mtp://phone/a.epub") == "/books/a.epub");
  CHECK(readomatic::book_location("", "mtp://phone/a.epub") == "mtp://phone/a.epub");
  CHECK(readomatic::book_location("", "").empty());

  const std::string mtp = "mtp://[usb:001,002]/Internal storage/Tale.epub";
  const std::string gphoto = "gphoto2://[usb:001,003]/store_00010001/Tale.epub";
  const std::string mtp_side = readomatic::lastread_path(mtp);
  const std::string gphoto_side = readomatic::lastread_path(gphoto);
  CHECK(mtp_side != gphoto_side);
  CHECK(mtp_side != mtp + ".lastread");
  CHECK(mtp_side.find(cache.path()) == 0);
  CHECK(!fs::exists(fs::path(mtp_side).parent_path()));
  CHECK(!Glib::file_test(mtp, Glib::FILE_TEST_IS_REGULAR));

  readomatic::LastRead pos;
  pos.present = true;
  pos.href = "ch2.xhtml";
  pos.fragment = "p3";
  pos.scroll = 0.5;
  pos.cpage = 2;
  pos.npage = 9;
  readomatic::save_lastread(mtp, pos);
  const readomatic::LastRead from_mtp = readomatic::load_lastread(mtp);
  CHECK(fs::is_regular_file(mtp_side));
  CHECK(from_mtp.present);
  CHECK(from_mtp.href == "ch2.xhtml");
  CHECK(from_mtp.fragment == "p3");
  CHECK(from_mtp.scroll == 0.5);
  CHECK(from_mtp.cpage == 2);
  CHECK(from_mtp.npage == 9);
  CHECK(readomatic::same_last_page(pos, from_mtp));
  CHECK(readomatic::resolve_lastread(mtp, nullptr, "").href == "ch2.xhtml");

  readomatic::save_lastread(gphoto, pos);
  CHECK(fs::is_regular_file(gphoto_side));
  CHECK(readomatic::same_last_page(readomatic::load_lastread(mtp),
                                   readomatic::load_lastread(gphoto)));
  readomatic::LastRead other = pos;
  other.href = "other.xhtml";
  readomatic::save_lastread(gphoto, other);
  CHECK(readomatic::load_lastread(mtp).href == "ch2.xhtml");
  CHECK(readomatic::load_lastread(gphoto).href == "other.xhtml");
  CHECK(!readomatic::same_last_page(readomatic::load_lastread(mtp),
                                    readomatic::load_lastread(gphoto)));

  const std::string mtpfs = "mtpfs://device/Books/Other.epub";
  CHECK(readomatic::lastread_path(mtpfs) != mtp_side);
  readomatic::save_lastread(mtpfs, pos);
  CHECK(readomatic::load_lastread(mtpfs).href == "ch2.xhtml");

  const std::string upper = "MTP://device/Books/Tale.epub";
  const std::string upper_side = readomatic::lastread_path(upper);
  CHECK(upper_side != upper + ".lastread");
  CHECK(upper_side.find(cache.path()) == 0);
  CHECK(upper_side != mtp_side);

  CHECK(!readomatic::lastread_from_pocketbook(mtp, mtp).present);

  return suite_test::done("copy");
}
