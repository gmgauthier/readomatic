/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "book.hpp"
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
  void show_current(const std::string& fragment = {});
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
  guint status_ctx_ = 0;

  Glib::RefPtr<Gtk::TreeStore> contents_store_;
  Glib::RefPtr<Gtk::ListStore> index_store_;
  Glib::RefPtr<Gtk::ListStore> find_store_;
  Gtk::TreeModelColumn<Glib::ustring> col_text_;
};

}  // namespace readomatic
