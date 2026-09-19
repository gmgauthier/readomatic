/* SPDX-License-Identifier: Unlicense */

#include "library_devices.hpp"

#include <giomm.h>
#include <glibmm/miscutils.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace readomatic {
namespace {

struct UnixMount {
  std::string device;
  std::string mountpoint;
};

std::string unescape_mount(const std::string& in)
{
  std::string out;
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '\\' && i + 3 < in.size() && std::isdigit(static_cast<unsigned char>(in[i + 1])) &&
        std::isdigit(static_cast<unsigned char>(in[i + 2])) &&
        std::isdigit(static_cast<unsigned char>(in[i + 3]))) {
      const int v = ((in[i + 1] - '0') << 6) | ((in[i + 2] - '0') << 3) | (in[i + 3] - '0');
      out.push_back(static_cast<char>(v));
      i += 3;
    } else {
      out.push_back(in[i]);
    }
  }
  return out;
}

std::string trim_copy(std::string s)
{
  while (!s.empty() &&
         (s.back() == ' ' || s.back() == '\n' || s.back() == '\r' || s.back() == '\t'))
    s.pop_back();
  return s;
}

std::vector<UnixMount> read_proc_mounts()
{
  std::vector<UnixMount> out;
  std::ifstream in("/proc/mounts");
  if (!in)
    return out;
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream iss(line);
    UnixMount m;
    std::string fstype;
    if (!(iss >> m.device >> m.mountpoint >> fstype))
      continue;
    m.mountpoint = unescape_mount(m.mountpoint);
    m.device = unescape_mount(m.device);
    out.push_back(std::move(m));
  }
  return out;
}

std::string device_for_path(const std::string& path, const std::vector<UnixMount>& mounts)
{
  std::string best;
  std::size_t best_len = 0;
  for (const auto& m : mounts) {
    const std::string& mp = m.mountpoint;
    if (mp.empty())
      continue;
    if (path == mp || (path.size() > mp.size() && path.compare(0, mp.size(), mp) == 0 &&
                       (mp == "/" || path[mp.size()] == '/'))) {
      if (mp.size() >= best_len) {
        best_len = mp.size();
        best = m.device;
      }
    }
  }
  return best;
}

bool is_host_os_mount(const std::string& mp)
{
  static const char* kSkip[] = {"/",    "/data", "/boot", "/boot/efi", "/home",  "/usr",
                                "/var", "/tmp",  "/opt",  "/snap",     "/media", "/run",
                                "/srv", "/root", "/etc",  "/dev",      "/proc",  "/sys"};
  for (const char* s : kSkip) {
    if (mp == s)
      return true;
  }
  return false;
}

bool skip_block_name(const std::string& name)
{
  return name.compare(0, 4, "loop") == 0 || name.compare(0, 2, "sr") == 0 ||
         name.compare(0, 4, "zram") == 0 || name.compare(0, 3, "ram") == 0 ||
         name.compare(0, 2, "dm") == 0 || name.find("mapper") != std::string::npos;
}

bool is_usb_block(const std::string& devnode)
{
  if (devnode.size() < 6 || devnode.compare(0, 5, "/dev/") != 0)
    return false;
  const std::string name = devnode.substr(5);
  if (name.empty() || name.find('/') != std::string::npos || skip_block_name(name))
    return false;
  char resolved[PATH_MAX];
  const std::string sys = std::string("/sys/class/block/") + name;
  if (!realpath(sys.c_str(), resolved))
    return false;
  return std::string(resolved).find("/usb") != std::string::npos;
}

std::string usb_model(const std::string& devnode)
{
  if (devnode.size() < 6 || devnode.compare(0, 5, "/dev/") != 0)
    return {};
  const std::string name = devnode.substr(5);
  std::vector<std::string> names = {name};
  char resolved[PATH_MAX];
  if (realpath((std::string("/sys/class/block/") + name).c_str(), resolved)) {
    std::string p = resolved;
    const auto slash = p.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
      const auto parent = p.substr(0, slash);
      const auto ps = parent.find_last_of('/');
      if (ps != std::string::npos)
        names.push_back(parent.substr(ps + 1));
    }
  }
  for (const auto& n : names) {
    std::ifstream in(std::string("/sys/class/block/") + n + "/device/model");
    if (!in)
      continue;
    std::string line;
    std::getline(in, line);
    line = trim_copy(line);
    if (!line.empty())
      return line;
  }
  return {};
}

bool scheme_is_mtp(const std::string& scheme)
{
  return scheme == "mtp" || scheme == "mtpfs" || scheme == "gphoto2";
}

