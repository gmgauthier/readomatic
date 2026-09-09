/* SPDX-License-Identifier: Unlicense */

#include "book.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <glib.h>
#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace readomatic {
namespace {

namespace fs = std::filesystem;

std::string node_name(xmlNode* n)
{
  if (!n || !n->name)
    return {};
  return reinterpret_cast<const char*>(n->name);
}

std::string node_prop(xmlNode* n, const char* key)
{
  if (!n)
    return {};
  xmlChar* v = xmlGetProp(n, BAD_CAST key);
  if (!v)
    return {};
  std::string out(reinterpret_cast<const char*>(v));
  xmlFree(v);
  return out;
}

std::string node_text(xmlNode* n)
{
  if (!n)
    return {};
  xmlChar* v = xmlNodeGetContent(n);
  if (!v)
    return {};
  std::string out(reinterpret_cast<const char*>(v));
  xmlFree(v);
  return out;
}

xmlNode* find_child(xmlNode* parent, const char* name)
{
  for (xmlNode* n = parent ? parent->children : nullptr; n; n = n->next) {
    if (n->type == XML_ELEMENT_NODE && node_name(n) == name)
      return n;
  }
  return nullptr;
}

xmlNode* find_desc(xmlNode* parent, const char* name)
{
  if (!parent)
    return nullptr;
  if (parent->type == XML_ELEMENT_NODE && node_name(parent) == name)
    return parent;
  for (xmlNode* n = parent->children; n; n = n->next) {
    if (xmlNode* hit = find_desc(n, name))
      return hit;
  }
  return nullptr;
}

std::string dirname_of(const std::string& path)
{
  const auto pos = path.find_last_of('/');
  if (pos == std::string::npos)
    return {};
  return path.substr(0, pos);
}

bool copy_archive_data(archive* src, archive* dst)
{
  const void* buf = nullptr;
  size_t size = 0;
  la_int64_t offset = 0;
  for (;;) {
    const int r = archive_read_data_block(src, &buf, &size, &offset);
    if (r == ARCHIVE_EOF)
      return true;
    if (r != ARCHIVE_OK)
      return false;
    if (archive_write_data_block(dst, buf, size, offset) != ARCHIVE_OK)
      return false;
  }
}

}  // namespace

void Book::set_error(const std::string& msg)
{
  error_ = msg;
}

void Book::close()
{
  if (!extract_dir_.empty()) {
    std::error_code ec;
    fs::remove_all(extract_dir_, ec);
  }
  extract_dir_.clear();
  opf_path_.clear();
  opf_dir_.clear();
  title_.clear();
  manifest_.clear();
  spine_.clear();
  spine_index_ = 0;
  error_.clear();
}

bool Book::extract_zip(const std::string& path)
{
  gchar* hex = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path.c_str(), path.size());
  const std::string hash = hex ? std::string(hex, 16) : std::string("book");
  g_free(hex);
  extract_dir_ = Glib::build_filename(Glib::get_user_cache_dir(), "readomatic", "books",
                                      hash);
  std::error_code ec;
  fs::remove_all(extract_dir_, ec);
  fs::create_directories(extract_dir_, ec);
  if (ec) {
    set_error("Could not create cache directory.");
    return false;
  }

  archive* a = archive_read_new();
  archive_read_support_format_zip(a);
  archive_read_support_filter_all(a);
  if (archive_read_open_filename(a, path.c_str(), 10240) != ARCHIVE_OK) {
    set_error("Not a zip/EPUB file.");
    archive_read_free(a);
    return false;
  }

  archive* ext = archive_write_disk_new();
  archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_SECURE_NODOTDOT |
                                           ARCHIVE_EXTRACT_SECURE_SYMLINKS);

  bool ok = true;
  archive_entry* entry = nullptr;
  while (ok && archive_read_next_header(a, &entry) == ARCHIVE_OK) {
    const char* name = archive_entry_pathname(entry);
    if (!name)
      continue;
    const std::string dest = Glib::build_filename(extract_dir_, name);
    archive_entry_set_pathname(entry, dest.c_str());
    if (archive_write_header(ext, entry) != ARCHIVE_OK) {
      ok = false;
      break;
    }
    if (archive_entry_size(entry) > 0 && !copy_archive_data(a, ext)) {
      ok = false;
      break;
    }
    archive_write_finish_entry(ext);
  }
  archive_write_free(ext);
  archive_read_free(a);
  if (!ok)
    set_error("Failed to extract EPUB.");
  return ok;
}

