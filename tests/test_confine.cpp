/* SPDX-License-Identifier: Unlicense */

#include "book.hpp"
#include "check.hpp"

#include <archive.h>
#include <archive_entry.h>

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
    char tmpl[] = "/tmp/readomatic-confine-XXXXXX";
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

std::string escape_href(const std::string& opf_dir, const std::string& absolute)
{
  const fs::path from(opf_dir);
  int ups = 0;
  for (auto it = from.begin(); it != from.end(); ++it)
    ++ups;
  if (ups > 0)
    --ups;
  std::string href;
  for (int i = 0; i < ups; ++i)
    href += "../";
  if (!absolute.empty() && absolute[0] == '/')
    href += absolute.substr(1);
  else
    href += absolute;
  return href;
}

}  // namespace

int main()
{
  TempDir tmp;
  CHECK(!tmp.path().empty());
  setenv("XDG_CACHE_HOME", (tmp.path() + "/cache").c_str(), 1);

  const std::string secret_path = tmp.path() + "/secret.txt";
  {
    std::ofstream out(secret_path);
    out << "OUTSIDE SECRET\n";
  }

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
      "<dc:title>Confine</dc:title><dc:identifier id=\"uid\">confine</dc:identifier>"
      "</metadata><manifest>"
      "<item id=\"ch\" href=\"ch.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "</manifest><spine><itemref idref=\"ch\"/></spine></package>";
  const std::string chapter =
      "<?xml version=\"1.0\"?>"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p>Inside the book.</p></body></html>";
  CHECK(add_zip(zip, "mimetype", "application/epub+zip"));
  CHECK(add_zip(zip, "META-INF/container.xml", container));
  CHECK(add_zip(zip, "OEBPS/content.opf", opf));
  CHECK(add_zip(zip, "OEBPS/ch.xhtml", chapter));
  CHECK(archive_write_close(zip) == ARCHIVE_OK);
  archive_write_free(zip);

  readomatic::Book book;
  CHECK(book.open(epub));
  CHECK(book.is_open());
  CHECK(book.load_document("ch.xhtml").find("Inside the book.") != std::string::npos);
  CHECK(book.load_document("ch.xhtml#p").find("Inside the book.") != std::string::npos);
  CHECK(book.load_document("../META-INF/container.xml").find("rootfile") != std::string::npos);

  const std::string escaped = escape_href(book.opf_dir(), secret_path);
  CHECK(book.resolve(escaped).empty());
  CHECK(book.load_document(escaped).empty());
  CHECK(book.load_document("../../../../../../etc/passwd").find("root:") == std::string::npos);

  const std::string absolute = book.resolve("/etc/passwd");
  CHECK(!absolute.empty());
  CHECK(absolute != "/etc/passwd");
  CHECK(absolute.rfind(book.extract_dir(), 0) == 0);
  CHECK(book.load_document("/etc/passwd").empty());

  const std::string alias = book.extract_dir() + "/OEBPS/alias.xhtml";
  CHECK(symlink((book.extract_dir() + "/OEBPS/ch.xhtml").c_str(), alias.c_str()) == 0);
  CHECK(book.load_document("alias.xhtml").find("Inside the book.") != std::string::npos);

  const std::string leak = book.extract_dir() + "/OEBPS/leak.xhtml";
  CHECK(symlink(secret_path.c_str(), leak.c_str()) == 0);
  CHECK(book.resolve("leak.xhtml").empty());
  CHECK(book.load_document("leak.xhtml").empty());

  return suite_test::done("confine");
}
