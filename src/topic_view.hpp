/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <gtkmm.h>
#include <libxml/tree.h>

#include <map>
#include <string>
#include <vector>

namespace readomatic {

class TopicView : public Gtk::TextView {
 public:
  TopicView();

  void load_xhtml(const std::string& xhtml, const std::string& base_dir);
  void clear_topic();
  bool scroll_to_id(const std::string& id);
  bool select_match(const Glib::ustring& query, int occurrence);
  void apply_appearance(const std::string& family, int size_pt, int weight, int palette);

  sigc::signal<void, Glib::ustring>& signal_jump() { return signal_jump_; }

 protected:
  bool on_button_release_event(GdkEventButton* event) override;

 private:
  void ensure_tags();
  void walk(xmlNode* node, const std::string& base_dir, int list_depth);
  void insert_text(const std::string& text, const std::vector<Glib::ustring>& tag_names);
  void insert_break();
  std::string resolve_src(const std::string& src, const std::string& base_dir) const;

  Glib::RefPtr<Gtk::TextBuffer> buf_;
  Glib::RefPtr<Gtk::CssProvider> chrome_css_;
  sigc::signal<void, Glib::ustring> signal_jump_;
  std::map<Glib::RefPtr<Gtk::TextTag>, std::string> link_hrefs_;
  std::vector<Glib::ustring> tag_stack_;
};

}  // namespace readomatic