Glib::ustring label_with_dev(const std::string& name, const std::string& unix_dev)
{
  Glib::ustring label = name.empty() ? Glib::ustring("Device") : Glib::ustring(name);
  if (!unix_dev.empty())
    label += "  ·  " + unix_dev;
  return label;
}

bool already_has(const std::vector<LibraryDevice>& devices, const Glib::RefPtr<Gio::File>& file)
{
  if (!file)
    return false;
  for (const auto& d : devices) {
    auto other = Gio::File::create_for_uri(d.uri);
    if (other && other->equal(file))
      return true;
  }
  return false;
}

bool weak_label(const Glib::ustring& label)
{
  return label.empty() || label == "mtp" || label == "mtpfs";
}

Glib::ustring mtp_label(const Glib::RefPtr<Gio::Mount>& mount,
                        const std::vector<Glib::RefPtr<Gio::Volume>>& volumes)
{
  auto root = mount->get_root();
  for (const auto& vol : volumes) {
    if (!vol)
      continue;
    auto ar = vol->get_activation_root();
    if (ar && root && ar->equal(root)) {
      const std::string vn = vol->get_name();
      if (!weak_label(Glib::ustring(vn)))
        return Glib::ustring(vn);
    }
  }
  const std::string name = mount->get_name();
  if (!weak_label(Glib::ustring(name)))
    return Glib::ustring(name);
  if (auto vol = mount->get_volume()) {
    const std::string vn = vol->get_name();
    if (!weak_label(Glib::ustring(vn)))
      return Glib::ustring(vn);
  }
  if (!name.empty())
    return Glib::ustring(name);
  return Glib::ustring("MTP device");
}

void add_device(std::vector<LibraryDevice>& devices, const Glib::RefPtr<Gio::File>& root,
                const Glib::ustring& label)
{
  if (!root)
    return;
  const std::string uri = root->get_uri();
  if (uri.empty())
    return;
  for (auto& d : devices) {
    auto other = Gio::File::create_for_uri(d.uri);
    if (!other || !other->equal(root))
      continue;
    if (weak_label(d.label) && !weak_label(label))
      d.label = label;
    return;
  }
  LibraryDevice d;
  d.label = label.empty() ? Glib::ustring(uri) : label;
  d.uri = uri;
  devices.push_back(std::move(d));
}

}  // namespace

std::vector<LibraryDevice> list_library_devices()
{
  std::vector<LibraryDevice> devices;
  const auto mounts = read_proc_mounts();

  try {
    auto monitor = Gio::VolumeMonitor::get();
    if (monitor) {
      const std::vector<Glib::RefPtr<Gio::Mount>> gio_mounts = monitor->get_mounts();
      const std::vector<Glib::RefPtr<Gio::Volume>> gio_volumes = monitor->get_volumes();
      for (const auto& mount : gio_mounts) {
        if (!mount)
          continue;
        auto root = mount->get_root();
        if (!root)
          continue;
        const std::string scheme = root->get_uri_scheme();
        if (scheme_is_mtp(scheme)) {
          add_device(devices, root, mtp_label(mount, gio_volumes));
          continue;
        }
        if (scheme != "file")
          continue;
        std::string unix_dev;
        std::string drive_name;
        if (auto vol = mount->get_volume()) {
          unix_dev = vol->get_identifier("unix-device");
          if (auto drive = vol->get_drive())
            drive_name = drive->get_name();
        }
        const std::string path = root->get_path();
        if (unix_dev.empty() && !path.empty())
          unix_dev = device_for_path(path, mounts);
        if (!is_usb_block(unix_dev))
          continue;
        if (!path.empty() && is_host_os_mount(path))
          continue;
        if (drive_name.empty())
          drive_name = mount->get_name();
        add_device(devices, root, label_with_dev(drive_name, unix_dev));
      }
    }
  } catch (const Glib::Error&) {
  }

  for (const auto& m : mounts) {
    if (!is_usb_block(m.device) || is_host_os_mount(m.mountpoint))
      continue;
    auto root = Gio::File::create_for_path(m.mountpoint);
    if (!root || already_has(devices, root))
      continue;
    std::string name = usb_model(m.device);
    if (name.empty())
      name = Glib::path_get_basename(m.mountpoint);
    add_device(devices, root, label_with_dev(name, m.device));
  }

  std::stable_partition(devices.begin(), devices.end(), [](const LibraryDevice& d) {
    auto f = Gio::File::create_for_uri(d.uri);
    return f && f->get_uri_scheme() == "file";
  });
  return devices;
}

}  // namespace readomatic
