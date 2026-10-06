/* SPDX-License-Identifier: Unlicense */

#include "book.hpp"
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
    char tmpl[] = "/tmp/readomatic-find-XXXXXX";
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
         "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body>" +
         body + "</body></html>";
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
      "<dc:title>Metamorphosis</dc:title><dc:identifier id=\"uid\">find</dc:identifier>"
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
  CHECK(add_zip(zip, "OEBPS/wrap0000.xhtml", chapter("<p>hello world secret</p>")));
  CHECK(add_zip(zip, "OEBPS/ch1.xhtml",
                chapter("<p>hello</p><p>world stays apart</p>"
                        "<p>hello<br/>world</p>"
                        "<p>then hello world again</p>"
                        "<p>Hello World</p>")));
  CHECK(add_zip(zip, "OEBPS/ch2.xhtml", chapter("<p>hello   world once</p>")));
  CHECK(archive_write_close(zip) == ARCHIVE_OK);
  archive_write_free(zip);

  readomatic::Book book;
  CHECK(book.open(epub));
  CHECK(book.search("h", 10).empty());

  const auto hits = book.search("hello world", 20);
  CHECK(hits.size() == 3);
  CHECK(hits[0].href == "ch1.xhtml");
  CHECK(hits[0].occurrence == 0);
  CHECK(hits[0].excerpt.find("hello world") != std::string::npos);
  CHECK(hits[1].href == "ch1.xhtml");
  CHECK(hits[1].occurrence == 1);
  CHECK(hits[1].excerpt.find("Hello World") != std::string::npos);
  CHECK(hits[2].href == "ch2.xhtml");
  CHECK(hits[2].occurrence == 0);
  CHECK(hits[2].excerpt.find("hello world") != std::string::npos);

  for (const auto& hit : hits)
    CHECK(hit.excerpt.find("secret") == std::string::npos);
  CHECK(book.search("secret", 10).empty());

  size_t b = 0;
  size_t e = 0;
  const std::string triple = "hello   world once";
  CHECK(readomatic::find_squeezed_occurrence(triple, "hello world", 0, b, e));
  CHECK(triple.substr(b, e - b) == "hello   world");
  const std::string nbsp = std::string("hello") + "\xC2\xA0" + "world";
  CHECK(readomatic::find_squeezed_occurrence(nbsp, "hello world", 0, b, e));
  CHECK(nbsp.substr(b, e - b) == nbsp);
  const std::string both = "hello   world and hello world";
  CHECK(readomatic::find_squeezed_occurrence(both, "hello world", 0, b, e));
  CHECK(both.substr(b, e - b) == "hello   world");
  CHECK(readomatic::find_squeezed_occurrence(both, "hello world", 1, b, e));
  CHECK(both.substr(b, e - b) == "hello world");
  CHECK(!readomatic::find_squeezed_occurrence("hello\nworld", "hello world", 0, b, e));
  const std::string shaped = "Hello   World";
  CHECK(readomatic::find_squeezed_occurrence(shaped, "hello world", 0, b, e));
  CHECK(shaped.substr(b, e - b) == shaped);

  return suite_test::done("find");
}
