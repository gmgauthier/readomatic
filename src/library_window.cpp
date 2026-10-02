/* SPDX-License-Identifier: Unlicense */

#include "library_window.hpp"
#include "lastread.hpp"
#include "library_catalog.hpp"

#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>

namespace readomatic {
namespace {

Glib::ustring format_size(guint64 n)
{
  char buf[32];
  if (n < 1024)
    std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(n));
  else if (n < 1024ull * 1024ull)
    std::snprintf(buf, sizeof(buf), "%llu KB", static_cast<unsigned long long>((n + 512) / 1024));
  else if (n < 1024ull * 1024ull * 1024ull)
    std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(n) / (1024.0 * 1024.0));
  else
    std::snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(n) / (1024.0 * 1024.0 * 1024.0));
  return buf;
}

bool skip_name(const std::string& name)
{
  if (name.empty() || name[0] == '.')
    return true;
  if (name.size() > 9 && name.compare(name.size() - 9, 9, ".lastread") == 0)
    return true;
  return false;
}

bool dir_type(Gio::FileType type)
{
  return type == Gio::FILE_TYPE_DIRECTORY || type == Gio::FILE_TYPE_MOUNTABLE;
}

}  // namespace

class LibraryPane : public Gtk::Box {
 public:
  struct Columns : public Gtk::TreeModel::ColumnRecord {
    Columns()
    {
      add(tick);
      add(tickable);
      add(is_dir);
      add(is_parent);
      add(name);
      add(size_text);
      add(tags_text);
      add(uri);
      add(rel);
      add(weight);
      add(is_group);
    }
    Gtk::TreeModelColumn<bool> tick;
    Gtk::TreeModelColumn<bool> tickable;
    Gtk::TreeModelColumn<bool> is_dir;
    Gtk::TreeModelColumn<bool> is_parent;
    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<Glib::ustring> size_text;
    Gtk::TreeModelColumn<Glib::ustring> tags_text;
    Gtk::TreeModelColumn<Glib::ustring> uri;
    Gtk::TreeModelColumn<Glib::ustring> rel;
    Gtk::TreeModelColumn<int> weight;
    Gtk::TreeModelColumn<bool> is_group;
  };

