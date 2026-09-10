/* SPDX-License-Identifier: Unlicense */

#include "settings.hpp"

#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/keyfile.h>
#include <glibmm/miscutils.h>

#include <algorithm>
#include <cstdio>

namespace readomatic {
namespace {

constexpr int kMaxRecent = 8;

std::string config_dir()
{
  return Glib::build_filename(Glib::get_user_config_dir(), "readomatic");
}

std::string config_path()
{
  return Glib::build_filename(config_dir(), "readomatic.ini");
}

int get_int(Glib::KeyFile& kf, const char* group, const char* key, int fallback)
{
  try {
    if (kf.has_key(group, key))
      return kf.get_integer(group, key);
  } catch (const Glib::Error&) {
  }
  return fallback;
}

double get_dbl(Glib::KeyFile& kf, const char* group, const char* key, double fallback)
{
  try {
    if (kf.has_key(group, key))
      return kf.get_double(group, key);
  } catch (const Glib::Error&) {
  }
  return fallback;
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

std::string Settings::key_for(const std::string& id)
{
  if (id.empty())
    return "unknown";
  gchar* hex = g_compute_checksum_for_string(G_CHECKSUM_SHA256, id.c_str(),
                                             static_cast<gssize>(id.size()));
  std::string k = hex ? std::string(hex, 16) : std::string("unknown");
  g_free(hex);
  return k;
}

void Settings::load()
{
  Glib::KeyFile kf;
  try {
    kf.load_from_file(config_path());
  } catch (const Glib::Error&) {
    return;
  }
  window_x = get_int(kf, "window", "x", window_x);
  window_y = get_int(kf, "window", "y", window_y);
  window_w = get_int(kf, "window", "width", window_w);
  window_h = get_int(kf, "window", "height", window_h);
  paned = get_int(kf, "window", "paned", paned);
  library_dir = get_str(kf, "library", "dir");
  {
    const std::string fam = get_str(kf, "topic", "font_family");
    if (!fam.empty())
      font_family = fam;
  }
  font_size = get_int(kf, "topic", "font_size", font_size);
  if (font_size < 8)
    font_size = 8;
  if (font_size > 32)
    font_size = 32;
  font_weight = get_int(kf, "topic", "font_weight", font_weight);
  palette = get_int(kf, "topic", "palette", palette);
  if (palette < 0 || palette > 2)
    palette = 1;

  const int nrec = get_int(kf, "recent", "n", 0);
  recent.clear();
  for (int i = 0; i < nrec && i < kMaxRecent; ++i) {
    char pk[16], tk[16];
    std::snprintf(pk, sizeof(pk), "%d_path", i);
    std::snprintf(tk, sizeof(tk), "%d_title", i);
    RecentItem r;
    r.path = get_str(kf, "recent", pk);
    r.title = get_str(kf, "recent", tk);
    if (!r.path.empty())
      recent.push_back(std::move(r));
  }

  books.clear();
  std::vector<Glib::ustring> groups;
  try {
    groups = kf.get_groups();
  } catch (const Glib::Error&) {
    return;
  }
  for (const auto& g : groups) {
    const std::string gs(g);
    if (gs.compare(0, 5, "book-") != 0)
      continue;
    const std::string key = gs.substr(5);
    BookRecord rec;
    rec.path = get_str(kf, g, "path");
    rec.title = get_str(kf, g, "title");
    rec.href = get_str(kf, g, "href");
    rec.fragment = get_str(kf, g, "fragment");
    rec.scroll = get_dbl(kf, gs.c_str(), "scroll", 0);
    const int nm = get_int(kf, gs.c_str(), "marks", 0);
    for (int i = 0; i < nm; ++i) {
      char lk[24], hk[24], fk[24];
      std::snprintf(lk, sizeof(lk), "mark%d_label", i);
      std::snprintf(hk, sizeof(hk), "mark%d_href", i);
      std::snprintf(fk, sizeof(fk), "mark%d_fragment", i);
      Bookmark b;
      b.label = get_str(kf, g, lk);
      b.href = get_str(kf, g, hk);
      b.fragment = get_str(kf, g, fk);
      if (!b.label.empty() && !b.href.empty())
        rec.bookmarks.push_back(std::move(b));
    }
    books[key] = std::move(rec);
  }
}

void Settings::save() const
{
  g_mkdir_with_parents(config_dir().c_str(), 0700);
  Glib::KeyFile kf;
  kf.set_integer("window", "x", window_x);
  kf.set_integer("window", "y", window_y);
  kf.set_integer("window", "width", window_w);
  kf.set_integer("window", "height", window_h);
  kf.set_integer("window", "paned", paned);
  kf.set_string("library", "dir", library_dir);
  kf.set_string("topic", "font_family", font_family);
  kf.set_integer("topic", "font_size", font_size);
  kf.set_integer("topic", "font_weight", font_weight);
  kf.set_integer("topic", "palette", palette);

  kf.set_integer("recent", "n", static_cast<int>(recent.size()));
  for (int i = 0; i < static_cast<int>(recent.size()); ++i) {
    char pk[24], tk[24];
    std::snprintf(pk, sizeof(pk), "%d_path", i);
    std::snprintf(tk, sizeof(tk), "%d_title", i);
    kf.set_string("recent", pk, recent[static_cast<size_t>(i)].path);
    kf.set_string("recent", tk, recent[static_cast<size_t>(i)].title);
  }

  for (const auto& kv : books) {
    const std::string g = "book-" + kv.first;
    const auto& rec = kv.second;
    kf.set_string(g, "path", rec.path);
    kf.set_string(g, "title", rec.title);
    kf.set_string(g, "href", rec.href);
    kf.set_string(g, "fragment", rec.fragment);
    kf.set_double(g, "scroll", rec.scroll);
    kf.set_integer(g, "marks", static_cast<int>(rec.bookmarks.size()));
    for (int i = 0; i < static_cast<int>(rec.bookmarks.size()); ++i) {
      char lk[40], hk[40], fk[40];
      std::snprintf(lk, sizeof(lk), "mark%d_label", i);
      std::snprintf(hk, sizeof(hk), "mark%d_href", i);
      std::snprintf(fk, sizeof(fk), "mark%d_fragment", i);
      kf.set_string(g, lk, rec.bookmarks[static_cast<size_t>(i)].label);
      kf.set_string(g, hk, rec.bookmarks[static_cast<size_t>(i)].href);
      kf.set_string(g, fk, rec.bookmarks[static_cast<size_t>(i)].fragment);
    }
  }
  try {
    kf.save_to_file(config_path());
  } catch (const Glib::Error&) {
  }
}

void Settings::touch_recent(const std::string& path, const std::string& title)
{
  if (path.empty())
    return;
  recent.erase(std::remove_if(recent.begin(), recent.end(),
                              [&](const RecentItem& r) { return r.path == path; }),
               recent.end());
  recent.insert(recent.begin(), {path, title});
  if (recent.size() > static_cast<size_t>(kMaxRecent))
    recent.resize(static_cast<size_t>(kMaxRecent));
}

BookRecord& Settings::book(const std::string& key)
{
  return books[key];
}

}  // namespace readomatic
