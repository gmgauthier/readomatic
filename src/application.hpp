/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <gtkmm.h>

#include <string>
#include <vector>

namespace readomatic {

class MainWindow;

class Application : public Gtk::Application {
 public:
  static Glib::RefPtr<Application> create();
  void set_open_paths(std::vector<std::string> paths);

 protected:
  Application();
  ~Application() override;
  void on_startup() override;
  void on_activate() override;

 private:
  bool take_instance_lock();
  void listen_open_socket();
  void close_open_socket();
  bool send_paths_to_primary() const;
  bool on_listen_io(Glib::IOCondition cond);
  void handle_open_payload(const std::string& payload);
  void ensure_window();
  void open_first(const std::vector<std::string>& paths);
  static std::string runtime_dir();
  static std::string lock_path();
  static std::string socket_path();

  std::vector<std::string> open_paths_;
  MainWindow* window_ = nullptr;
  int lock_fd_ = -1;
  int listen_fd_ = -1;
  bool lock_ok_ = true;
  sigc::connection listen_conn_;
};

}  // namespace readomatic