  LibraryPane(bool device_side, Settings& settings)
      : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0),
        device_side_(device_side),
        settings_(settings)
  {
    store_ = Gtk::TreeStore::create(cols_);
    head_.get_style_context()->add_class("readomatic-lib-head");
    path_.set_ellipsize(Pango::ELLIPSIZE_START);
    path_.set_halign(Gtk::ALIGN_START);
    path_.set_margin_start(6);
    path_.set_margin_end(6);
    path_.set_margin_top(2);
    path_.set_margin_bottom(2);

    if (device_side_) {
      combo_.set_hexpand(true);
      head_.pack_start(combo_, Gtk::PACK_EXPAND_WIDGET);
    } else {
      title_.set_markup("<b>Library</b>");
      set_btn_.set_label("Library folder…");
      head_.pack_start(title_, Gtk::PACK_EXPAND_WIDGET);
      head_.pack_start(set_btn_, Gtk::PACK_SHRINK);
    }

    view_.set_model(store_);
    view_.set_headers_visible(false);
    view_.set_enable_search(true);
    view_.set_search_column(cols_.name);

    auto* tick = Gtk::manage(new Gtk::CellRendererToggle());
    tick->signal_toggled().connect(sigc::mem_fun(*this, &LibraryPane::on_toggled));
    auto* tick_col = Gtk::manage(new Gtk::TreeViewColumn());
    tick_col->pack_start(*tick, false);
    tick_col->add_attribute(tick->property_active(), cols_.tick);
    tick_col->add_attribute(tick->property_visible(), cols_.tickable);
    tick_col->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    tick_col->set_fixed_width(28);
    view_.append_column(*tick_col);

    auto* name = Gtk::manage(new Gtk::CellRendererText());
    name->property_ellipsize() = Pango::ELLIPSIZE_END;
    auto* name_col = Gtk::manage(new Gtk::TreeViewColumn());
    name_col->pack_start(*name, true);
    name_col->add_attribute(name->property_text(), cols_.name);
    name_col->add_attribute(name->property_weight(), cols_.weight);
    name_col->set_expand(true);
    view_.append_column(*name_col);
    view_.set_expander_column(*name_col);

    auto* sz = Gtk::manage(new Gtk::CellRendererText());
    sz->property_xalign() = 1.0;
    auto* sz_col = Gtk::manage(new Gtk::TreeViewColumn());
    sz_col->pack_start(*sz, false);
    sz_col->add_attribute(sz->property_text(), cols_.size_text);
    sz_col->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    sz_col->set_fixed_width(72);
    view_.append_column(*sz_col);

    if (!device_side_) {
      auto* tags = Gtk::manage(new Gtk::CellRendererText());
      tags->property_ellipsize() = Pango::ELLIPSIZE_END;
      auto* tags_col = Gtk::manage(new Gtk::TreeViewColumn("Tags"));
      tags_col->pack_start(*tags, true);
      tags_col->add_attribute(tags->property_text(), cols_.tags_text);
      tags_col->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
      tags_col->set_fixed_width(120);
      view_.append_column(*tags_col);
      view_.set_headers_visible(true);
    }

    view_.signal_row_activated().connect(sigc::mem_fun(*this, &LibraryPane::on_row_activated));
    scroll_.add(view_);
    scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll_.set_shadow_type(Gtk::SHADOW_IN);
    scroll_.set_hexpand(true);
    scroll_.set_vexpand(true);

    if (!device_side_) {
      add_btn_.set_label("Add…");
      del_btn_.set_label("Delete");
      group_btn_.set_label("Group…");
      tag_btn_.set_label("Tag…");
      filter_.set_tooltip_text("Show all, a group, or a tag");
      filter_.set_size_request(140, -1);
      org_.set_spacing(4);
      org_.set_border_width(4);
      org_.pack_start(add_btn_, Gtk::PACK_SHRINK);
      org_.pack_start(del_btn_, Gtk::PACK_SHRINK);
      org_.pack_start(group_btn_, Gtk::PACK_SHRINK);
      org_.pack_start(tag_btn_, Gtk::PACK_SHRINK);
      filter_row_.set_border_width(4);
      filter_row_.set_spacing(4);
      filter_row_.pack_start(*Gtk::manage(new Gtk::Label("Show", Gtk::ALIGN_START)),
                             Gtk::PACK_SHRINK);
      filter_row_.pack_start(filter_, Gtk::PACK_EXPAND_WIDGET);
    }

    pack_start(head_, Gtk::PACK_SHRINK);
    pack_start(path_, Gtk::PACK_SHRINK);
    pack_start(scroll_, Gtk::PACK_EXPAND_WIDGET);
    if (!device_side_) {
      pack_start(org_, Gtk::PACK_SHRINK);
      pack_start(filter_row_, Gtk::PACK_SHRINK);
    }
    if (!device_side_) {
      filter_.signal_changed().connect([this]() {
        if (filling_filter_)
          return;
        filter_id_ = filter_.get_active_id().raw();
        refresh_catalog();
      });
    }
  }

  Gtk::ComboBoxText& combo()
  {
    return combo_;
  }
  Gtk::Button& set_button()
  {
    return set_btn_;
  }
  Gtk::Button& add_button()
  {
    return add_btn_;
  }
  Gtk::Button& delete_button()
  {
    return del_btn_;
  }
  Gtk::Button& group_button()
  {
    return group_btn_;
  }
  Gtk::Button& tag_button()
  {
    return tag_btn_;
  }
  Gtk::ComboBoxText& filter()
  {
    return filter_;
  }
  bool filling_filter() const
  {
    return filling_filter_;
  }

  void set_ceiling(const Glib::RefPtr<Gio::File>& ceiling)
  {
    ceiling_ = ceiling;
  }
  void set_device_root(const std::string& root)
  {
    device_root_ = root;
  }
  std::string device_root() const
  {
    return device_root_;
  }

  Glib::RefPtr<Gio::File> current() const
  {
    return current_;
  }

  void navigate(const Glib::RefPtr<Gio::File>& dir)
  {
    current_ = dir;
    if (dir && ceiling_ && !(dir->equal(ceiling_) || dir->has_prefix(ceiling_)))
      current_.reset();
    refresh();
    signal_navigated.emit();
  }

  bool at_ceiling() const
  {
    if (!current_)
      return true;
    if (!ceiling_)
      return !current_->get_parent();
    return current_->equal(ceiling_);
  }

  void refresh()
  {
    if (!device_side_) {
      refresh_catalog();
      return;
    }
    store_->clear();
    if (!current_) {
      path_.set_text("");
      return;
    }
    path_.set_text(current_->get_parse_name());

    if (!at_ceiling()) {
      auto parent = current_->get_parent();
      if (parent) {
        auto row = *store_->append();
        row[cols_.tick] = false;
        row[cols_.tickable] = false;
        row[cols_.is_dir] = true;
        row[cols_.is_parent] = true;
        row[cols_.name] = "[..]";
        row[cols_.size_text] = "";
        row[cols_.uri] = parent->get_uri();
        row[cols_.rel] = "";
        row[cols_.tags_text] = "";
        row[cols_.is_group] = false;
        row[cols_.weight] = Pango::WEIGHT_BOLD;
      }
    }

    struct Item {
      bool is_dir = false;
      Glib::ustring name;
      Glib::ustring size_text;
      Glib::ustring uri;
    };
    std::vector<Item> items;
    try {
      auto en = current_->enumerate_children(
          "standard::name,standard::type,standard::size,standard::display-name,standard::is-"
          "hidden");
      while (auto info = en->next_file()) {
        const std::string name = info->get_name();
        if (skip_name(name) || (info->has_attribute("standard::is-hidden") && info->is_hidden()))
          continue;
        const auto type = info->get_file_type();
        if (!dir_type(type) && type != Gio::FILE_TYPE_REGULAR)
          continue;
        if (!dir_type(type) && !is_ebook_name(name))
          continue;
        Item it;
        it.is_dir = dir_type(type);
        const std::string display = info->get_display_name();
        it.name = display.empty() ? Glib::ustring(name) : Glib::ustring(display);
        it.size_text =
            it.is_dir ? Glib::ustring() : format_size(static_cast<guint64>(info->get_size()));
        it.uri = current_->get_child(name)->get_uri();
        items.push_back(std::move(it));
      }
    } catch (const Glib::Error& e) {
      signal_status.emit(e.what());
      return;
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
      if (a.is_dir != b.is_dir)
        return a.is_dir;
      return a.name.lowercase() < b.name.lowercase();
    });
    for (const auto& it : items) {
      auto row = *store_->append();
      row[cols_.tick] = false;
      row[cols_.tickable] = true;
      row[cols_.is_dir] = it.is_dir;
      row[cols_.is_parent] = false;
      row[cols_.name] = it.name;
      row[cols_.size_text] = it.size_text;
      row[cols_.uri] = it.uri;
      row[cols_.rel] = "";
      row[cols_.tags_text] = "";
      row[cols_.is_group] = false;
      row[cols_.weight] = it.is_dir ? Pango::WEIGHT_BOLD : Pango::WEIGHT_NORMAL;
    }
  }

  void refresh_catalog()
  {
    store_->clear();
    filling_filter_ = true;
    const std::string keep_filter = filter_id_;
    filter_.remove_all();
    filter_.append("all", "All");
    filter_.append("none", "(Uncategorized)");
    if (!current_) {
      path_.set_text("");
      filling_filter_ = false;
      return;
    }
    const std::string root = current_->get_path();
    path_.set_text(current_->get_parse_name());
    auto cat = LibraryCatalog::load(root);
    const auto books = scan_library(root, cat);
    std::vector<std::string> groups = cat.groups;
    std::vector<std::string> all_tags;
    for (const auto& b : books) {
      if (!b.group.empty() && std::find(groups.begin(), groups.end(), b.group) == groups.end())
        groups.push_back(b.group);
      for (const auto& t : b.tags)
        all_tags.push_back(t);
    }
    std::sort(groups.begin(), groups.end());
    groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
    std::sort(all_tags.begin(), all_tags.end());
    all_tags.erase(std::unique(all_tags.begin(), all_tags.end()), all_tags.end());
    for (const auto& g : groups)
      filter_.append("g:" + g, g);
    for (const auto& t : all_tags)
      filter_.append("t:" + t, "Tag: " + t);
    if (keep_filter.empty() || filter_.get_active_id().raw() == keep_filter || keep_filter == "all")
      filter_.set_active_id(keep_filter.empty() ? "all" : keep_filter);
    else if (!keep_filter.empty())
      filter_.set_active_id(keep_filter);
    if (filter_.get_active_id().empty())
      filter_.set_active_id("all");
    filter_id_ = filter_.get_active_id().raw();
    filling_filter_ = false;

    auto show_book = [&](const CatalogBook& b) {
      if (filter_id_ == "all" || filter_id_.empty())
        return true;
      if (filter_id_ == "none")
        return b.group.empty();
      if (filter_id_.compare(0, 2, "g:") == 0)
        return b.group == filter_id_.substr(2);
      if (filter_id_.compare(0, 2, "t:") == 0) {
        const std::string tag = filter_id_.substr(2);
        return std::find(b.tags.begin(), b.tags.end(), tag) != b.tags.end();
      }
      return true;
    };

    auto append_book = [&](const Gtk::TreeNodeChildren& parent, const CatalogBook& b) {
      auto row = *store_->append(parent);
      row[cols_.tick] = false;
      row[cols_.tickable] = true;
      row[cols_.is_dir] = false;
      row[cols_.is_parent] = false;
      row[cols_.is_group] = false;
      row[cols_.name] = b.name;
      row[cols_.size_text] = format_size(b.size);
      row[cols_.tags_text] = join_tags(b.tags);
      row[cols_.uri] = Gio::File::create_for_path(b.abs)->get_uri();
      row[cols_.rel] = b.rel;
      row[cols_.weight] = Pango::WEIGHT_NORMAL;
    };

    const bool group_filter = filter_id_.compare(0, 2, "g:") == 0;
    const bool none_filter = filter_id_ == "none";
    if (!none_filter && !group_filter) {
      for (const auto& g : groups) {
        std::vector<const CatalogBook*> kids;
        for (const auto& b : books) {
          if (b.group == g && show_book(b))
            kids.push_back(&b);
        }
        if (kids.empty() && filter_id_ != "all")
          continue;
        auto grow = *store_->append();
        grow[cols_.tick] = false;
        grow[cols_.tickable] = true;
        grow[cols_.is_dir] = true;
        grow[cols_.is_parent] = false;
        grow[cols_.is_group] = true;
        grow[cols_.name] = g;
        grow[cols_.size_text] = "";
        grow[cols_.tags_text] = "";
        grow[cols_.uri] = Gio::File::create_for_path(Glib::build_filename(root, g))->get_uri();
        grow[cols_.rel] = g;
        grow[cols_.weight] = Pango::WEIGHT_BOLD;
        for (const auto* b : kids)
          append_book(grow.children(), *b);
      }
    } else if (group_filter) {
      const std::string g = filter_id_.substr(2);
      auto grow = *store_->append();
      grow[cols_.tick] = false;
      grow[cols_.tickable] = true;
      grow[cols_.is_dir] = true;
      grow[cols_.is_parent] = false;
      grow[cols_.is_group] = true;
      grow[cols_.name] = g;
      grow[cols_.size_text] = "";
      grow[cols_.tags_text] = "";
      grow[cols_.uri] = Gio::File::create_for_path(Glib::build_filename(root, g))->get_uri();
      grow[cols_.rel] = g;
      grow[cols_.weight] = Pango::WEIGHT_BOLD;
      for (const auto& b : books) {
        if (b.group == g)
          append_book(grow.children(), b);
      }
    }
    for (const auto& b : books) {
      if (!b.group.empty())
        continue;
      if (!show_book(b))
        continue;
      append_book(store_->children(), b);
    }
    view_.expand_all();
  }

  std::vector<Glib::RefPtr<Gio::File>> ticked() const
  {
    std::vector<Glib::RefPtr<Gio::File>> out;
    std::function<void(const Gtk::TreeNodeChildren&)> walk =
        [&](const Gtk::TreeNodeChildren& nodes) {
          for (const auto& row : nodes) {
            if (row[cols_.tick] && !row[cols_.is_parent] && !row[cols_.is_group]) {
              const Glib::ustring uri = row[cols_.uri];
              if (!uri.empty())
                out.push_back(Gio::File::create_for_uri(uri.raw()));
            }
            walk(row.children());
          }
        };
    walk(store_->children());
    return out;
  }

  std::vector<std::string> ticked_rels() const
  {
    std::vector<std::string> out;
    std::function<void(const Gtk::TreeNodeChildren&)> walk =
        [&](const Gtk::TreeNodeChildren& nodes) {
          for (const auto& row : nodes) {
            if (row[cols_.tick] && !row[cols_.is_parent] && !row[cols_.is_group]) {
              const std::string rel = Glib::ustring(row[cols_.rel]).raw();
              if (!rel.empty())
                out.push_back(rel);
            }
            walk(row.children());
          }
        };
    walk(store_->children());
    return out;
  }

  std::string selected_group()
  {
    auto sel = view_.get_selection()->get_selected();
    if (!sel)
      return {};
    auto row = *sel;
    if (row[cols_.is_group])
      return Glib::ustring(row[cols_.rel]).raw();
    const std::string rel = Glib::ustring(row[cols_.rel]).raw();
    const auto slash = rel.find('/');
    if (slash != std::string::npos)
      return rel.substr(0, slash);
    return {};
  }

  void clear_ticks()
  {
    std::function<void(const Gtk::TreeNodeChildren&)> walk =
        [&](const Gtk::TreeNodeChildren& nodes) {
          for (auto& row : nodes) {
            row[cols_.tick] = false;
            walk(row.children());
          }
        };
    walk(store_->children());
  }

  void set_list_sensitive(bool on)
  {
    view_.set_sensitive(on);
    combo_.set_sensitive(on);
    set_btn_.set_sensitive(on);
    add_btn_.set_sensitive(on);
    del_btn_.set_sensitive(on);
    group_btn_.set_sensitive(on);
    tag_btn_.set_sensitive(on);
    filter_.set_sensitive(on);
  }

  sigc::signal<void, Glib::ustring> signal_status;
  sigc::signal<void> signal_navigated;

 private:
  void on_toggled(const Glib::ustring& path)
  {
    auto it = store_->get_iter(path);
    if (!it)
      return;
    auto row = *it;
    if (!row[cols_.tickable])
      return;
    const bool on = !row[cols_.tick];
    row[cols_.tick] = on;
    if (row[cols_.is_group]) {
      for (auto& child : row.children())
        child[cols_.tick] = on;
    }
  }

  void on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*)
  {
    auto it = store_->get_iter(path);
    if (!it)
      return;
    auto row = *it;
    const Glib::ustring uri = row[cols_.uri];
    if (uri.empty())
      return;
    if (row[cols_.is_group]) {
      if (view_.row_expanded(path))
        view_.collapse_row(path);
      else
        view_.expand_row(path, false);
      return;
    }
    if (row[cols_.is_dir] || row[cols_.is_parent]) {
      navigate(Gio::File::create_for_uri(uri.raw()));
      return;
    }
    if (row[cols_.tickable])
      row[cols_.tick] = !row[cols_.tick];
  }

  bool device_side_ = false;
  Settings& settings_;
  Columns cols_;
  Glib::RefPtr<Gtk::TreeStore> store_;
  Glib::RefPtr<Gio::File> ceiling_;
  Glib::RefPtr<Gio::File> current_;
  std::string device_root_;
  Gtk::Box head_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::Label title_;
  Gtk::Button set_btn_;
  Gtk::ComboBoxText combo_;
  Gtk::Label path_;
  Gtk::ScrolledWindow scroll_;
  Gtk::TreeView view_;
  Gtk::Box org_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::Box filter_row_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::Button add_btn_;
  Gtk::Button del_btn_;
  Gtk::Button group_btn_;
  Gtk::Button tag_btn_;
  Gtk::ComboBoxText filter_;
  std::string filter_id_ = "all";
  bool filling_filter_ = false;
};

