/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <glibmm/ustring.h>

#include <string>
#include <vector>

namespace readomatic {

/* Already-mounted USB mass-storage (TRAN=usb) and gio MTP volumes.
 * Host disks (nvme, /, /data) are never returned. Does not mount anything. */
struct LibraryDevice {
  Glib::ustring label;
  std::string uri;
};

std::vector<LibraryDevice> list_library_devices();

}  // namespace readomatic
