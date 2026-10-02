/* SPDX-License-Identifier: Unlicense */

#include "history.hpp"
#include "lastread.hpp"
#include "check.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int main()
{
  {
    readomatic::History history;
    CHECK(history.empty());
    readomatic::History::Entry back;
    CHECK(!history.back(back));
    history.push("ch1.xhtml", "p1");
    history.update_scroll(0.25);
    history.push("ch1.xhtml", "p1");
    history.push("ch2.xhtml", "");
    CHECK(!history.empty());
    CHECK(history.back(back));
    CHECK(back.href == "ch1.xhtml");
    CHECK(back.fragment == "p1");
    CHECK(back.scroll == 0.25);
    CHECK(!history.back(back));
  }

  {
    readomatic::LastRead missing;
    CHECK(readomatic::canonical_lastread(missing).empty());
    readomatic::LastRead pos;
    pos.present = true;
    pos.href = "chapter.xhtml";
    pos.fragment = "note";
    pos.scroll = 0.5;
    CHECK(readomatic::canonical_lastread(pos) == "chapter.xhtml|note|0.5");
    pos.cpage = 3;
    pos.npage = 10;
    CHECK(readomatic::canonical_lastread(pos) == "chapter.xhtml|note|0.5|p3/10");
    pos.pbr = "epubcfi(/6/4!/4)";
    CHECK(readomatic::canonical_lastread(pos) == "epubcfi(/6/4!/4)");
  }

  {
    const std::string dir =
        "/tmp/readomatic-test-" + std::to_string(static_cast<long long>(getpid()));
    mkdir(dir.c_str(), 0700);
    const std::string book = dir + "/story.epub";
    readomatic::LastRead pos;
    pos.present = true;
    pos.href = "text/ch.xhtml";
    pos.fragment = "s";
    pos.scroll = 0.125;
    pos.cpage = 2;
    pos.npage = 8;
    readomatic::save_lastread(book, pos);
    const readomatic::LastRead loaded = readomatic::load_lastread(book);
    CHECK(loaded.present);
    CHECK(loaded.href == pos.href);
    CHECK(loaded.fragment == pos.fragment);
    CHECK(loaded.scroll > 0.12 && loaded.scroll < 0.13);
    CHECK(loaded.cpage == 2);
    CHECK(loaded.npage == 8);
    CHECK(!readomatic::load_lastread(dir + "/missing.epub").present);
    std::remove(readomatic::lastread_path(book).c_str());
    rmdir(dir.c_str());
  }

  {
    readomatic::LastRead cfi;
    cfi.pbr = "epubcfi(/6/4!/4/2:0)";
    cfi.cpage = 0;
    cfi.npage = 40;
    CHECK(readomatic::canonical_lastread(cfi) == cfi.pbr);

    const std::string dir =
        "/tmp/readomatic-cpage-" + std::to_string(static_cast<long long>(getpid()));
    mkdir(dir.c_str(), 0700);
    const std::string book = dir + "/pocket.epub";
    {
      std::ofstream side(readomatic::lastread_path(book));
      side << "[position]\n"
           << "pbr=" << cfi.pbr << "\n"
           << "cpage=0\n"
           << "npage=40\n";
    }
    const readomatic::LastRead loaded = readomatic::load_lastread(book);
    CHECK(loaded.present);
    CHECK(loaded.pbr == cfi.pbr);
    CHECK(loaded.cpage == 0);
    CHECK(loaded.npage == 40);
    CHECK(readomatic::canonical_lastread(loaded) == cfi.pbr);

    readomatic::Settings settings;
    const readomatic::LastRead resolved = readomatic::resolve_lastread(book, &settings, "");
    CHECK(resolved.present);
    CHECK(resolved.pbr == cfi.pbr);

    readomatic::save_lastread(book, loaded);
    const readomatic::LastRead round = readomatic::load_lastread(book);
    CHECK(round.present);
    CHECK(round.pbr == cfi.pbr);
    CHECK(round.cpage == 0);

    readomatic::LastRead unread;
    unread.cpage = 0;
    readomatic::save_lastread(book, unread);
    CHECK(!readomatic::load_lastread(book).present);

    const std::string device = dir + "/device";
    const std::string dbdir = device + "/system/explorer-3";
    std::filesystem::create_directories(dbdir);
    const std::string db = dbdir + "/explorer-3.db";
    {
      std::ofstream sql(dir + "/pocket.sql");
      sql << "CREATE TABLE files (filename TEXT, book_id INTEGER);\n"
          << "CREATE TABLE books_settings (bookid INTEGER, position TEXT, cpage INTEGER, npage "
             "INTEGER);\n"
          << "INSERT INTO files VALUES ('pocket.epub', 7);\n"
          << "INSERT INTO books_settings VALUES (7, 'epubcfi(/6/2!/4)', 0, 12);\n"
          << "INSERT INTO files VALUES ('fresh.epub', 8);\n"
          << "INSERT INTO books_settings VALUES (8, '', 0, 0);\n";
    }
    const std::string import = "sqlite3 \"" + db + "\" < \"" + dir + "/pocket.sql\"";
    CHECK(std::system(import.c_str()) == 0);
    const readomatic::LastRead from_device =
        readomatic::lastread_from_pocketbook(device, dir + "/library/pocket.epub");
    CHECK(from_device.present);
    CHECK(from_device.pbr == "epubcfi(/6/2!/4)");
    CHECK(from_device.cpage == 0);
    CHECK(from_device.npage == 12);
    CHECK(readomatic::canonical_lastread(from_device) == from_device.pbr);
    const readomatic::LastRead fresh =
        readomatic::lastread_from_pocketbook(device, dir + "/library/fresh.epub");
    CHECK(!fresh.present);
    CHECK(fresh.cpage == 0);

    std::filesystem::remove_all(dir);
  }

  {
    readomatic::LastRead unread_a;
    readomatic::LastRead unread_b;
    CHECK(readomatic::canonical_lastread(unread_a).empty());
    CHECK(readomatic::canonical_lastread(unread_b).empty());
    CHECK(!readomatic::same_last_page(unread_a, unread_b));

    readomatic::LastRead page;
    page.present = true;
    page.href = "ch.xhtml";
    page.fragment = "p";
    page.scroll = 0.25;
    readomatic::LastRead twin = page;
    CHECK(readomatic::same_last_page(page, twin));
    readomatic::LastRead other = page;
    other.href = "ch2.xhtml";
    CHECK(!readomatic::same_last_page(page, other));
    CHECK(!readomatic::same_last_page(page, unread_a));

    readomatic::LastRead cfi;
    cfi.present = true;
    cfi.pbr = "epubcfi(/6/4!/4)";
    cfi.cpage = 0;
    readomatic::LastRead cfi_twin = cfi;
    CHECK(readomatic::same_last_page(cfi, cfi_twin));
    CHECK(!readomatic::same_last_page(cfi, unread_a));

    const std::string dir =
        "/tmp/readomatic-transfer-" + std::to_string(static_cast<long long>(getpid()));
    mkdir(dir.c_str(), 0700);
    const std::string src = dir + "/story.epub";
    const std::string dest = dir + "/story-copy.epub";
    std::ofstream(src) << "src";
    std::ofstream(dest) << "old";
    const readomatic::LastRead src_pos = readomatic::resolve_lastread(src, nullptr, "");
    const readomatic::LastRead dest_pos = readomatic::resolve_lastread(dest, nullptr, "");
    CHECK(!src_pos.present);
    CHECK(!dest_pos.present);
    CHECK(!readomatic::same_last_page(src_pos, dest_pos));

    readomatic::LastRead saved = page;
    readomatic::save_lastread(src, saved);
    readomatic::save_lastread(dest, saved);
    CHECK(readomatic::same_last_page(readomatic::load_lastread(src),
                                     readomatic::load_lastread(dest)));
    saved.href = "other.xhtml";
    readomatic::save_lastread(dest, saved);
    CHECK(!readomatic::same_last_page(readomatic::load_lastread(src),
                                      readomatic::load_lastread(dest)));

    std::filesystem::remove_all(dir);
  }

  return suite_test::done("place");
}