LibraryWindow::LibraryWindow(Gtk::Window& parent, Settings& settings)
    : settings_(settings)
{
  set_transient_for(parent);
  set_title("Library");
  set_default_size(920, 560);
  set_type_hint(Gdk::WINDOW_TYPE_HINT_DIALOG);
  set_border_width(8);

  left_ = Gtk::manage(new LibraryPane(false, settings_));
  right_ = Gtk::manage(new LibraryPane(true, settings_));
  left_->signal_status.connect(sigc::mem_fun(*this, &LibraryWindow::set_status));
  right_->signal_status.connect(sigc::mem_fun(*this, &LibraryWindow::set_status));
  left_->set_button().signal_clicked().connect(
      sigc::mem_fun(*this, &LibraryWindow::on_set_library));
  left_->add_button().signal_clicked().connect(sigc::mem_fun(*this, &LibraryWindow::on_add_books));
  left_->delete_button().signal_clicked().connect(
      sigc::mem_fun(*this, &LibraryWindow::on_delete_books));
  left_->group_button().signal_clicked().connect(sigc::mem_fun(*this, &LibraryWindow::on_group));
  left_->tag_button().signal_clicked().connect(sigc::mem_fun(*this, &LibraryWindow::on_tag));
  right_->combo().signal_changed().connect(sigc::mem_fun(*this, &LibraryWindow::on_device_changed));
  right_->signal_navigated.connect(sigc::mem_fun(*this, &LibraryWindow::persist_device_dir));

  panes_.pack1(*left_, true, true);
  panes_.pack2(*right_, true, true);
  panes_.set_wide_handle(true);
  panes_.set_position(450);

  open_btn_.signal_clicked().connect(sigc::mem_fun(*this, &LibraryWindow::on_open));
  status_.set_halign(Gtk::ALIGN_START);
  status_.set_hexpand(true);
  status_.set_ellipsize(Pango::ELLIPSIZE_END);
  transfer_.signal_clicked().connect(sigc::mem_fun(*this, &LibraryWindow::on_transfer));
  close_.signal_clicked().connect(sigc::mem_fun(*this, &LibraryWindow::on_close));
  actions_.pack_start(open_btn_, Gtk::PACK_SHRINK);
  actions_.pack_start(status_, Gtk::PACK_EXPAND_WIDGET);
  actions_.pack_start(transfer_, Gtk::PACK_SHRINK);
  actions_.pack_start(close_, Gtk::PACK_SHRINK);

  root_.pack_start(panes_, Gtk::PACK_EXPAND_WIDGET);
  root_.pack_start(actions_, Gtk::PACK_SHRINK);
  add(root_);

  progress_conn_ = progress_.connect(sigc::mem_fun(*this, &LibraryWindow::on_copy_progress));
  done_conn_ = done_.connect(sigc::mem_fun(*this, &LibraryWindow::on_copy_done));

  add_events(Gdk::KEY_PRESS_MASK);
  signal_key_press_event().connect(sigc::mem_fun(*this, &LibraryWindow::on_key_press), false);
  signal_delete_event().connect([this](GdkEventAny*) {
    hide();
    return true;
  });
  signal_hide().connect([this]() { stop_copy(); });

  if (!settings_.library_dir.empty() &&
      Glib::file_test(settings_.library_dir, Glib::FILE_TEST_IS_DIR)) {
    auto root = Gio::File::create_for_path(settings_.library_dir);
    left_->set_ceiling(root);
    left_->navigate(root);
  } else {
    set_status("Set a library folder to begin.");
  }
  refresh_devices();
  show_all();
}

