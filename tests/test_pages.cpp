/* SPDX-License-Identifier: Unlicense */

#include "book.hpp"
#include "check.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <filesystem>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

class TempDir {
 public:
  TempDir()
  {
    char tmpl[] = "/tmp/readomatic-pages-XXXXXX";
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

std::string page(const std::string& body)
{
  return "<?xml version=\"1.0\"?>"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p>" +
         body + "</p></body></html>";
}

void expect_pages(const readomatic::Book& book, int number, int total)
{
  int got_number = -1;
  int got_total = -1;
  book.readable_span(got_number, got_total);
  CHECK(got_number == number);
  CHECK(got_total == total);
  const std::string line = readomatic::topic_status_text("Metamorphosis", number - 1, total);
  CHECK(line.size() > std::string("Metamorphosis").size());
  const std::string suffix = " — " + std::to_string(number) + " of " + std::to_string(total);
  CHECK(line.compare(line.size() - suffix.size(), suffix.size(), suffix) == 0);
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
      "<dc:title>Metamorphosis</dc:title><dc:identifier id=\"uid\">pages</dc:identifier>"
      "</metadata><manifest>"
      "<item id=\"w\" href=\"wrap0000.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"a\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"b\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "</manifest><spine>"
      "<itemref idref=\"w\"/><itemref idref=\"a\"/><itemref idref=\"b\"/>"
      "</spine></package>";
  CHECK(add_zip(zip, "mimetype", "application/epub+zip"));
  CHECK(add_zip(zip, "META-INF/container.xml", container));
  CHECK(add_zip(zip, "OEBPS/content.opf", opf));
  CHECK(add_zip(zip, "OEBPS/wrap0000.xhtml", page("Frontispiece.")));
  CHECK(add_zip(zip, "OEBPS/ch1.xhtml", page("First chapter.")));
  CHECK(add_zip(zip, "OEBPS/ch2.xhtml", page("Second chapter.")));
  CHECK(archive_write_close(zip) == ARCHIVE_OK);
  archive_write_free(zip);

  readomatic::Book book;
  CHECK(book.open(epub));
  CHECK(book.spine_count() == 3);
  CHECK(book.current_href() == "ch1.xhtml");
  expect_pages(book, 1, 2);
  CHECK(book.advance_spine(1));
  CHECK(book.current_href() == "ch2.xhtml");
  expect_pages(book, 2, 2);
  CHECK(book.advance_spine(-1));
  expect_pages(book, 1, 2);
  CHECK(!book.advance_spine(-1));
  expect_pages(book, 1, 2);

  return suite_test::done("pages");
}
