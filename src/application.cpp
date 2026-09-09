/* SPDX-License-Identifier: Unlicense */

#include "application.hpp"
#include "main_window.hpp"
#include "config.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <glib.h>
#include <glibmm/miscutils.h>

#include <iostream>

namespace readomatic {

Glib::RefPtr<Application> Application::create()
{
  return Glib::RefPtr<Application>(new Application());
}

Application::Application()
    : Gtk::Application(APP_ID, Gio::APPLICATION_FLAGS_NONE)
{
}

bool Application::take_instance_lock()
{
  const std::string dir = Glib::get_user_runtime_dir();
  g_mkdir_with_parents(dir.c_str(), 0700);
  const std::string path = Glib::build_filename(dir, "readomatic.lock");
  lock_fd_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
  if (lock_fd_ < 0)
    return true;
  if (flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
    close(lock_fd_);
    lock_fd_ = -1;
    return false;
  }
  return true;
}

void Application::on_startup()
{
  Gtk::Application::on_startup();
  if (auto settings = Gtk::Settings::get_default())
    settings->property_gtk_application_prefer_dark_theme() = false;
  lock_ok_ = take_instance_lock();
}

void Application::on_activate()
{
  const auto windows = get_windows();
  if (!windows.empty()) {
    windows.front()->present();
    return;
  }
  if (!lock_ok_) {
    std::cerr << "readomatic: already running\n";
    quit();
    return;
  }

  auto* win = new MainWindow();
  add_window(*win);
  win->signal_hide().connect([win]() { delete win; });
  win->present();
}

}  // namespace readomatic
