/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <gtkmm.h>

namespace readomatic {

class Application : public Gtk::Application {
 public:
  static Glib::RefPtr<Application> create();

 protected:
  Application();
  void on_startup() override;
  void on_activate() override;

 private:
  bool take_instance_lock();

  int lock_fd_ = -1;
  bool lock_ok_ = true;
};

}  // namespace readomatic
