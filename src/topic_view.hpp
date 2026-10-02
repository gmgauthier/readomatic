/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "settings.hpp"

#include <gtkmm.h>
#include <libxml/tree.h>

#include <map>
#include <string>
#include <vector>

namespace readomatic {

class TopicView : public Gtk::TextView {
 public:
  TopicView();

  void load_xhtml(const std::string& xhtml, const std::string& base_dir,
                  const std::string& book_root);
  void clear_topic();
  bool scroll_to_id(const std::string& id);
  bool select_match(const Glib::ustring& query, int occurrence);
  void apply_appearance(const std::string& family, int size_pt, int weight, int palette);
  bool copy_selection() const;
  bool has_selection() const;
  bool selection_range(int& start, int& end) const;
  Glib::ustring selection_text() const;
  void apply_highlights(const std::vector<Highlight>& marks, const std::string& href, int palette);
  bool find_excerpt(const std::string& excerpt, int& start, int& end) const;
  int char_count() const;

  sigc::signal<void, Glib::ustring>& signal_jump()
  {
    return signal_jump_;
  }

 protected:
  bool on_button_release_event(GdkEventButton* event) override;

 private:
  void ensure_tags();
  void walk(xmlNode* node, const std::string& base_dir, const std::string& book_root,
            int list_depth);
  void insert_text(const std::string& text, const std::vector<Glib::ustring>& tag_names);
  void insert_break();
  std::string resolve_src(const std::string& src, const std::string& base_dir,
                          const std::string& book_root) const;

  Glib::RefPtr<Gtk::TextBuffer> buf_;
  Glib::RefPtr<Gtk::CssProvider> chrome_css_;
  sigc::signal<void, Glib::ustring> signal_jump_;
  std::map<Glib::RefPtr<Gtk::TextTag>, std::string> link_hrefs_;
  std::vector<Glib::ustring> tag_stack_;
  int palette_ = 1;
};

/* Image path under the extracted book. Empty when the file would sit outside it. */
std::string topic_image_path(const std::string& src, const std::string& base_dir,
                             const std::string& book_root);

/* Link ink. Palette 2 is light gray on near-black; every other palette keeps the dark blue. */
inline const char* link_foreground(int palette)
{
  return palette == 2 ? "#8CB4E8" : "#0B3A96";
}

}  // namespace readomatic
