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
    char tmpl[] = "/tmp/readomatic-status-XXXXXX";
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

std::string suffix_for(int page_index, int page_count)
{
  return " — " + std::to_string(page_index + 1) + " of " + std::to_string(page_count);
}

void check_status(const std::string& title, int page_index, int page_count)
{
  const std::string text = readomatic::topic_status_text(title, page_index, page_count);
  const std::string suffix = suffix_for(page_index, page_count);
  CHECK(text.size() == title.size() + suffix.size());
  CHECK(text.compare(0, title.size(), title) == 0);
  CHECK(text.compare(title.size(), suffix.size(), suffix) == 0);
}

}  // namespace

int main()
{
  check_status("Kafka", 0, 3);
  CHECK(readomatic::topic_status_text("Kafka", 0, 3) == "Kafka — 1 of 3");

  const std::string ascii(180, 'A');
  check_status(ascii, 11, 339);
  CHECK(readomatic::topic_status_text(ascii, 11, 339).size() > 160);

  std::string marks;
  for (int i = 0; i < 80; ++i)
    marks += "—";
  check_status(marks, 0, 1);
  CHECK(readomatic::topic_status_text(marks, 0, 1).find("— — 1 of 1") != std::string::npos);

  const std::string mixed = std::string(150, 'B') + " — already";
  check_status(mixed, 9, 40);

  TempDir tmp;
  CHECK(!tmp.path().empty());
  setenv("XDG_CACHE_HOME", (tmp.path() + "/cache").c_str(), 1);
  const std::string title = std::string(150, 'C') + "—tail";
  const std::string epub = tmp.path() + "/long.epub";
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
      "<dc:title>" +
      title +
      "</dc:title><dc:identifier id=\"uid\">status</dc:identifier>"
      "</metadata><manifest>"
      "<item id=\"ch\" href=\"ch.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "</manifest><spine><itemref idref=\"ch\"/></spine></package>";
  const std::string chapter =
      "<?xml version=\"1.0\"?>"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p>Page.</p></body></html>";
  CHECK(add_zip(zip, "mimetype", "application/epub+zip"));
  CHECK(add_zip(zip, "META-INF/container.xml", container));
  CHECK(add_zip(zip, "OEBPS/content.opf", opf));
  CHECK(add_zip(zip, "OEBPS/ch.xhtml", chapter));
  CHECK(archive_write_close(zip) == ARCHIVE_OK);
  archive_write_free(zip);

  readomatic::Book book;
  CHECK(book.open(epub));
  CHECK(book.title() == title);
  check_status(book.title(), book.spine_index(), book.spine_count());
  CHECK(readomatic::topic_status_text(book.title(), book.spine_index(), book.spine_count())
            .find("1 of 1") != std::string::npos);

  return suite_test::done("status");
}
