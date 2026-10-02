/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "library_catalog.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

class TempDir {
 public:
  TempDir()
  {
    char tmpl[] = "/tmp/readomatic-catalog-XXXXXX";
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

void write_file(const std::string& path, const std::string& body)
{
  std::ofstream out(path);
  out << body;
}

const readomatic::CatalogBook* find_rel(const std::vector<readomatic::CatalogBook>& books,
                                        const std::string& rel)
{
  for (const auto& b : books) {
    if (b.rel == rel)
      return &b;
  }
  return nullptr;
}

}  // namespace

int main()
{
  TempDir dir;
  CHECK(!dir.path().empty());
  const std::string root = dir.path();
  const fs::path fiction = fs::path(root) / "fiction";
  fs::create_directory(fiction);
  write_file(root + "/fiction/tale.epub", "old");
  write_file(root + "/fiction/notes.epub", "notes");
  write_file(root + "/other.epub", "keep-book");

  readomatic::LibraryCatalog cat;
  cat.groups = {"fiction"};
  cat.set_tags("fiction/tale.epub", {"unread", "sf"});
  cat.set_tags("fiction/notes.epub", {"essay"});
  cat.set_tags("other.epub", {"keep"});
  cat.set_tags("", {"ignored"});
  cat.save(root);

  auto loaded = readomatic::LibraryCatalog::load(root);
  const auto before = readomatic::scan_library(root, loaded);
  const auto* tale = find_rel(before, "fiction/tale.epub");
  CHECK(tale != nullptr);
  CHECK(tale && tale->tags.size() == 2);
  CHECK(tale && tale->tags[0] == "sf");
  CHECK(tale && tale->tags[1] == "unread");

  loaded.forget("");
  loaded.forget("missing.epub");
  loaded.forget("fiction/tale.epub");
  loaded.save(root);

  const fs::path old_tale = fiction / "tale.epub";
  fs::remove(old_tale);
  write_file(root + "/fiction/tale.epub", "new");

  auto again = readomatic::LibraryCatalog::load(root);
  CHECK(again.tags_for("fiction/tale.epub").empty());
  CHECK(again.tags_for("other.epub").size() == 1);
  CHECK(again.tags_for("other.epub").size() == 1 && again.tags_for("other.epub")[0] == "keep");
  CHECK(again.tags_for("fiction/notes.epub").size() == 1);
  CHECK(again.tags_for("fiction/notes.epub").size() == 1 &&
        again.tags_for("fiction/notes.epub")[0] == "essay");
  CHECK(again.groups.size() == 1);
  CHECK(again.groups.size() == 1 && again.groups[0] == "fiction");

  const auto books = readomatic::scan_library(root, again);
  const auto* replaced = find_rel(books, "fiction/tale.epub");
  const auto* other = find_rel(books, "other.epub");
  const auto* notes = find_rel(books, "fiction/notes.epub");
  CHECK(replaced != nullptr);
  CHECK(replaced && replaced->tags.empty());
  CHECK(other != nullptr);
  CHECK(other && other->tags.size() == 1 && other->tags[0] == "keep");
  CHECK(notes != nullptr);
  CHECK(notes && notes->tags.size() == 1 && notes->tags[0] == "essay");

  return suite_test::done("catalog");
}