bool Book::parse_container()
{
  const std::string container =
      Glib::build_filename(extract_dir_, "META-INF", "container.xml");
  if (!Glib::file_test(container, Glib::FILE_TEST_IS_REGULAR)) {
    set_error("No META-INF/container.xml.");
    return false;
  }
  xmlDoc* doc = xmlReadFile(container.c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
  if (!doc) {
    set_error("Could not parse container.xml.");
    return false;
  }
  xmlNode* root = xmlDocGetRootElement(doc);
  xmlNode* rf = find_desc(root, "rootfile");
  const std::string full = node_prop(rf, "full-path");
  xmlFreeDoc(doc);
  if (full.empty()) {
    set_error("container.xml has no rootfile.");
    return false;
  }
  opf_path_ = Glib::build_filename(extract_dir_, full);
  opf_dir_ = dirname_of(opf_path_);
  return true;
}

bool Book::parse_opf()
{
  xmlDoc* doc = xmlReadFile(opf_path_.c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
  if (!doc) {
    set_error("Could not parse OPF.");
    return false;
  }
  xmlNode* package = xmlDocGetRootElement(doc);
  xmlNode* metadata = find_child(package, "metadata");
  if (xmlNode* title = find_desc(metadata, "title"))
    title_ = node_text(title);
  if (title_.empty())
    title_ = "Untitled";

  xmlNode* manifest = find_child(package, "manifest");
  for (xmlNode* n = manifest ? manifest->children : nullptr; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE || node_name(n) != "item")
      continue;
    Item it;
    it.id = node_prop(n, "id");
    it.href = node_prop(n, "href");
    it.media_type = node_prop(n, "media-type");
    it.properties = node_prop(n, "properties");
    if (!it.id.empty())
      manifest_[it.id] = it;
  }

  xmlNode* spine = find_child(package, "spine");
  for (xmlNode* n = spine ? spine->children : nullptr; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE || node_name(n) != "itemref")
      continue;
    const std::string idref = node_prop(n, "idref");
    auto it = manifest_.find(idref);
    if (it == manifest_.end())
      continue;
    spine_.push_back(it->second.href);
  }
  xmlFreeDoc(doc);
  if (spine_.empty()) {
    set_error("OPF spine is empty.");
    return false;
  }
  return true;
}

bool Book::open(const std::string& path)
{
  close();
  if (!extract_zip(path)) {
    close();
    return false;
  }
  if (!parse_container() || !parse_opf()) {
    close();
    return false;
  }
  const std::string start = start_href();
  for (int i = 0; i < spine_count(); ++i) {
    if (spine_[static_cast<size_t>(i)] == start) {
      spine_index_ = i;
      break;
    }
  }
  return true;
}

std::string Book::spine_href(int i) const
{
  if (i < 0 || i >= spine_count())
    return {};
  return spine_[static_cast<size_t>(i)];
}

bool Book::set_spine_index(int i)
{
  if (i < 0 || i >= spine_count())
    return false;
  spine_index_ = i;
  return true;
}

std::string Book::current_href() const
{
  return spine_href(spine_index_);
}

std::string Book::resolve(const std::string& href) const
{
  std::string h = href;
  const auto hash = h.find('#');
  if (hash != std::string::npos)
    h = h.substr(0, hash);
  if (h.empty())
    return {};
  fs::path full = fs::path(h).is_absolute() ? fs::path(extract_dir_) / h.substr(h[0] == '/' ? 1 : 0)
                                            : fs::path(opf_dir_) / h;
  full = full.lexically_normal();
  return full.string();
}

std::string Book::load_document(const std::string& href) const
{
  const std::string path = resolve(href);
  if (path.empty() || !Glib::file_test(path, Glib::FILE_TEST_IS_REGULAR))
    return {};
  std::ifstream in(path);
  if (!in)
    return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool Book::skip_spine_href(const std::string& href) const
{
  auto basename = [](std::string h) {
    const auto pos = h.find_last_of('/');
    if (pos != std::string::npos)
      h = h.substr(pos + 1);
    for (char& c : h)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return h;
  };
  for (const auto& kv : manifest_) {
    if (kv.second.href != href)
      continue;
    const auto& it = kv.second;
    if (it.properties.find("svg") != std::string::npos ||
        it.properties.find("cover-image") != std::string::npos)
      return true;
    const std::string mt = it.media_type;
    if (mt.find("xhtml") == std::string::npos && mt.find("html") == std::string::npos)
      return true;
    const std::string b = basename(it.href);
    if (b.find("cover") != std::string::npos || b.find("titlepage") != std::string::npos ||
        b.find("wrap") != std::string::npos)
      return true;
    return false;
  }
  return false;
}

std::string Book::start_href() const
{
  for (const auto& href : spine_) {
    if (!skip_spine_href(href))
      return href;
  }
  return spine_.empty() ? std::string() : spine_.front();
}

bool Book::advance_spine(int delta)
{
  if (spine_.empty() || delta == 0)
    return false;
  const int step = delta > 0 ? 1 : -1;
  int n = std::abs(delta);
  int i = spine_index_;
  while (n > 0) {
    i += step;
    if (i < 0 || i >= spine_count())
      return false;
    if (skip_spine_href(spine_[static_cast<size_t>(i)]))
      continue;
    --n;
  }
  spine_index_ = i;
  return true;
}

std::string Book::href_for_id(const std::string& id) const
{
  if (id.empty())
    return {};
  const std::string a = "id=\"" + id + "\"";
  const std::string b = "id='" + id + "'";
  for (const auto& href : spine_) {
    if (skip_spine_href(href))
      continue;
    const std::string doc = load_document(href);
    if (doc.find(a) != std::string::npos || doc.find(b) != std::string::npos)
      return href;
  }
  return {};
}

}  // namespace readomatic
