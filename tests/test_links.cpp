/* SPDX-License-Identifier: Unlicense */

#include "book.hpp"
#include "check.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

class TempDir {
 public:
  TempDir()
  {
    char tmpl[] = "/tmp/readomatic-links-XXXXXX";
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
      "<dc:title>Links</dc:title><dc:identifier id=\"uid\">links</dc:identifier>"
      "</metadata><manifest>"
      "<item id=\"c1\" href=\"text/ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"c2\" href=\"text/ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"sp\" href=\"Chapter%201.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "</manifest><spine>"
      "<itemref idref=\"c1\"/><itemref idref=\"c2\"/><itemref idref=\"sp\"/>"
      "</spine></package>";
  CHECK(add_zip(zip, "mimetype", "application/epub+zip"));
  CHECK(add_zip(zip, "META-INF/container.xml", container));
  CHECK(add_zip(zip, "OEBPS/content.opf", opf));
  CHECK(
      add_zip(zip, "OEBPS/text/ch1.xhtml", page("<a href=\"ch2.xhtml\">Next</a> First chapter.")));
  CHECK(add_zip(zip, "OEBPS/text/ch2.xhtml", page("Second chapter.")));
  CHECK(add_zip(zip, "OEBPS/Chapter 1.xhtml", page("Spaced name.")));
  CHECK(add_zip(zip, "OEBPS/literal%20name.xhtml", page("Literal percent.")));
  CHECK(add_zip(zip, "OEBPS/a+b.xhtml", page("Plus sign.")));
  CHECK(archive_write_close(zip) == ARCHIVE_OK);
  archive_write_free(zip);

  readomatic::Book book;
  CHECK(book.open(epub));
  CHECK(book.spine_count() == 3);
  CHECK(book.select_href("text/ch1.xhtml"));
  CHECK(book.current_href() == "text/ch1.xhtml");

  const std::string ch1 = book.resolve("text/ch1.xhtml");
  const auto slash = ch1.find_last_of('/');
  CHECK(slash != std::string::npos);
  const std::string dir = ch1.substr(0, slash);
  const std::string ch2 = book.resolve("text/ch2.xhtml");
  CHECK(!ch2.empty());
  CHECK(book.resolve("ch2.xhtml") != ch2);
  CHECK(book.resolve_against("ch2.xhtml", dir) == ch2);
  CHECK(book.resolve_content_link("ch2.xhtml") == ch2);
  {
    std::ifstream in(ch2);
    std::ostringstream ss;
    ss << in.rdbuf();
    CHECK(ss.str().find("Second chapter.") != std::string::npos);
  }
  CHECK(book.load_document("ch2.xhtml").find("Second chapter.") == std::string::npos);
  CHECK(book.load_document("text/ch2.xhtml").find("Second chapter.") != std::string::npos);

  /* An OPF-relative spine href still resolves while the chapter is in text/. */
  CHECK(book.resolve_content_link("text/ch2.xhtml") == ch2);
  CHECK(book.resolve_content_link("text/ch1.xhtml") == ch1);
  CHECK(book.resolve("text%2Fch2.xhtml") == ch2);

  CHECK(book.load_document("Chapter%201.xhtml").find("Spaced name.") != std::string::npos);
  CHECK(book.load_document("Chapter%201.xhtml#note").find("Spaced name.") != std::string::npos);
  CHECK(book.select_href("Chapter%201.xhtml"));
  CHECK(book.current_href() == "Chapter%201.xhtml");
  CHECK(book.select_href("Chapter 1.xhtml"));
  CHECK(book.current_href() == "Chapter%201.xhtml");
  CHECK(book.resolve_against("../Chapter%201.xhtml", dir) == book.resolve("Chapter%201.xhtml"));

  const std::string literal = book.resolve("literal%2520name.xhtml");
  CHECK(book.load_document("literal%2520name.xhtml").find("Literal percent.") != std::string::npos);
  CHECK(book.resolve("literal%20name.xhtml") != literal);

  const std::string plus = book.resolve("a+b.xhtml");
  CHECK(book.resolve("a%2Bb.xhtml") == plus);
  CHECK(plus.find("a+b.xhtml") != std::string::npos);
  CHECK(book.load_document("a+b.xhtml").find("Plus sign.") != std::string::npos);

  const std::string encoded_escape = "%2e%2e/%2e%2e/%2e%2e/%2e%2e/%2e%2e/%2e%2e/%2e%2e/etc/passwd";
  CHECK(book.resolve(encoded_escape).empty());
  CHECK(book.resolve_against(encoded_escape, dir).empty());
  CHECK(book.resolve_content_link(encoded_escape).empty());
  CHECK(book.resolve("%00etc/passwd").empty());

  const std::string absolute = book.resolve("%2Fetc%2Fpasswd");
  CHECK(!absolute.empty());
  CHECK(absolute != "/etc/passwd");
  CHECK(absolute.rfind(book.extract_dir(), 0) == 0);

  return suite_test::done("links");
}
