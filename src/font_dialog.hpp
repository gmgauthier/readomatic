/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "settings.hpp"
#include "topic_view.hpp"

#include <gtkmm.h>

namespace readomatic {

void ensure_user_fonts();

class FontDialog : public Gtk::Dialog {
 public:
  FontDialog(Gtk::Window& parent, Settings& settings, TopicView& topic);

 private:
  void fill_families();
  void fill_weights();
  void apply_preview();
  void on_family_changed();
  void restore();

  Settings& settings_;
  TopicView& topic_;
  Settings snapshot_;
  Gtk::ComboBoxText family_;
  Gtk::SpinButton size_;
  Gtk::ComboBoxText weight_;
  Gtk::RadioButton pal_white_;
  Gtk::RadioButton pal_eggshell_;
  Gtk::RadioButton pal_dark_;
  bool filling_ = false;
};

}  // namespace readomatic
