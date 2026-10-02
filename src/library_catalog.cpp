/* SPDX-License-Identifier: Unlicense */

#include "library_catalog.hpp"

#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/keyfile.h>
#include <glibmm/miscutils.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace readomatic {
namespace {

namespace fs = std::filesystem;

std::string to_lower(std::string s)
{
  for (char& c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string get_str(Glib::KeyFile& kf, const Glib::ustring& group, const char* key)
{
  try {
    if (kf.has_key(group, key))
      return kf.get_string(group, key);
  } catch (const Glib::Error&) {
  }
  return {};
}

}  // namespace

bool is_ebook_name(const std::string& name)
{
  const std::string n = to_lower(name);
  return n.size() > 4 &&
         (n.compare(n.size() - 5, 5, ".epub") == 0 || n.compare(n.size() - 4, 4, ".pdf") == 0 ||
          n.compare(n.size() - 5, 5, ".mobi") == 0 || n.compare(n.size() - 4, 4, ".fb2") == 0 ||
          n.compare(n.size() - 4, 4, ".azw") == 0 ||
          (n.size() > 5 && n.compare(n.size() - 5, 5, ".azw3") == 0));
}

std::string join_tags(const std::vector<std::string>& tags)
{
  std::string out;
  for (const auto& t : tags) {
    if (t.empty())
      continue;
    if (!out.empty())
      out += ";";
    out += t;
  }
  return out;
}

std::vector<std::string> split_tags(const std::string& text)
{
  std::vector<std::string> out;
  std::string cur;
  for (char c : text) {
    if (c == ';' || c == ',') {
      const auto a = cur.find_first_not_of(" \t");
      if (a != std::string::npos) {
        auto b = cur.find_last_not_of(" \t");
        std::string t = cur.substr(a, b - a + 1);
        if (!t.empty())
          out.push_back(t);
      }
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  const auto a = cur.find_first_not_of(" \t");
  if (a != std::string::npos) {
    auto b = cur.find_last_not_of(" \t");
    std::string t = cur.substr(a, b - a + 1);
    if (!t.empty())
      out.push_back(t);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::string sanitize_group(const std::string& name)
{
  std::string out;
  for (char c : name) {
    if (c == '/' || c == '\\' || c == '\0')
      continue;
    out.push_back(c);
  }
  while (!out.empty() && (out.front() == '.' || out.front() == ' '))
    out.erase(out.begin());
  const auto b = out.find_last_not_of(" \t");
  if (b == std::string::npos)
    return {};
  out.resize(b + 1);
  return out;
}

std::string LibraryCatalog::sidecar_path(const std::string& library_dir)
{
  return Glib::build_filename(library_dir, ".readomatic-catalog.ini");
}

LibraryCatalog LibraryCatalog::load(const std::string& library_dir)
{
  LibraryCatalog cat;
  if (library_dir.empty())
    return cat;
  Glib::KeyFile kf;
  try {
    kf.load_from_file(sidecar_path(library_dir));
  } catch (const Glib::Error&) {
    return cat;
  }
  const std::string names = get_str(kf, "catalog", "groups");
  cat.groups = split_tags(names);
  try {
    if (kf.has_group("tags")) {
      for (const auto& key : kf.get_keys("tags")) {
        cat.tags_by_rel[std::string(key)] = split_tags(kf.get_string("tags", key));
      }
    }
  } catch (const Glib::Error&) {
  }
  return cat;
}

void LibraryCatalog::save(const std::string& library_dir) const
{
  if (library_dir.empty())
    return;
  g_mkdir_with_parents(library_dir.c_str(), 0700);
  Glib::KeyFile kf;
  kf.set_string("catalog", "groups", join_tags(groups));
  for (const auto& kv : tags_by_rel) {
    if (kv.second.empty())
      continue;
    kf.set_string("tags", kv.first, join_tags(kv.second));
  }
  try {
    kf.save_to_file(sidecar_path(library_dir));
  } catch (const Glib::Error&) {
  }
}

std::vector<std::string> LibraryCatalog::tags_for(const std::string& rel) const
{
  auto it = tags_by_rel.find(rel);
  if (it == tags_by_rel.end())
    return {};
  return it->second;
}

void LibraryCatalog::set_tags(const std::string& rel, const std::vector<std::string>& tags)
{
  if (rel.empty())
    return;
  if (tags.empty())
    tags_by_rel.erase(rel);
  else
    tags_by_rel[rel] = tags;
}

void LibraryCatalog::forget(const std::string& rel)
{
  if (rel.empty())
    return;
  tags_by_rel.erase(rel);
}

std::vector<CatalogBook> scan_library(const std::string& library_dir, const LibraryCatalog& cat)
{
  std::vector<CatalogBook> out;
  std::error_code ec;
  if (library_dir.empty() || !fs::is_directory(library_dir, ec))
    return out;
  auto add_book = [&](const fs::path& p, const std::string& group) {
    if (!p.has_filename() || !is_ebook_name(p.filename().string()))
      return;
    CatalogBook b;
    b.abs = p.string();
    b.name = p.filename().string();
    b.group = group;
    b.rel = group.empty() ? b.name : (group + "/" + b.name);
    b.tags = cat.tags_for(b.rel);
    std::error_code se;
    b.size = fs::file_size(p, se);
    out.push_back(std::move(b));
  };
  for (const auto& entry : fs::directory_iterator(library_dir, ec)) {
    if (ec)
      break;
    const auto p = entry.path();
    const std::string name = p.filename().string();
    if (name.empty() || name[0] == '.')
      continue;
    if (entry.is_regular_file(ec)) {
      add_book(p, {});
      continue;
    }
    if (!entry.is_directory(ec))
      continue;
    for (const auto& child : fs::directory_iterator(p, ec)) {
      if (ec)
        break;
      if (child.is_regular_file(ec))
        add_book(child.path(), name);
    }
  }
  std::sort(out.begin(), out.end(), [](const CatalogBook& a, const CatalogBook& b) {
    if (a.group != b.group)
      return a.group < b.group;
    return to_lower(a.name) < to_lower(b.name);
  });
  return out;
}

}  // namespace readomatic
