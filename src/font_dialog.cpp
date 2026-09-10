/* SPDX-License-Identifier: Unlicense */

#include "font_dialog.hpp"

#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <glibmm/miscutils.h>

#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>

namespace readomatic {
namespace {

const char* weight_label(int w)
{
  if (w <= 300)
    return "Light";
  if (w <= 400)
    return "Regular";
  if (w <= 500)
    return "Medium";
  if (w <= 600)
    return "Semibold";
  if (w <= 700)
    return "Bold";
  return "Extra Bold";
}

}  // namespace

void ensure_user_fonts()
{
  static bool done = false;
  if (done)
    return;
  done = true;
  const std::string dir =
      Glib::build_filename(Glib::get_home_dir(), ".local", "share", "fonts");
  if (g_file_test(dir.c_str(), G_FILE_TEST_IS_DIR))
    FcConfigAppFontAddDir(nullptr, reinterpret_cast<const FcChar8*>(dir.c_str()));
}

FontDialog::FontDialog(Gtk::Window& parent, Settings& settings, TopicView& topic)
    : Gtk::Dialog("Appearance", parent, true),
      settings_(settings),
      topic_(topic),
      snapshot_(settings),
      pal_white_("Black on white"),
      pal_eggshell_("Dark gray on eggshell"),
      pal_dark_("Light gray on near-black")
{
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_border_width(8);

  pal_eggshell_.join_group(pal_white_);
  pal_dark_.join_group(pal_white_);

  size_.set_range(8, 32);
  size_.set_increments(1, 2);
  size_.set_value(settings_.font_size);
  size_.set_digits(0);

  family_.set_hexpand(true);
  fill_families();
  fill_weights();

  if (settings_.palette == 0)
    pal_white_.set_active();
  else if (settings_.palette == 2)
    pal_dark_.set_active();
  else
    pal_eggshell_.set_active();

  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_column_spacing(8);
  grid->set_row_spacing(6);
  int row = 0;
  grid->attach(*Gtk::manage(new Gtk::Label("Family", Gtk::ALIGN_START)), 0, row, 1, 1);
  grid->attach(family_, 1, row, 3, 1);
  ++row;
  grid->attach(*Gtk::manage(new Gtk::Label("Size", Gtk::ALIGN_START)), 0, row, 1, 1);
  grid->attach(size_, 1, row, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Weight", Gtk::ALIGN_START)), 2, row, 1, 1);
  grid->attach(weight_, 3, row, 1, 1);
  ++row;
  auto* pal_lab = Gtk::manage(new Gtk::Label("Page color", Gtk::ALIGN_START));
  pal_lab->set_margin_top(8);
  grid->attach(*pal_lab, 0, row, 4, 1);
  ++row;
  grid->attach(pal_white_, 0, row, 4, 1);
  ++row;
  grid->attach(pal_eggshell_, 0, row, 4, 1);
  ++row;
  grid->attach(pal_dark_, 0, row, 4, 1);

  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();

  family_.signal_changed().connect(sigc::mem_fun(*this, &FontDialog::on_family_changed));
  size_.signal_value_changed().connect(sigc::mem_fun(*this, &FontDialog::apply_preview));
  weight_.signal_changed().connect(sigc::mem_fun(*this, &FontDialog::apply_preview));
  pal_white_.signal_toggled().connect(sigc::mem_fun(*this, &FontDialog::apply_preview));
  pal_eggshell_.signal_toggled().connect(sigc::mem_fun(*this, &FontDialog::apply_preview));
  pal_dark_.signal_toggled().connect(sigc::mem_fun(*this, &FontDialog::apply_preview));

  signal_response().connect([this](int resp) {
    if (resp != Gtk::RESPONSE_OK)
      restore();
  });
}

void FontDialog::fill_families()
{
  ensure_user_fonts();
  filling_ = true;
  family_.remove_all();
  std::vector<Glib::ustring> names;
  auto ctx = get_pango_context();
  if (ctx) {
    for (const auto& fam : ctx->list_families()) {
      if (fam)
        names.push_back(fam->get_name());
    }
  }
  std::sort(names.begin(), names.end(), [](const Glib::ustring& a, const Glib::ustring& b) {
    return a.casefold() < b.casefold();
  });
  bool have = false;
  for (const auto& n : names) {
    family_.append(n);
    if (n == settings_.font_family)
      have = true;
  }
  if (!have && !settings_.font_family.empty())
    family_.prepend(settings_.font_family);
  if (!settings_.font_family.empty())
    family_.set_active_text(settings_.font_family);
  else if (!names.empty())
    family_.set_active(0);
  filling_ = false;
}

void FontDialog::fill_weights()
{
  filling_ = true;
  weight_.remove_all();
  std::set<int> weights;
  const Glib::ustring fam_name = family_.get_active_text();
  auto ctx = get_pango_context();
  if (ctx && !fam_name.empty()) {
    for (const auto& fam : ctx->list_families()) {
      if (!fam || fam->get_name() != fam_name)
        continue;
      for (const auto& face : fam->list_faces()) {
        if (!face)
          continue;
        weights.insert(static_cast<int>(face->describe().get_weight()));
      }
      break;
    }
  }
  if (weights.empty()) {
    weights.insert(400);
    weights.insert(700);
  }
  int pick = settings_.font_weight;
  int best = *weights.begin();
  int best_d = std::abs(best - pick);
  for (int w : weights) {
    char id[12];
    std::snprintf(id, sizeof(id), "%d", w);
    weight_.append(id, weight_label(w));
    const int d = std::abs(w - pick);
    if (d < best_d) {
      best = w;
      best_d = d;
    }
  }
  char pick_id[12];
  std::snprintf(pick_id, sizeof(pick_id), "%d", best);
  weight_.set_active_id(pick_id);
  filling_ = false;
}

void FontDialog::on_family_changed()
{
  if (filling_)
    return;
  fill_weights();
  apply_preview();
}

void FontDialog::apply_preview()
{
  if (filling_)
    return;
  Glib::ustring fam = family_.get_active_text();
  if (!fam.empty())
    settings_.font_family = fam;
  settings_.font_size = size_.get_value_as_int();
  try {
    const std::string id = weight_.get_active_id();
    if (!id.empty())
      settings_.font_weight = std::stoi(id);
  } catch (...) {
  }
  if (pal_white_.get_active())
    settings_.palette = 0;
  else if (pal_dark_.get_active())
    settings_.palette = 2;
  else
    settings_.palette = 1;
  topic_.apply_appearance(settings_.font_family, settings_.font_size, settings_.font_weight,
                          settings_.palette);
}

void FontDialog::restore()
{
  settings_ = snapshot_;
  topic_.apply_appearance(settings_.font_family, settings_.font_size, settings_.font_weight,
                          settings_.palette);
}

}  // namespace readomatic