LibraryWindow::~LibraryWindow()
{
  progress_conn_.disconnect();
  done_conn_.disconnect();
  stop_copy();
}

void LibraryWindow::set_status(const Glib::ustring& text)
{
  status_.set_text(text);
}

void LibraryWindow::on_add_books()
{
  if (!left_->current()) {
    set_status("Set a library folder first.");
    return;
  }
  Gtk::FileChooserDialog dlg(*this, "Add to library", Gtk::FILE_CHOOSER_ACTION_OPEN);
  dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dlg.add_button("_Add", Gtk::RESPONSE_ACCEPT);
  dlg.set_select_multiple(true);
  auto filter = Gtk::FileFilter::create();
  filter->set_name("Books");
  filter->add_pattern("*.epub");
  filter->add_pattern("*.pdf");
  filter->add_pattern("*.mobi");
  filter->add_pattern("*.azw");
  filter->add_pattern("*.azw3");
  filter->add_pattern("*.fb2");
  dlg.add_filter(filter);
  if (dlg.run() != Gtk::RESPONSE_ACCEPT)
    return;
  const std::string group = sanitize_group(left_->selected_group());
  const std::string dest_dir = group.empty()
                                   ? left_->current()->get_path()
                                   : Glib::build_filename(left_->current()->get_path(), group);
  g_mkdir_with_parents(dest_dir.c_str(), 0700);
  int n = 0;
  for (const auto& path : dlg.get_filenames()) {
    if (!is_ebook_name(Glib::path_get_basename(path)))
      continue;
    auto src = Gio::File::create_for_path(path);
    auto dest =
        Gio::File::create_for_path(Glib::build_filename(dest_dir, Glib::path_get_basename(path)));
    try {
      if (dest->query_exists()) {
        set_status("Already in library: " + Glib::path_get_basename(path));
        continue;
      }
      src->copy(dest, Gio::FILE_COPY_NONE);
      ++n;
    } catch (const Glib::Error& e) {
      set_status(e.what());
    }
  }
  left_->refresh();
  if (n > 0)
    set_status(Glib::ustring::compose("Added %1 book(s).", n));
}

