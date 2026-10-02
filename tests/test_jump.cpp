/* SPDX-License-Identifier: Unlicense */

#include "book.hpp"
#include "history.hpp"
#include "check.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

class TempDir {
 public:
  TempDir()
  {
    char tmpl[] = "/tmp/readomatic-jump-XXXXXX";
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

bool add_zip(archive* zip, const char* name, const std::string& body)
{
  archive_entry* entry = archive_entry_new();
  archive_entry_set_pathname(entry, name);
  archive_entry_set_size(entry, static_cast<la_int64_t>(body.size()));
  archive_entry_set_filetype(entry, AE_IFREG);
  archive_entry_set_perm(entry, 0644);
  const bool ok =
      archive_write_header(zip, entry) == ARCHIVE_OK &&
      archive_write_data(zip, body.data(), body.size()) == static_cast<la_ssize_t>(body.size());
  archive_entry_free(entry);
  return ok;
}

std::string chapter(const std::string& body)
{
  return "<?xml version=\"1.0\"?>"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p>" +
         body + "</p></body></html>";
}

}  // namespace

int main()
{
  TempDir tmp;
  CHECK(!tmp.path().empty());
  setenv("XDG_CACHE_HOME", (tmp.path() + "/cache").c_str(), 1);

  const std::string epub = tmp.path() + "/story.epub";
  archive* zip = archive_write_new();
  CHECK(archive_write_set_format_zip(zip) == ARCHIVE_OK);
  CHECK(archive_write_open_filename(zip, epub.c_str()) == ARCHIVE_OK);
  const std::string container =
      "<?xml version=\"1.0\"?>"
      "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
      "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
      "media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
  const std::string opf =
      "<?xml version=\"1.0\"?>"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"uid\">"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
      "<dc:title>Metamorphosis</dc:title><dc:identifier id=\"uid\">jump</dc:identifier>"
      "</metadata><manifest>"
      "<item id=\"a\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"b\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "</manifest><spine>"
      "<itemref idref=\"a\"/><itemref idref=\"b\"/>"
      "</spine></package>";
  CHECK(add_zip(zip, "mimetype", "application/epub+zip"));
  CHECK(add_zip(zip, "META-INF/container.xml", container));
  CHECK(add_zip(zip, "OEBPS/content.opf", opf));
  CHECK(add_zip(zip, "OEBPS/ch1.xhtml", chapter("Chapter one.")));
  CHECK(add_zip(zip, "OEBPS/ch2.xhtml", chapter("Chapter two.")));
  CHECK(add_zip(zip, "OEBPS/toc.xhtml", chapter("Notes live here.")));
  CHECK(archive_write_close(zip) == ARCHIVE_OK);
  archive_write_free(zip);

  readomatic::Book book;
  CHECK(book.open(epub));
  CHECK(!book.off_spine());
  CHECK(book.current_href() == "ch1.xhtml");
  CHECK(book.spine_index() == 0);

  const std::string toc = book.resolve("toc.xhtml");
  CHECK(!toc.empty());
  CHECK(book.follow_path(toc));
  CHECK(book.off_spine());
  CHECK(book.current_href() == "toc.xhtml");
  CHECK(book.spine_index() == 0);
  CHECK(book.load_document(book.current_href()).find("Notes live here.") != std::string::npos);
  CHECK(readomatic::off_spine_status(book.title(), book.current_href()) ==
        "Metamorphosis — toc.xhtml");

  readomatic::History history;
  history.push("ch1.xhtml", "");
  history.push(book.current_href(), "note");

  CHECK(book.advance_spine(1));
  CHECK(!book.off_spine());
  CHECK(book.current_href() == "ch1.xhtml");
  CHECK(book.spine_index() == 0);
  CHECK(book.advance_spine(1));
  CHECK(book.current_href() == "ch2.xhtml");

  CHECK(book.follow_path(toc));
  CHECK(book.off_spine());
  CHECK(book.spine_index() == 1);
  CHECK(book.advance_spine(-1));
  CHECK(!book.off_spine());
  CHECK(book.current_href() == "ch2.xhtml");

  const std::string ch1 = book.resolve("ch1.xhtml");
  CHECK(book.follow_path(ch1));
  CHECK(!book.off_spine());
  CHECK(book.current_href() == "ch1.xhtml");
  CHECK(book.spine_index() == 0);
  CHECK(!book.follow_path(tmp.path() + "/missing.xhtml"));
  CHECK(book.current_href() == "ch1.xhtml");
  CHECK(!book.off_spine());

  readomatic::History::Entry back;
  CHECK(history.back(back));
  CHECK(back.href == "ch1.xhtml");
  CHECK(back.fragment.empty());

  return suite_test::done("jump");
}
