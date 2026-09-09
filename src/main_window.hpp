/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "book.hpp"
#include "history.hpp"
#include "topic_view.hpp"

#include <gtkmm.h>

namespace readomatic {

class MainWindow : public Gtk::Window {
 public:
  MainWindow();

 private:
  void build_menu();
  void build_toolbar();
  void build_body();
  void load_css();
  void set_status(const Glib::ustring& text);

  void on_open();
  void on_close_book();
  void on_quit();
  void on_about();
  void on_nav_page(int page);
  void on_jump(const Glib::ustring& href);
  void on_spine_step(int delta);
  void on_back();
  void on_contents_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* col);
  void on_index_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* col);
  void on_find_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* col);
  void on_find();
  bool on_contents_motion(GdkEventMotion* event);
  bool on_contents_leave(GdkEventCrossing* event);
  bool on_contents_key(GdkEventKey* event);
  void on_contents_cell_data(Gtk::CellRenderer* cell,
                             const Gtk::TreeModel::const_iterator& it);
  bool on_find_motion(GdkEventMotion* event);
  bool on_find_leave(GdkEventCrossing* event);
  bool on_find_key(GdkEventKey* event);
  void on_find_cell_data(Gtk::CellRenderer* cell,
                         const Gtk::TreeModel::const_iterator& it);
  void show_current(const std::string& fragment = {});
  void fill_contents();
  void fill_index();
  void highlight_contents();
  void style_list_column(Gtk::TreeView& view);
  void snap_nav_left(Gtk::TreeView& view, Gtk::ScrolledWindow& scroll);
  void keep_nav_left(Gtk::TreeView& view, Gtk::ScrolledWindow& scroll);
  void scroll_nav_vertically(Gtk::TreeView& view, const Gtk::TreeModel::Path& path);
  void relayout_nav(Gtk::TreeView& view, Gtk::ScrolledWindow& scroll);
  double topic_scroll() const;
  void set_topic_scroll(double value);
  void on_not_yet(const Glib::ustring& feature);

  Gtk::Box root_{Gtk::ORIENTATION_VERTICAL, 0};
  Gtk::MenuBar menubar_;
  Gtk::Box toolbar_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::Button btn_contents_{"Contents"};
  Gtk::Button btn_index_{"Index"};
  Gtk::Button btn_find_{"Find"};
  Gtk::Button btn_back_{"Back"};
  Gtk::Button btn_prev_{"<<"};
  Gtk::Button btn_next_{">>"};
  Gtk::Button btn_print_{"Print"};
  Gtk::Paned paned_{Gtk::ORIENTATION_HORIZONTAL};
  Gtk::Notebook nav_;
  Gtk::ScrolledWindow contents_scroll_;
  Gtk::TreeView contents_view_;
  Gtk::ScrolledWindow index_scroll_;
  Gtk::TreeView index_view_;
  Gtk::Box find_box_{Gtk::ORIENTATION_VERTICAL, 4};
  Gtk::Entry find_entry_;
  Gtk::ScrolledWindow find_scroll_;
  Gtk::TreeView find_view_;
  Gtk::ScrolledWindow topic_scroll_;
  TopicView topic_view_;
  Gtk::Statusbar status_;
  Book book_;
  History history_;
  bool suppress_history_ = false;
  guint status_ctx_ = 0;

  Glib::RefPtr<Gtk::TreeStore> contents_store_;
  Glib::RefPtr<Gtk::ListStore> index_store_;
  Glib::RefPtr<Gtk::ListStore> find_store_;
  Gtk::TreeModelColumn<Glib::ustring> col_text_;
  Gtk::TreeModelColumn<Glib::ustring> col_href_;
  Gtk::TreeModelColumn<int> col_occ_;
  Gtk::TreeModel::Path contents_hover_path_;
  Gtk::TreeModel::Path contents_current_path_;
  Gtk::TreeModel::Path find_hover_path_;
  Gtk::TreeModel::Path find_current_path_;
  std::string loaded_fragment_;
  Glib::ustring last_find_query_;
};

}  // namespace readomatic