void LibraryWindow::on_delete_books()
{
  auto files = left_->ticked();
  if (files.empty()) {
    set_status("Tick a book to delete.");
    return;
  }
  Gtk::MessageDialog ask(*this, "Delete the ticked books from the library?", false,
                         Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_OK_CANCEL);
  ask.set_secondary_text("Removes the files from the library folder.");
  if (ask.run() != Gtk::RESPONSE_OK)
    return;
  auto cat = LibraryCatalog::load(left_->current() ? left_->current()->get_path() : "");
  int n = 0;
  for (const auto& f : files) {
    const std::string path = f->get_path();
    try {
      const std::string lr = lastread_path(path);
      if (Glib::file_test(lr, Glib::FILE_TEST_EXISTS))
        Gio::File::create_for_path(lr)->remove();
      f->remove();
      ++n;
    } catch (const Glib::Error& e) {
      set_status(e.what());
    }
  }
  if (left_->current())
    cat.save(left_->current()->get_path());
  left_->refresh();
  set_status(Glib::ustring::compose("Deleted %1 book(s).", n));
}

void LibraryWindow::on_group()
{
  if (!left_->current()) {
    set_status("Set a library folder first.");
    return;
  }
  Gtk::Dialog dlg("Group", *this, true);
  dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dlg.add_button("_OK", Gtk::RESPONSE_ACCEPT);
  auto* box = dlg.get_content_area();
  box->set_border_width(10);
  box->set_spacing(8);
  auto* name = Gtk::manage(new Gtk::Entry());
  name->set_placeholder_text("New or existing group name");
  name->set_activates_default(true);
  auto* none = Gtk::manage(new Gtk::CheckButton("Uncategorized (library root)"));
  box->pack_start(*Gtk::manage(new Gtk::Label("Move ticked books into:", Gtk::ALIGN_START)),
                  Gtk::PACK_SHRINK);
  box->pack_start(*name, Gtk::PACK_SHRINK);
  box->pack_start(*none, Gtk::PACK_SHRINK);
  dlg.set_default_response(Gtk::RESPONSE_ACCEPT);
  dlg.show_all();
  if (dlg.run() != Gtk::RESPONSE_ACCEPT)
    return;
  std::string group;
  if (!none->get_active())
    group = sanitize_group(std::string(name->get_text()));
  const std::string root = left_->current()->get_path();
  auto cat = LibraryCatalog::load(root);
  if (!group.empty()) {
    g_mkdir_with_parents(Glib::build_filename(root, group).c_str(), 0700);
    if (std::find(cat.groups.begin(), cat.groups.end(), group) == cat.groups.end()) {
      cat.groups.push_back(group);
      std::sort(cat.groups.begin(), cat.groups.end());
    }
  }
  auto files = left_->ticked();
  auto rels = left_->ticked_rels();
  int n = 0;
  for (size_t i = 0; i < files.size(); ++i) {
    const std::string src_path = files[i]->get_path();
    const std::string base = Glib::path_get_basename(src_path);
    const std::string dest_dir = group.empty() ? root : Glib::build_filename(root, group);
    auto dest = Gio::File::create_for_path(Glib::build_filename(dest_dir, base));
    if (files[i]->equal(dest))
      continue;
    try {
      if (dest->query_exists()) {
        set_status("Name already used: " + base);
        continue;
      }
      files[i]->move(dest, Gio::FILE_COPY_NONE);
      const std::string old_rel = i < rels.size() ? rels[i] : base;
      const std::string new_rel = group.empty() ? base : (group + "/" + base);
      auto tags = cat.tags_for(old_rel);
      cat.tags_by_rel.erase(old_rel);
      if (!tags.empty())
        cat.set_tags(new_rel, tags);
      const std::string lr = lastread_path(src_path);
      if (Glib::file_test(lr, Glib::FILE_TEST_EXISTS)) {
        auto lr_dest = Gio::File::create_for_path(lastread_path(dest->get_path()));
        try {
          Gio::File::create_for_path(lr)->move(lr_dest, Gio::FILE_COPY_NONE);
        } catch (const Glib::Error&) {
        }
      }
      ++n;
    } catch (const Glib::Error& e) {
      set_status(e.what());
    }
  }
  cat.save(root);
  left_->refresh();
  if (n > 0)
    set_status(Glib::ustring::compose("Moved %1 book(s).", n));
  else if (group.empty() && files.empty())
    set_status("Group saved.");
  else if (!group.empty() && files.empty())
    set_status("Group “" + group + "” is ready.");
}

