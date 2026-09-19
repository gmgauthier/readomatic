/* SPDX-License-Identifier: Unlicense */

#include "lastread.hpp"

#include <glibmm.h>

#include <cstdlib>
#include <sstream>
#include <vector>

namespace readomatic {
namespace {

std::string trim(std::string s)
{
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
    s.pop_back();
  return s;
}

std::string sql_escape(const std::string& s)
{
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "''";
    else
      out += c;
  }
  out += "'";
  return out;
}

}  // namespace

std::string lastread_path(const std::string& book_path)
{
  return book_path + ".lastread";
}

std::string canonical_lastread(const LastRead& pos)
{
  if (!pos.present)
    return {};
  if (!pos.pbr.empty())
    return pos.pbr;
  std::ostringstream os;
  os << pos.href << '|' << pos.fragment << '|' << pos.scroll;
  if (pos.cpage > 0)
    os << "|p" << pos.cpage << '/' << pos.npage;
  return os.str();
}

LastRead load_lastread(const std::string& book_path)
{
  LastRead pos;
  const std::string path = lastread_path(book_path);
  if (!Glib::file_test(path, Glib::FILE_TEST_IS_REGULAR))
    return pos;
  Glib::KeyFile kf;
  try {
    kf.load_from_file(path);
  } catch (const Glib::Error&) {
    return pos;
  }
  try {
    if (kf.has_key("position", "href"))
      pos.href = kf.get_string("position", "href");
    if (kf.has_key("position", "fragment"))
      pos.fragment = kf.get_string("position", "fragment");
    if (kf.has_key("position", "scroll"))
      pos.scroll = kf.get_double("position", "scroll");
    if (kf.has_key("position", "pbr"))
      pos.pbr = kf.get_string("position", "pbr");
    if (kf.has_key("position", "cpage"))
      pos.cpage = kf.get_integer("position", "cpage");
    if (kf.has_key("position", "npage"))
      pos.npage = kf.get_integer("position", "npage");
  } catch (const Glib::Error&) {
  }
  pos.present = !canonical_lastread(pos).empty() || !pos.href.empty() || pos.cpage > 0;
  return pos;
}

void save_lastread(const std::string& book_path, const LastRead& pos)
{
  if (book_path.empty())
    return;
  Glib::KeyFile kf;
  kf.set_string("position", "href", pos.href);
  kf.set_string("position", "fragment", pos.fragment);
  kf.set_double("position", "scroll", pos.scroll);
  kf.set_string("position", "pbr", pos.pbr);
  kf.set_integer("position", "cpage", pos.cpage);
  kf.set_integer("position", "npage", pos.npage);
  try {
    kf.save_to_file(lastread_path(book_path));
  } catch (const Glib::Error&) {
  }
}

LastRead lastread_from_settings(const Settings& settings, const std::string& book_path)
{
  LastRead pos;
  for (const auto& kv : settings.books) {
    if (kv.second.path == book_path) {
      pos.present = true;
      pos.href = kv.second.href;
      pos.fragment = kv.second.fragment;
      pos.scroll = kv.second.scroll;
      return pos;
    }
  }
  return pos;
}

LastRead lastread_from_pocketbook(const std::string& device_root, const std::string& book_path)
{
  LastRead pos;
  if (device_root.empty() || book_path.empty())
    return pos;
  const std::string db = Glib::build_filename(device_root, "system", "explorer-3", "explorer-3.db");
  if (!Glib::file_test(db, Glib::FILE_TEST_IS_REGULAR))
    return pos;
  const std::string name = Glib::path_get_basename(book_path);
  const std::string uri = "file:" + db + "?mode=ro";
  const std::string sql =
      "SELECT IFNULL(s.position,''), IFNULL(s.cpage,0), IFNULL(s.npage,0) "
      "FROM files f LEFT JOIN books_settings s ON s.bookid=f.book_id WHERE f.filename=" +
      sql_escape(name) + " LIMIT 1;";
  std::string out;
  std::string err;
  int st = 0;
  try {
    const std::vector<std::string> argv = {"sqlite3", "-readonly", "-separator", "|", uri, sql};
    Glib::spawn_sync("", argv, Glib::SPAWN_SEARCH_PATH, Glib::SlotSpawnChildSetup(), &out, &err,
                     &st);
  } catch (const Glib::Error&) {
    return pos;
  }
  out = trim(out);
  if (out.empty())
    return pos;
  LastRead r;
  const auto a = out.find('|');
  const auto b = out.find('|', a == std::string::npos ? 0 : a + 1);
  if (a == std::string::npos) {
    r.pbr = out;
  } else {
    r.pbr = out.substr(0, a);
    if (b == std::string::npos)
      r.cpage = std::atoi(out.c_str() + a + 1);
    else {
      r.cpage = std::atoi(out.substr(a + 1, b - a - 1).c_str());
      r.npage = std::atoi(out.c_str() + b + 1);
    }
  }
  r.present = !r.pbr.empty() || r.cpage > 0;
  return r;
}

LastRead resolve_lastread(const std::string& book_path, const Settings* settings,
                          const std::string& device_root)
{
  LastRead pos = load_lastread(book_path);
  if (pos.present)
    return pos;
  if (settings) {
    pos = lastread_from_settings(*settings, book_path);
    if (pos.present)
      return pos;
  }
  if (!device_root.empty())
    return lastread_from_pocketbook(device_root, book_path);
  return {};
}

}  // namespace readomatic
