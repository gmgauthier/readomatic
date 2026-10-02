/* SPDX-License-Identifier: Unlicense */

#include "book.hpp"
#include "check.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

class TempDir {
 public:
  TempDir()
  {
    char tmpl[] = "/tmp/readomatic-open-XXXXXX";
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

bool write_zip(const std::string& path,
               const std::vector<std::pair<std::string, std::string>>& files)
{
  archive* zip = archive_write_new();
  if (archive_write_set_format_zip(zip) != ARCHIVE_OK)
    return false;
  if (archive_write_open_filename(zip, path.c_str()) != ARCHIVE_OK)
    return false;
  bool ok = true;
  for (const auto& file : files)
    ok = ok && add_zip(zip, file.first.c_str(), file.second);
  ok = ok && archive_write_close(zip) == ARCHIVE_OK;
  archive_write_free(zip);
  return ok;
}

const char* kContainer =
    "<?xml version=\"1.0\"?>"
    "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
    "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
    "media-type=\"application/oebps-package+xml\"/></rootfiles></container>";

const char* kChapter =
    "<?xml version=\"1.0\"?>"
    "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p>Readable.</p></body></html>";

std::string opf_with_spine(const std::string& spine)
{
  return "<?xml version=\"1.0\"?>"
         "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" "
         "unique-identifier=\"uid\">"
         "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
         "<dc:title>Open</dc:title><dc:identifier id=\"uid\">open</dc:identifier>"
         "</metadata><manifest>"
         "<item id=\"ch\" href=\"ch.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "</manifest><spine>" +
         spine + "</spine></package>";
}

}  // namespace

int main()
{
  TempDir tmp;
  CHECK(!tmp.path().empty());
  setenv("XDG_CACHE_HOME", (tmp.path() + "/cache").c_str(), 1);

  const std::string good = tmp.path() + "/good.epub";
  CHECK(write_zip(good, {{"mimetype", "application/epub+zip"},
                         {"META-INF/container.xml", kContainer},
                         {"OEBPS/content.opf", opf_with_spine("<itemref idref=\"ch\"/>")},
                         {"OEBPS/ch.xhtml", kChapter}}));

  readomatic::Book book;
  CHECK(book.open(good));
  CHECK(book.is_open());
  CHECK(book.error().empty());
  CHECK(book.title() == "Open");
  CHECK(book.load_document("ch.xhtml").find("Readable.") != std::string::npos);

  const std::string junk = tmp.path() + "/notes.epub";
  {
    std::ofstream out(junk);
    out << "this is not a zip";
  }
  CHECK(!book.open(junk));
  CHECK(!book.is_open());
  CHECK(book.error() == "Not a zip/EPUB file.");
  CHECK(book.title().empty());

  const std::string mobi = tmp.path() + "/notes.mobi";
  {
    std::ofstream out(mobi, std::ios::binary);
    out << std::string(80, '\0');
  }
  CHECK(!book.open(mobi));
  CHECK(!book.is_open());
  CHECK(book.error() == "Not a MOBI/AZW file.");

  const std::string bare = tmp.path() + "/bare.epub";
  CHECK(write_zip(bare, {{"mimetype", "application/epub+zip"}}));
  CHECK(!book.open(bare));
  CHECK(book.error() == "No META-INF/container.xml.");

  const std::string bad_xml = tmp.path() + "/bad-xml.epub";
  CHECK(write_zip(
      bad_xml, {{"mimetype", "application/epub+zip"}, {"META-INF/container.xml", "<container"}}));
  CHECK(!book.open(bad_xml));
  CHECK(book.error() == "Could not parse container.xml.");

  const std::string no_root = tmp.path() + "/no-root.epub";
  const char* empty_roots =
      "<?xml version=\"1.0\"?>"
      "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
      "<rootfiles></rootfiles></container>";
  CHECK(write_zip(no_root,
                  {{"mimetype", "application/epub+zip"}, {"META-INF/container.xml", empty_roots}}));
  CHECK(!book.open(no_root));
  CHECK(book.error() == "container.xml has no rootfile.");

  const std::string missing_opf = tmp.path() + "/missing-opf.epub";
  CHECK(write_zip(missing_opf,
                  {{"mimetype", "application/epub+zip"}, {"META-INF/container.xml", kContainer}}));
  CHECK(!book.open(missing_opf));
  CHECK(book.error() == "Could not parse OPF.");

  const std::string empty_spine = tmp.path() + "/empty-spine.epub";
  CHECK(write_zip(empty_spine, {{"mimetype", "application/epub+zip"},
                                {"META-INF/container.xml", kContainer},
                                {"OEBPS/content.opf", opf_with_spine("")}}));
  CHECK(!book.open(empty_spine));
  CHECK(!book.is_open());
  CHECK(book.error() == "OPF spine is empty.");

  CHECK(book.open(good));
  CHECK(book.is_open());
  CHECK(book.error().empty());
  CHECK(book.title() == "Open");
  book.close();
  CHECK(!book.is_open());
  CHECK(book.error().empty());

  CHECK(!book.open(tmp.path() + "/missing.epub"));
  CHECK(book.error() == "Not a zip/EPUB file.");

  return suite_test::done("open");
}
