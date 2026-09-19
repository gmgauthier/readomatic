/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "library_devices.hpp"
#include "settings.hpp"

#include <giomm.h>
#include <gtkmm.h>

#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace readomatic {

class LibraryPane;

class LibraryWindow : public Gtk::Window {
 public:
  LibraryWindow(Gtk::Window& parent, Settings& settings);
  ~LibraryWindow() override;

  void refresh_devices();

  sigc::signal<void, std::string> signal_open_book;

 private:
  void set_status(const Glib::ustring& text);
  void on_set_library();
  void on_device_changed();
  void persist_device_dir();
  void on_open();
  void on_transfer();
  void on_close();
  void run_copy();
  void on_copy_progress();
  void on_copy_done();
  void stop_copy();
  void set_busy(bool on);
  bool on_key_press(GdkEventKey* event);

  Settings& settings_;
  Gtk::Box root_{Gtk::ORIENTATION_VERTICAL, 6};
  Gtk::Paned panes_{Gtk::ORIENTATION_HORIZONTAL};
  LibraryPane* left_ = nullptr;
  LibraryPane* right_ = nullptr;
  Gtk::Box actions_{Gtk::ORIENTATION_HORIZONTAL, 8};
  Gtk::Button open_btn_{"_Open", true};
  Gtk::Label status_;
  Gtk::Button transfer_{"_Transfer", true};
  Gtk::Button close_{"_Close", true};

  std::vector<LibraryDevice> devices_;
  bool ignore_device_ = false;
  bool busy_ = false;

  std::vector<std::string> job_srcs_;
  std::string job_dest_;
  std::string job_src_root_;
  std::string job_dest_root_;
  bool job_src_is_library_ = false;
  Glib::RefPtr<Gio::Cancellable> cancellable_;
  std::thread copy_thread_;
  std::mutex mu_;
  Glib::ustring progress_text_;
  Glib::ustring result_text_;
  Glib::Dispatcher progress_;
  Glib::Dispatcher done_;
  sigc::connection progress_conn_;
  sigc::connection done_conn_;
};

}  // namespace readomatic
