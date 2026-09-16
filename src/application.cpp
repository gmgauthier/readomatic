/* SPDX-License-Identifier: Unlicense */

#include "application.hpp"
#include "config.hpp"
#include "main_window.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <glib.h>
#include <glibmm/miscutils.h>

#include <cstring>
#include <iostream>

namespace readomatic {
namespace {

bool fill_unix_addr(sockaddr_un* addr, const std::string& path)
{
  if (path.size() >= sizeof(addr->sun_path))
    return false;
  std::memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  std::memcpy(addr->sun_path, path.c_str(), path.size() + 1);
  return true;
}

std::string canonicalize_arg(const std::string& arg)
{
  auto file = Gio::File::create_for_commandline_arg(arg);
  const std::string path = file->get_path();
  return path.empty() ? arg : path;
}

}  // namespace

Glib::RefPtr<Application> Application::create()
{
  return Glib::RefPtr<Application>(new Application());
}

Application::Application()
    : Gtk::Application(APP_ID, Gio::APPLICATION_NON_UNIQUE)
{
}

Application::~Application()
{
  close_open_socket();
  if (lock_fd_ >= 0) {
    close(lock_fd_);
    lock_fd_ = -1;
  }
}

void Application::set_open_paths(std::vector<std::string> paths)
{
  open_paths_.clear();
  open_paths_.reserve(paths.size());
  for (const auto& p : paths)
    open_paths_.push_back(canonicalize_arg(p));
}

std::string Application::runtime_dir()
{
  const std::string dir = Glib::get_user_runtime_dir();
  g_mkdir_with_parents(dir.c_str(), 0700);
  return dir;
}

std::string Application::lock_path()
{
  return Glib::build_filename(runtime_dir(), "readomatic.lock");
}

std::string Application::socket_path()
{
  return Glib::build_filename(runtime_dir(), "readomatic.sock");
}

bool Application::take_instance_lock()
{
  lock_fd_ = ::open(lock_path().c_str(), O_CREAT | O_RDWR, 0600);
  if (lock_fd_ < 0)
    return true;
  if (flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
    close(lock_fd_);
    lock_fd_ = -1;
    return false;
  }
  return true;
}

void Application::listen_open_socket()
{
  const std::string path = socket_path();
  ::unlink(path.c_str());

  listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listen_fd_ < 0)
    return;

  sockaddr_un addr;
  if (!fill_unix_addr(&addr, path)) {
    close(listen_fd_);
    listen_fd_ = -1;
    return;
  }
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      ::listen(listen_fd_, 8) != 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return;
  }
  ::chmod(path.c_str(), 0600);

  listen_conn_ = Glib::signal_io().connect(sigc::mem_fun(*this, &Application::on_listen_io),
                                           listen_fd_, Glib::IO_IN | Glib::IO_HUP);
}

void Application::close_open_socket()
{
  listen_conn_.disconnect();
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    listen_fd_ = -1;
  }
  ::unlink(socket_path().c_str());
}

bool Application::send_paths_to_primary() const
{
  const std::string path = socket_path();
  sockaddr_un addr;
  if (!fill_unix_addr(&addr, path))
    return false;

  for (int attempt = 0; attempt < 25; ++attempt) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
      return false;
    const int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0) {
      close(fd);
      g_usleep(20000);
      continue;
    }
    for (const auto& p : open_paths_) {
      if (::write(fd, p.data(), p.size()) != static_cast<ssize_t>(p.size()))
        break;
      if (::write(fd, "\n", 1) != 1)
        break;
    }
    close(fd);
    return true;
  }
  return false;
}

bool Application::on_listen_io(Glib::IOCondition)
{
  if (listen_fd_ < 0)
    return false;

  const int cfd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
  if (cfd < 0)
    return true;

  std::string buf;
  char tmp[4096];
  ssize_t n = 0;
  while ((n = ::read(cfd, tmp, sizeof(tmp))) > 0) {
    buf.append(tmp, static_cast<size_t>(n));
    if (buf.size() > 1024 * 1024)
      break;
  }
  close(cfd);
  handle_open_payload(buf);
  return true;
}

void Application::handle_open_payload(const std::string& payload)
{
  std::vector<std::string> paths;
  std::string line;
  for (char ch : payload) {
    if (ch == '\n' || ch == '\0') {
      if (!line.empty()) {
        paths.push_back(canonicalize_arg(line));
        line.clear();
      }
    } else {
      line += ch;
    }
  }
  if (!line.empty())
    paths.push_back(canonicalize_arg(line));

  ensure_window();
  open_first(paths);
  if (window_ != nullptr)
    window_->present();
}

void Application::ensure_window()
{
  if (window_ != nullptr)
    return;
  auto* win = new MainWindow();
  window_ = win;
  add_window(*win);
  win->signal_hide().connect([this, win]() {
    if (window_ == win)
      window_ = nullptr;
    delete win;
  });
}

void Application::open_first(const std::vector<std::string>& paths)
{
  if (window_ == nullptr || paths.empty())
    return;
  window_->open_path(paths.front());
}

void Application::on_startup()
{
  Gtk::Application::on_startup();
  if (auto settings = Gtk::Settings::get_default())
    settings->property_gtk_application_prefer_dark_theme() = false;
  lock_ok_ = take_instance_lock();
  if (lock_ok_)
    listen_open_socket();
}

void Application::on_activate()
{
  if (!lock_ok_) {
    if (!send_paths_to_primary())
      std::cerr << "readomatic: already running\n";
    quit();
    return;
  }

  ensure_window();
  open_first(open_paths_);
  if (window_ != nullptr)
    window_->present();
}

}  // namespace readomatic
