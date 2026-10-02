/* SPDX-License-Identifier: Unlicense */

#include "book.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <mobi.h>

#include <glib.h>
#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

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

std::string ascii_lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool ends_with_ci(const std::string& path, const char* ext)
{
  const std::string p = ascii_lower(path);
  const std::string e = ascii_lower(ext);
  return p.size() >= e.size() && p.compare(p.size() - e.size(), e.size(), e) == 0;
}

bool looks_like_mobi_path(const std::string& path)
{
  return ends_with_ci(path, ".mobi") || ends_with_ci(path, ".azw") || ends_with_ci(path, ".azw3") ||
         ends_with_ci(path, ".prc");
}

bool pdb_is_mobi(const std::string& path)
{
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f)
    return false;
  char buf[68] = {};
  const size_t n = std::fread(buf, 1, sizeof(buf), f);
  std::fclose(f);
  if (n < 68)
    return false;
  return std::memcmp(buf + 60, "BOOKMOBI", 8) == 0 || std::memcmp(buf + 60, "TEXtREAd", 8) == 0;
}

std::string xml_escape_text(const std::string& in)
{
  std::string out;
  out.reserve(in.size() + 8);
  for (const char c : in) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

bool write_bytes(const std::string& path, const unsigned char* data, size_t size)
{
  try {
    Glib::file_set_contents(path, std::string(reinterpret_cast<const char*>(data), size));
  } catch (const Glib::Error&) {
    return false;
  }
  return true;
}

const char kEpubContainer[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
    "  <rootfiles>\n"
    "    <rootfile full-path=\"OEBPS/content.opf\" "
    "media-type=\"application/oebps-package+xml\"/>\n"
    "  </rootfiles>\n"
    "</container>";

std::string html_name(xmlNode* n)
{
  return ascii_lower(node_name(n));
}

std::string squeeze_ws(std::string s)
{
  for (size_t i = 0; i + 1 < s.size();) {
    if (static_cast<unsigned char>(s[i]) == 0xC2 && static_cast<unsigned char>(s[i + 1]) == 0xA0) {
      s[i] = ' ';
      s.erase(i + 1, 1);
    } else {
      ++i;
    }
  }
  std::string out;
  out.reserve(s.size());
  bool space = true;
  for (unsigned char c : s) {
    if (std::isspace(c)) {
      if (!space) {
        out.push_back(' ');
        space = true;
      }
    } else {
      out.push_back(static_cast<char>(c));
      space = false;
    }
  }
  if (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

bool skip_html_tag(const std::string& name)
{
  return name == "script" || name == "style" || name == "svg" || name == "iframe" ||
         name == "head" || name == "meta" || name == "link" || name == "title";
}

bool is_block_tag(const std::string& name)
{
  return name == "p" || name == "div" || name == "br" || name == "li" || name == "tr" ||
         name == "h1" || name == "h2" || name == "h3" || name == "h4" || name == "h5" ||
         name == "h6" || name == "blockquote" || name == "pre" || name == "section" ||
         name == "article" || name == "header" || name == "footer";
}

void collect_plain(xmlNode* n, std::string& out)
{
  for (; n; n = n->next) {
    if (n->type == XML_TEXT_NODE || n->type == XML_CDATA_SECTION_NODE) {
      if (n->content)
        out += reinterpret_cast<const char*>(n->content);
      continue;
    }
    if (n->type != XML_ELEMENT_NODE)
      continue;
    const std::string name = html_name(n);
    if (skip_html_tag(name))
      continue;
    if (is_block_tag(name))
      out += ' ';
    collect_plain(n->children, out);
    if (is_block_tag(name))
      out += ' ';
  }
}

bool search_break_tag(const std::string& name)
{
  return name == "p" || name == "div" || name == "blockquote" || name == "li" || name == "tr" ||
         name == "h1" || name == "h2" || name == "h3" || name == "h4" || name == "h5" ||
         name == "h6" || name == "pre";
}

void append_search_break(std::string& out)
{
  if (!out.empty() && out.back() != '\n')
    out.push_back('\n');
}

/* Paragraphs stay on their own lines. The topic pane inserts those same breaks,
   and a find hit is counted only where that pane can highlight it. */
void collect_search_text(xmlNode* n, std::string& out)
{
  for (; n; n = n->next) {
    if (n->type == XML_TEXT_NODE || n->type == XML_CDATA_SECTION_NODE) {
      if (n->content)
        out += reinterpret_cast<const char*>(n->content);
      continue;
    }
    if (n->type != XML_ELEMENT_NODE)
      continue;
    const std::string name = html_name(n);
    if (skip_html_tag(name))
      continue;
    if (name == "br") {
      append_search_break(out);
      continue;
    }
    if (search_break_tag(name))
      append_search_break(out);
    collect_search_text(n->children, out);
    if (search_break_tag(name) && name != "tr")
      append_search_break(out);
  }
}

std::string normalize_search_text(const std::string& raw)
{
  std::string out;
  std::string line;
  auto flush = [&]() {
    if (!out.empty())
      out.push_back('\n');
    out += squeeze_ws(line);
    line.clear();
  };
  for (char c : raw) {
    if (c == '\n')
      flush();
    else
      line.push_back(c);
  }
  flush();
  return out;
}

struct Heading {
  int level = 1;
  std::string id;
  std::string text;
};

void collect_headings(xmlNode* n, std::vector<Heading>& out)
{
  for (; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE)
      continue;
    const std::string name = html_name(n);
    if (skip_html_tag(name))
      continue;
    if (name == "h1" || name == "h2" || name == "h3") {
      Heading h;
      h.level = name[1] - '0';
      h.id = node_prop(n, "id");
      h.text = squeeze_ws(node_text(n));
      if (!h.text.empty())
        out.push_back(std::move(h));
      continue;
    }
    collect_headings(n->children, out);
  }
}

xmlDoc* parse_xhtml_memory(const std::string& xhtml)
{
  if (xhtml.empty())
    return nullptr;
  return htmlReadMemory(xhtml.data(), static_cast<int>(xhtml.size()), "doc.xhtml", "UTF-8",
                        HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING |
                            HTML_PARSE_NONET | HTML_PARSE_NOBLANKS);
}

std::string fallback_title(const std::string& href)
{
  std::string h = href;
  const auto hash = h.find('#');
  if (hash != std::string::npos)
    h = h.substr(0, hash);
  const auto slash = h.find_last_of('/');
  if (slash != std::string::npos)
    h = h.substr(slash + 1);
  const auto dot = h.find('.');
  if (dot != std::string::npos)
    h = h.substr(0, dot);
  return h.empty() ? std::string("Topic") : h;
}

bool utf8_cont(unsigned char c)
{
  return (c & 0xC0) == 0x80;
}

/* Byte index of the character that contains i, or s.size() if i is at/after end. */
size_t utf8_floor(const std::string& s, size_t i)
{
  if (i >= s.size())
    return s.size();
  while (i > 0 && utf8_cont(static_cast<unsigned char>(s[i])))
    --i;
  return i;
}

/* First byte of the next character at or after i (exclusive-end friendly). */
size_t utf8_ceil(const std::string& s, size_t i)
{
  if (i >= s.size())
    return s.size();
  while (i < s.size() && utf8_cont(static_cast<unsigned char>(s[i])))
    ++i;
  return i;
}

std::string excerpt_at(const std::string& text, size_t pos, size_t qlen)
{
  const size_t before = 36;
  const size_t after = 40;
  const size_t start = utf8_floor(text, pos > before ? pos - before : 0);
  size_t end = utf8_ceil(text, pos + qlen + after);
  if (end > text.size())
    end = text.size();
  std::string out;
  if (start > 0)
    out += "…";
  out += text.substr(start, end - start);
  if (end < text.size())
    out += "…";
  out = squeeze_ws(out);
  if (!g_utf8_validate(out.c_str(), static_cast<gssize>(out.size()), nullptr)) {
    gchar* v = g_utf8_make_valid(out.c_str(), static_cast<gssize>(out.size()));
    out = v ? v : std::string();
    g_free(v);
  }
  return out;
}

std::string dirname_of(const std::string& path)
{
  const auto pos = path.find_last_of('/');
  if (pos == std::string::npos)
    return {};
  return path.substr(0, pos);
}

bool inside_directory(const fs::path& root, const fs::path& candidate)
{
  auto base = root.begin();
  auto full = candidate.begin();
  for (; base != root.end(); ++base, ++full) {
    if (full == candidate.end() || *base != *full)
      return false;
  }
  return full != candidate.end();
}

int hex_value(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* One pass. '+' stays '+'. '%2520' stays '%20'. */
std::string percent_decode(const std::string& in)
{
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size()) {
      const int hi = hex_value(in[i + 1]);
      const int lo = hex_value(in[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(in[i]);
  }
  return out;
}

bool regular_file(const std::string& path)
{
  if (path.empty())
    return false;
  std::error_code ec;
  const fs::file_status st = fs::symlink_status(fs::path(path), ec);
  return !ec && fs::is_regular_file(st);
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

bool Book::fail_open()
{
  const std::string why = error_;
  close();
  error_ = why;
  return false;
}

void Book::close()
{
  if (!extract_dir_.empty()) {
    std::error_code ec;
    fs::remove_all(extract_dir_, ec);
  }
  extract_dir_.clear();
  source_path_.clear();
  identifier_.clear();
  opf_path_.clear();
  opf_dir_.clear();
  title_.clear();
  manifest_.clear();
  spine_.clear();
  nav_.clear();
  nav_href_.clear();
  ncx_href_.clear();
  spine_index_ = 0;
  error_.clear();
}

bool Book::extract_zip(const std::string& path)
{
  gchar* hex = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path.c_str(), path.size());
  const std::string hash = hex ? std::string(hex, 16) : std::string("book");
  g_free(hex);
  extract_dir_ = Glib::build_filename(Glib::get_user_cache_dir(), "readomatic", "books", hash);
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

bool Book::extract_mobi(const std::string& path)
{
  gchar* hex = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path.c_str(), path.size());
  const std::string hash = hex ? std::string(hex, 16) : std::string("book");
  g_free(hex);
  extract_dir_ = Glib::build_filename(Glib::get_user_cache_dir(), "readomatic", "books", hash);
  std::error_code ec;
  fs::remove_all(extract_dir_, ec);
  const std::string meta = Glib::build_filename(extract_dir_, "META-INF");
  const std::string oebps = Glib::build_filename(extract_dir_, "OEBPS");
  fs::create_directories(meta, ec);
  fs::create_directories(oebps, ec);
  if (ec) {
    set_error("Could not create cache directory.");
    return false;
  }

  MOBIData* m = mobi_init();
  if (!m) {
    set_error("Could not initialize libmobi.");
    return false;
  }
  MOBI_RET ret = mobi_load_filename(m, path.c_str());
  if (ret != MOBI_SUCCESS) {
    mobi_free(m);
    set_error("Not a MOBI/AZW file.");
    return false;
  }
  if (mobi_is_encrypted(m)) {
    mobi_free(m);
    set_error("This MOBI is encrypted.");
    return false;
  }
  if (mobi_is_replica(m)) {
    mobi_free(m);
    set_error("Print Replica files are not readable.");
    return false;
  }

  MOBIRawml* rawml = mobi_init_rawml(m);
  if (!rawml) {
    mobi_free(m);
    set_error("Could not parse MOBI.");
    return false;
  }
  ret = mobi_parse_rawml(rawml, m);
  if (ret != MOBI_SUCCESS) {
    mobi_free_rawml(rawml);
    mobi_free(m);
    set_error("Could not reconstruct MOBI markup.");
    return false;
  }

  const std::string container = Glib::build_filename(meta, "container.xml");
  if (!write_bytes(container, reinterpret_cast<const unsigned char*>(kEpubContainer),
                   sizeof(kEpubContainer) - 1)) {
    mobi_free_rawml(rawml);
    mobi_free(m);
    set_error("Could not write EPUB container.");
    return false;
  }

  bool wrote_opf = false;
  std::vector<std::string> parts;
  auto dump_list = [&](MOBIPart* curr, const char* prefix, bool skip_first) -> bool {
    if (skip_first && curr)
      curr = curr->next;
    for (; curr; curr = curr->next) {
      if (!curr->data || curr->size == 0)
        continue;
      const MOBIFileMeta meta_t = mobi_get_filemeta_by_type(curr->type);
      char name[64];
      if (meta_t.type == T_OPF) {
        std::snprintf(name, sizeof(name), "content.opf");
        wrote_opf = true;
      } else {
        std::snprintf(name, sizeof(name), "%s%05zu.%s", prefix, curr->uid, meta_t.extension);
      }
      const std::string dest = Glib::build_filename(oebps, name);
      if (!write_bytes(dest, curr->data, curr->size))
        return false;
      if (meta_t.type == T_HTML)
        parts.emplace_back(name);
    }
    return true;
  };

  if (!dump_list(rawml->markup, "part", false) || !dump_list(rawml->flow, "flow", true) ||
      !dump_list(rawml->resources, "resource", false)) {
    mobi_free_rawml(rawml);
    mobi_free(m);
    set_error("Could not write reconstructed MOBI files.");
    return false;
  }

  if (!wrote_opf) {
    char fullname[256] = {};
    if (mobi_get_fullname(m, fullname, sizeof(fullname) - 1) != MOBI_SUCCESS || fullname[0] == 0)
      std::snprintf(fullname, sizeof(fullname), "Untitled");
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<package version=\"2.0\" unique-identifier=\"uid\" "
          "xmlns=\"http://www.idpf.org/2007/opf\">\n"
       << "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
       << "    <dc:title>" << xml_escape_text(fullname) << "</dc:title>\n"
       << "    <dc:identifier id=\"uid\">" << xml_escape_text(path) << "</dc:identifier>\n"
       << "  </metadata>\n"
       << "  <manifest>\n";
    for (size_t i = 0; i < parts.size(); ++i) {
      os << "    <item id=\"part" << i << "\" href=\"" << parts[i]
         << "\" media-type=\"application/xhtml+xml\"/>\n";
    }
    os << "  </manifest>\n  <spine>\n";
    for (size_t i = 0; i < parts.size(); ++i)
      os << "    <itemref idref=\"part" << i << "\"/>\n";
    os << "  </spine>\n</package>\n";
    const std::string opf = os.str();
    if (!write_bytes(Glib::build_filename(oebps, "content.opf"),
                     reinterpret_cast<const unsigned char*>(opf.data()), opf.size())) {
      mobi_free_rawml(rawml);
      mobi_free(m);
      set_error("Could not write OPF.");
      return false;
    }
  }

  if (parts.empty() && !wrote_opf) {
    mobi_free_rawml(rawml);
    mobi_free(m);
    set_error("No readable text in this MOBI.");
    return false;
  }

  mobi_free_rawml(rawml);
  mobi_free(m);
  return true;
}

bool Book::parse_container()
{
  const std::string container = Glib::build_filename(extract_dir_, "META-INF", "container.xml");
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
  if (xmlNode* ident = find_desc(metadata, "identifier"))
    identifier_ = node_text(ident);

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
    if (it.properties.find("nav") != std::string::npos)
      nav_href_ = it.href;
    if (it.media_type.find("ncx") != std::string::npos)
      ncx_href_ = it.href;
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

namespace {

void walk_nav_ol(xmlNode* ol, std::vector<Book::NavNode>& out)
{
  for (xmlNode* li = ol ? ol->children : nullptr; li; li = li->next) {
    if (li->type != XML_ELEMENT_NODE || node_name(li) != "li")
      continue;
    Book::NavNode node;
    xmlNode* nested = nullptr;
    for (xmlNode* c = li->children; c; c = c->next) {
      if (c->type != XML_ELEMENT_NODE)
        continue;
      const std::string nm = node_name(c);
      if (nm == "a" || nm == "span") {
        if (node.label.empty())
          node.label = node_text(c);
        if (nm == "a" && node.href.empty())
          node.href = node_prop(c, "href");
      } else if (nm == "ol") {
        nested = c;
      }
    }
    if (nested)
      walk_nav_ol(nested, node.children);
    if (!node.label.empty() || !node.href.empty())
      out.push_back(std::move(node));
  }
}

void walk_nav_point(xmlNode* parent, std::vector<Book::NavNode>& out)
{
  for (xmlNode* n = parent ? parent->children : nullptr; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE || node_name(n) != "navPoint")
      continue;
    Book::NavNode node;
    if (xmlNode* label = find_desc(n, "navLabel"))
      node.label = node_text(label);
    if (xmlNode* content = find_child(n, "content"))
      node.href = node_prop(content, "src");
    walk_nav_point(n, node.children);
    out.push_back(std::move(node));
  }
}

}  // namespace

bool Book::parse_nav()
{
  nav_.clear();
  const std::string href = !nav_href_.empty() ? nav_href_ : ncx_href_;
  if (href.empty())
    return true;
  const std::string path = resolve(href);
  if (path.empty() || !Glib::file_test(path, Glib::FILE_TEST_IS_REGULAR))
    return true;
  xmlDoc* doc =
      xmlReadFile(path.c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_RECOVER);
  if (!doc)
    return true;
  xmlNode* root = xmlDocGetRootElement(doc);
  if (xmlNode* nav = find_desc(root, "nav")) {
    if (xmlNode* ol = find_desc(nav, "ol"))
      walk_nav_ol(ol, nav_);
  } else if (xmlNode* map = find_desc(root, "navMap")) {
    walk_nav_point(map, nav_);
  }
  xmlFreeDoc(doc);
  return true;
}

bool Book::open(const std::string& path)
{
  close();
  gchar* canon = g_canonicalize_filename(path.c_str(), nullptr);
  source_path_ = canon ? canon : path;
  g_free(canon);
  const bool mobi = looks_like_mobi_path(path) || pdb_is_mobi(path);
  if (mobi) {
    if (!extract_mobi(path))
      return fail_open();
  } else if (!extract_zip(path)) {
    return fail_open();
  }
  if (!parse_container() || !parse_opf())
    return fail_open();
  parse_nav();
  if (identifier_.empty())
    identifier_ = source_path_;
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

bool Book::select_href(const std::string& href)
{
  std::string file = href;
  const auto hash = file.find('#');
  if (hash != std::string::npos)
    file = file.substr(0, hash);
  if (file.empty())
    return false;
  if (skip_spine_href(file))
    return false;
  for (int i = 0; i < spine_count(); ++i) {
    if (spine_href(i) == file)
      return set_spine_index(i);
  }
  const std::string want = resolve(file);
  if (want.empty())
    return false;
  for (int i = 0; i < spine_count(); ++i) {
    if (resolve(spine_href(i)) == want)
      return set_spine_index(i);
  }
  return false;
}

std::string Book::current_href() const
{
  return spine_href(spine_index_);
}

std::string Book::resolve_against(const std::string& href, const std::string& base_dir) const
{
  std::string h = href;
  const auto hash = h.find('#');
  if (hash != std::string::npos)
    h = h.substr(0, hash);
  h = percent_decode(h);
  if (h.empty() || h.find('\0') != std::string::npos || extract_dir_.empty())
    return {};
  const std::string base = base_dir.empty() ? opf_dir_ : base_dir;
  const fs::path joined = fs::path(h).is_absolute()
                              ? fs::path(extract_dir_) / h.substr(h[0] == '/' ? 1 : 0)
                              : fs::path(base) / h;
  std::error_code ec;
  const fs::path root = fs::weakly_canonical(fs::path(extract_dir_), ec);
  if (ec)
    return {};
  ec.clear();
  const fs::path full = fs::weakly_canonical(joined.lexically_normal(), ec);
  if (ec || !inside_directory(root, full))
    return {};
  return full.string();
}

std::string Book::resolve(const std::string& href) const
{
  return resolve_against(href, opf_dir_);
}

std::string Book::resolve_content_link(const std::string& href) const
{
  std::string base = opf_dir_;
  const std::string current = resolve(current_href());
  if (!current.empty()) {
    const auto slash = current.find_last_of('/');
    if (slash != std::string::npos)
      base = current.substr(0, slash);
  }
  const std::string beside = resolve_against(href, base);
  if (regular_file(beside))
    return beside;
  const std::string packaged = resolve(href);
  if (regular_file(packaged))
    return packaged;
  return {};
}

std::string Book::load_document(const std::string& href) const
{
  const std::string path = resolve(href);
  if (path.empty())
    return {};
  std::error_code ec;
  const fs::file_status st = fs::symlink_status(path, ec);
  if (ec || !fs::is_regular_file(st))
    return {};
  std::ifstream in(path);
  if (!in)
    return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

namespace {

bool cover_basename(std::string base)
{
  const auto dot = base.find_last_of('.');
  if (dot != std::string::npos)
    base = base.substr(0, dot);
  if (base == "cover" || base == "titlepage" || base == "wrap")
    return true;
  if (base.size() > 4 && base.compare(0, 4, "wrap") == 0) {
    for (size_t i = 4; i < base.size(); ++i) {
      if (base[i] < '0' || base[i] > '9')
        return false;
    }
    return true;
  }
  return false;
}

}  // namespace

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
    if (cover_basename(basename(it.href)))
      return true;
    const std::string html = load_document(href);
    const std::string low = ascii_lower(html);
    if (low.find("<title>\"cover\"") != std::string::npos)
      return true;
    xmlDoc* doc = parse_xhtml_memory(html);
    std::string raw;
    if (doc) {
      collect_plain(xmlDocGetRootElement(doc), raw);
      xmlFreeDoc(doc);
    }
    if (squeeze_ws(raw).empty())
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

void Book::readable_span(int& number, int& total) const
{
  number = 0;
  total = 0;
  for (int i = 0; i < spine_count(); ++i) {
    if (skip_spine_href(spine_[static_cast<size_t>(i)]))
      continue;
    ++total;
    if (i == spine_index_)
      number = total;
  }
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

std::vector<Book::IndexEntry> Book::build_index() const
{
  std::vector<IndexEntry> out;
  if (!is_open())
    return out;
  for (const auto& href : spine_) {
    if (skip_spine_href(href))
      continue;
    const std::string xhtml = load_document(href);
    xmlDoc* doc = parse_xhtml_memory(xhtml);
    std::vector<Heading> headings;
    if (doc) {
      collect_headings(xmlDocGetRootElement(doc), headings);
      xmlFreeDoc(doc);
    }
    if (headings.empty()) {
      out.push_back({fallback_title(href), href});
      continue;
    }
    for (const auto& h : headings) {
      IndexEntry e;
      e.label = h.text;
      e.href = h.id.empty() ? href : href + "#" + h.id;
      out.push_back(std::move(e));
    }
  }
  return out;
}

std::vector<Book::SearchHit> Book::search(const std::string& query, int limit) const
{
  std::vector<SearchHit> out;
  if (!is_open() || query.size() < 2 || limit <= 0)
    return out;
  const std::string needle = ascii_lower(query);
  for (const auto& href : spine_) {
    if (out.size() >= static_cast<size_t>(limit))
      break;
    if (skip_spine_href(href))
      continue;
    const std::string xhtml = load_document(href);
    xmlDoc* doc = parse_xhtml_memory(xhtml);
    std::string raw;
    if (doc) {
      collect_search_text(xmlDocGetRootElement(doc), raw);
      xmlFreeDoc(doc);
    }
    const std::string text = normalize_search_text(raw);
    const std::string hay = ascii_lower(text);
    size_t pos = 0;
    int occ = 0;
    while (out.size() < static_cast<size_t>(limit)) {
      pos = hay.find(needle, pos);
      if (pos == std::string::npos)
        break;
      SearchHit hit;
      hit.href = href;
      hit.excerpt = excerpt_at(text, pos, needle.size());
      hit.occurrence = occ;
      out.push_back(std::move(hit));
      ++occ;
      pos += needle.size();
    }
  }
  return out;
}

}  // namespace readomatic