void LibraryWindow::on_tag()
{
  auto rels = left_->ticked_rels();
  if (rels.empty()) {
    set_status("Tick a book to tag.");
    return;
  }
  if (!left_->current())
    return;
  const std::string root = left_->current()->get_path();
  auto cat = LibraryCatalog::load(root);
  Gtk::Dialog dlg("Tags", *this, true);
  dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dlg.add_button("_OK", Gtk::RESPONSE_ACCEPT);
  auto* box = dlg.get_content_area();
  box->set_border_width(10);
  box->set_spacing(8);
  auto* entry = Gtk::manage(new Gtk::Entry());
  entry->set_placeholder_text("semicolon-separated tags");
  entry->set_activates_default(true);
  if (rels.size() == 1)
    entry->set_text(join_tags(cat.tags_for(rels.front())));
  box->pack_start(*Gtk::manage(new Gtk::Label("Tags for ticked books:", Gtk::ALIGN_START)),
                  Gtk::PACK_SHRINK);
  box->pack_start(*entry, Gtk::PACK_SHRINK);
  dlg.set_default_response(Gtk::RESPONSE_ACCEPT);
  dlg.show_all();
  if (dlg.run() != Gtk::RESPONSE_ACCEPT)
    return;
  const auto tags = split_tags(std::string(entry->get_text()));
  for (const auto& rel : rels)
    cat.set_tags(rel, tags);
  cat.save(root);
  left_->refresh();
  set_status("Tags saved.");
}

