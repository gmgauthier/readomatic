/* SPDX-License-Identifier: Unlicense */

#include "book.hpp"
#include "check.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

class TempDir {
 public:
  TempDir()
  {
    char tmpl[] = "/tmp/readomatic-spine-XXXXXX";
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
         "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Chapter</title></head>"
         "<body><p>" +
         body + "</p></body></html>";
}

struct Part {
  const char* id;
  const char* href;
  std::string html;
};

}  // namespace

int main()
{
  TempDir tmp;
  CHECK(!tmp.path().empty());
  setenv("XDG_CACHE_HOME", (tmp.path() + "/cache").c_str(), 1);

  const std::vector<Part> parts = {
      {"w", "wrap0000.xhtml", page("Frontispiece token.")},
      {"c", "Cover.xhtml", page("Front cover.")},
      {"t", "titlepage.xhtml", page("Title page body.")},
      {"b", "blank.xhtml", page(" ")},
      {"r", "recovered.xhtml", page("Recovered chapter.")},
      {"cov", "coverage.xhtml", page("Coverage chapter.")},
      {"u", "unwrapping.xhtml", page("Unwrapping chapter.")},
      {"tn", "titlepage-notes.xhtml", page("Notes on the title page.")},
      {"m", "mention.xhtml", page("The coverpage lists chapters.")},
      {"wn", "wrapper-notes.xhtml", page("Wrapper notes chapter.")},
  };

  std::string manifest;
  std::string spine;
  for (const auto& part : parts) {
    manifest += "<item id=\"";
    manifest += part.id;
    manifest += "\" href=\"";
    manifest += part.href;
    manifest += "\" media-type=\"application/xhtml+xml\"/>";
    spine += "<itemref idref=\"";
    spine += part.id;
    spine += "\"/>";
  }
  const std::string opf =
      "<?xml version=\"1.0\"?>"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"uid\">"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
      "<dc:title>Spine</dc:title><dc:identifier id=\"uid\">spine</dc:identifier>"
      "</metadata><manifest>" +
      manifest + "</manifest><spine>" + spine + "</spine></package>";
  const std::string container =
      "<?xml version=\"1.0\"?>"
      "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
      "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
      "media-type=\"application/oebps-package+xml\"/></rootfiles></container>";

  const std::string epub = tmp.path() + "/story.epub";
  archive* zip = archive_write_new();
  CHECK(archive_write_set_format_zip(zip) == ARCHIVE_OK);
  CHECK(archive_write_open_filename(zip, epub.c_str()) == ARCHIVE_OK);
  CHECK(add_zip(zip, "mimetype", "application/epub+zip"));
  CHECK(add_zip(zip, "META-INF/container.xml", container));
  CHECK(add_zip(zip, "OEBPS/content.opf", opf));
  for (const auto& part : parts)
    CHECK(add_zip(zip, ("OEBPS/" + std::string(part.href)).c_str(), part.html));
  CHECK(archive_write_close(zip) == ARCHIVE_OK);
  archive_write_free(zip);

  readomatic::Book book;
  CHECK(book.open(epub));
  CHECK(book.spine_count() == static_cast<int>(parts.size()));
  CHECK(book.start_href() == "recovered.xhtml");
  CHECK(book.current_href() == "recovered.xhtml");

  CHECK(!book.select_href("wrap0000.xhtml"));
  CHECK(!book.select_href("Cover.xhtml"));
  CHECK(!book.select_href("titlepage.xhtml"));
  CHECK(!book.select_href("blank.xhtml"));
  CHECK(book.select_href("recovered.xhtml"));
  CHECK(book.select_href("coverage.xhtml"));
  CHECK(book.select_href("unwrapping.xhtml"));
  CHECK(book.select_href("titlepage-notes.xhtml"));
  CHECK(book.select_href("mention.xhtml"));
  CHECK(book.select_href("wrapper-notes.xhtml"));

  CHECK(book.select_href("recovered.xhtml"));
  CHECK(book.advance_spine(1));
  CHECK(book.current_href() == "coverage.xhtml");
  CHECK(book.advance_spine(1));
  CHECK(book.current_href() == "unwrapping.xhtml");
  CHECK(book.advance_spine(1));
  CHECK(book.current_href() == "titlepage-notes.xhtml");
  CHECK(book.advance_spine(1));
  CHECK(book.current_href() == "mention.xhtml");
  CHECK(book.advance_spine(1));
  CHECK(book.current_href() == "wrapper-notes.xhtml");
  CHECK(!book.advance_spine(1));
  CHECK(book.current_href() == "wrapper-notes.xhtml");

  CHECK(book.search("Frontispiece", 20).empty());
  const auto recovered = book.search("Recovered", 20);
  CHECK(recovered.size() == 1);
  CHECK(recovered[0].href == "recovered.xhtml");
  const auto mentioned = book.search("coverpage", 20);
  CHECK(mentioned.size() == 1);
  CHECK(mentioned[0].href == "mention.xhtml");

  return suite_test::done("spine");
}
