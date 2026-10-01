/* SPDX-License-Identifier: Unlicense */

#include "history.hpp"
#include "lastread.hpp"
#include "check.hpp"

#include <cstdlib>
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

  return suite_test::done("place");
}