void LibraryWindow::on_open()
{
  std::vector<Glib::RefPtr<Gio::File>> sel = left_->ticked();
  auto right_sel = right_->ticked();
  sel.insert(sel.end(), right_sel.begin(), right_sel.end());
  Glib::RefPtr<Gio::File> book;
  for (const auto& f : sel) {
    if (!f)
      continue;
    const std::string name = f->get_basename();
    if (is_ebook_name(name)) {
      book = f;
      break;
    }
  }
  if (!book) {
    set_status("Tick a book to open.");
    return;
  }
  const std::string path = book->get_path();
  if (path.empty()) {
    set_status("That book has no local path.");
    return;
  }
  signal_open_book.emit(path);
}

void LibraryWindow::on_set_library()
{
  Gtk::FileChooserDialog dlg(*this, "Library folder", Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
  dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dlg.add_button("_Select", Gtk::RESPONSE_OK);
  if (!settings_.library_dir.empty())
    dlg.set_current_folder(settings_.library_dir);
  else {
    const std::string books = Glib::build_filename(Glib::get_home_dir(), "Books");
    dlg.set_current_folder(Glib::file_test(books, Glib::FILE_TEST_IS_DIR) ? books
                                                                          : Glib::get_home_dir());
  }
  if (dlg.run() != Gtk::RESPONSE_OK)
    return;
  settings_.library_dir = dlg.get_filename();
  settings_.save();
  auto root = Gio::File::create_for_path(settings_.library_dir);
  left_->set_ceiling(root);
  left_->navigate(root);
  set_status("Library folder saved.");
}

void LibraryWindow::refresh_devices()
{
  const Glib::ustring keep = right_->combo().get_active_id();
  devices_ = list_library_devices();
  ignore_device_ = true;
  right_->combo().remove_all();
  for (const auto& d : devices_)
    right_->combo().append(d.uri, d.label);
  ignore_device_ = false;
  if (devices_.empty()) {
    right_->set_ceiling({});
    right_->set_device_root({});
    right_->navigate({});
    right_->combo().set_active(-1);
    set_status("No USB or MTP device is mounted.");
    return;
  }
  Glib::ustring pick = keep;
  if (pick.empty() && !settings_.device_uri.empty())
    pick = settings_.device_uri;
  bool found = false;
  for (const auto& d : devices_) {
    if (d.uri == pick.raw()) {
      found = true;
      break;
    }
  }
  if (!found)
    pick = devices_.front().uri;
  ignore_device_ = true;
  right_->combo().set_active_id(pick);
  ignore_device_ = false;
  on_device_changed();
}

void LibraryWindow::on_device_changed()
{
  if (ignore_device_)
    return;
  const Glib::ustring id = right_->combo().get_active_id();
  if (id.empty()) {
    right_->set_ceiling({});
    right_->set_device_root({});
    right_->navigate({});
    return;
  }
  auto root = Gio::File::create_for_uri(id.raw());
  right_->set_ceiling(root);
  right_->set_device_root(root->get_path());
  auto start = root;
  if (!settings_.device_dir.empty() && settings_.device_uri == id.raw()) {
    auto last = Gio::File::create_for_uri(settings_.device_dir);
    if (last && last->query_exists() && (last->equal(root) || last->has_prefix(root)))
      start = last;
  }
  right_->navigate(start);
}

void LibraryWindow::persist_device_dir()
{
  const Glib::ustring id = right_->combo().get_active_id();
  auto cur = right_->current();
  if (id.empty() || !cur)
    return;
  settings_.device_uri = id.raw();
  settings_.device_dir = cur->get_uri();
  settings_.save();
}

void LibraryWindow::set_busy(bool on)
{
  busy_ = on;
  left_->set_list_sensitive(!on);
  right_->set_list_sensitive(!on);
  transfer_.set_sensitive(!on);
  open_btn_.set_sensitive(!on);
}

void LibraryWindow::on_transfer()
{
  if (busy_)
    return;
  const auto left_sel = left_->ticked();
  const auto right_sel = right_->ticked();
  if (!left_sel.empty() && !right_sel.empty()) {
    set_status("Tick books on one side only.");
    return;
  }
  if (left_sel.empty() && right_sel.empty()) {
    set_status("Tick books to copy.");
    return;
  }
  const bool to_device = !left_sel.empty();
  auto dest = to_device ? right_->current() : left_->current();
  const auto& srcs = to_device ? left_sel : right_sel;
  if (!dest) {
    set_status(to_device ? "No device folder." : "Set a library folder first.");
    return;
  }
  job_srcs_.clear();
  for (const auto& f : srcs)
    job_srcs_.push_back(f->get_uri());
  job_dest_ = dest->get_uri();
  job_src_is_library_ = to_device;
  job_src_root_ = to_device ? left_->current()->get_path() : right_->device_root();
  job_dest_root_ =
      to_device ? right_->device_root() : (left_->current() ? settings_.library_dir : "");
  if (!to_device && left_->current())
    job_dest_root_ = settings_.library_dir;
  result_text_.clear();
  progress_text_ = "Copying…";
  cancellable_ = Gio::Cancellable::create();
  set_busy(true);
  set_status(progress_text_);
  copy_thread_ = std::thread([this]() { run_copy(); });
}

void LibraryWindow::run_copy()
{
  int ok = 0;
  int skipped = 0;
  int failed = 0;
  bool cancelled = false;

  std::function<void(const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&)> copy_one;
  copy_one = [&](const Glib::RefPtr<Gio::File>& src, const Glib::RefPtr<Gio::File>& dest_dir) {
    if (!src || !dest_dir)
      return;
    if (cancellable_ && cancellable_->is_cancelled()) {
      cancelled = true;
      return;
    }
    const std::string name = src->get_basename();
    if (skip_name(name))
      return;
    auto dest = dest_dir->get_child(name);
    auto info = src->query_info(cancellable_, "standard::type,standard::name");
    if (!info)
      return;
    if (dir_type(info->get_file_type())) {
      if (!dest->query_exists(cancellable_))
        dest->make_directory(cancellable_);
      auto en = src->enumerate_children(cancellable_, "standard::name,standard::type");
      while (auto child = en->next_file(cancellable_)) {
        const std::string cn = child->get_name();
        if (skip_name(cn))
          continue;
        copy_one(src->get_child(cn), dest);
        if (cancelled)
          return;
      }
      return;
    }
    if (info->get_file_type() != Gio::FILE_TYPE_REGULAR)
      return;
    if (!is_ebook_name(name))
      return;

    const std::string src_path = src->get_path();
    const std::string dest_path = dest->get_path();
    const LastRead src_pos = resolve_lastread(src_path, job_src_is_library_ ? &settings_ : nullptr,
                                              job_src_is_library_ ? std::string() : job_src_root_);
    if (dest->query_exists(cancellable_)) {
      const LastRead dest_pos =
          resolve_lastread(dest_path, job_src_is_library_ ? nullptr : &settings_,
                           job_src_is_library_ ? job_dest_root_ : std::string());
      if (same_last_page(src_pos, dest_pos)) {
        ++skipped;
        std::lock_guard<std::mutex> lock(mu_);
        progress_text_ = "Skipped " + Glib::ustring(name) + " (same last page)";
        progress_.emit();
        return;
      }
    }
    {
      std::lock_guard<std::mutex> lock(mu_);
      progress_text_ = "Copying " + Glib::ustring(name);
    }
    progress_.emit();
    src->copy(dest, [](goffset, goffset) {}, cancellable_, Gio::FILE_COPY_OVERWRITE);
    if (src_pos.present)
      save_lastread(dest_path, src_pos);
    else {
      const std::string side = lastread_path(src_path);
      if (Glib::file_test(side, Glib::FILE_TEST_IS_REGULAR)) {
        Gio::File::create_for_path(side)->copy(Gio::File::create_for_path(lastread_path(dest_path)),
                                               Gio::FILE_COPY_OVERWRITE);
      }
    }
    ++ok;
  };

  try {
    auto dest = Gio::File::create_for_uri(job_dest_);
    for (const auto& uri : job_srcs_) {
      if (cancellable_ && cancellable_->is_cancelled()) {
        cancelled = true;
        break;
      }
      try {
        copy_one(Gio::File::create_for_uri(uri), dest);
      } catch (const Glib::Error& e) {
        if (e.code() == Gio::Error::CANCELLED) {
          cancelled = true;
          break;
        }
        ++failed;
        std::lock_guard<std::mutex> lock(mu_);
        progress_text_ = e.what();
        progress_.emit();
      }
    }
  } catch (const Glib::Error& e) {
    if (e.code() == Gio::Error::CANCELLED)
      cancelled = true;
    else
      ++failed;
  }

  {
    std::lock_guard<std::mutex> lock(mu_);
    if (cancelled)
      result_text_ = "Transfer cancelled.";
    else if (failed == 0 && skipped == 0)
      result_text_ = Glib::ustring::compose("Copied %1 item(s).", ok);
    else if (failed == 0)
      result_text_ = Glib::ustring::compose("Copied %1, skipped %2 (same last page).", ok, skipped);
    else
      result_text_ =
          Glib::ustring::compose("Copied %1, skipped %2, %3 failed.", ok, skipped, failed);
  }
  done_.emit();
}

void LibraryWindow::on_copy_progress()
{
  std::lock_guard<std::mutex> lock(mu_);
  if (!progress_text_.empty())
    set_status(progress_text_);
}

void LibraryWindow::on_copy_done()
{
  if (copy_thread_.joinable())
    copy_thread_.join();
  set_busy(false);
  Glib::ustring text;
  {
    std::lock_guard<std::mutex> lock(mu_);
    text = result_text_;
  }
  left_->refresh();
  right_->refresh();
  left_->clear_ticks();
  right_->clear_ticks();
  set_status(text);
}

void LibraryWindow::stop_copy()
{
  if (cancellable_)
    cancellable_->cancel();
  if (copy_thread_.joinable())
    copy_thread_.join();
  busy_ = false;
}

void LibraryWindow::on_close()
{
  hide();
}

bool LibraryWindow::on_key_press(GdkEventKey* event)
{
  if (event && event->keyval == GDK_KEY_Escape) {
    hide();
    return true;
  }
  return false;
}

}  // namespace readomatic
