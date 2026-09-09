/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"
#include "about_dialog.hpp"
#include "paths.hpp"
#include "config.hpp"

#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

namespace readomatic {
namespace {

Gtk::MenuItem* add_item(Gtk::Menu& menu, const Glib::ustring& label,
                        const sigc::slot<void()>& slot)
{
  auto* item = Gtk::manage(new Gtk::MenuItem(label, true));
  item->signal_activate().connect(slot);
  menu.append(*item);
  return item;
}

void paint_nav_cell(Gtk::CellRenderer* cell,
                    const Gtk::TreeModel::Path& path,
                    const Gtk::TreeModel::Path& current,
                    const Gtk::TreeModel::Path& hover)
{
  if (!cell)
    return;
  const bool on = (current.size() > 0 && path.size() > 0 && path == current) ||
                  (hover.size() > 0 && path.size() > 0 && path == hover);
  if (on) {
    cell->property_cell_background() = "#C4C4BC";
    cell->property_cell_background_set() = true;
  } else {
    cell->property_cell_background_set() = false;
  }
}

bool nav_motion(Gtk::TreeView& view, Gtk::TreeModel::Path& hover, GdkEventMotion* event)
{
  Gtk::TreeModel::Path path;
  Gtk::TreeViewColumn* col = nullptr;
  int cx = 0, cy = 0, bx = 0, by = 0;
  view.convert_widget_to_bin_window_coords(static_cast<int>(event->x),
                                           static_cast<int>(event->y), bx, by);
  if (view.get_path_at_pos(bx, by, path, col, cx, cy) && path.size() > 0) {
    if (hover.size() == 0 || hover != path) {
      hover = path;
      view.queue_draw();
    }
  } else if (hover.size() > 0) {
    hover.clear();
    view.queue_draw();
  }
  return false;
}

bool nav_leave(Gtk::TreeView& view, Gtk::TreeModel::Path& hover, GdkEventCrossing* event)
{
  if (event && event->detail == GDK_NOTIFY_INFERIOR)
    return false;
  if (hover.size() > 0) {
    hover.clear();
    view.queue_draw();
  }
  return false;
}

}  // namespace

MainWindow::MainWindow()
{
  settings_.load();
  set_title("Read-O-Matic");
  set_default_size(settings_.window_w > 0 ? settings_.window_w : 800,
                   settings_.window_h > 0 ? settings_.window_h : 560);
  set_border_width(0);
  get_style_context()->add_class("readomatic-window");

  load_css();
  build_menu();
  build_toolbar();
  build_body();

  if (settings_.window_w > 0 && settings_.window_h > 0)
    resize(settings_.window_w, settings_.window_h);
  if (settings_.window_x >= 0 && settings_.window_y >= 0)
    move(settings_.window_x, settings_.window_y);
  if (settings_.paned > 40)
    paned_.set_position(settings_.paned);

  status_ctx_ = status_.get_context_id("main");
  set_status("No book open.");
  rebuild_recent();
  rebuild_bookmarks();

  add(root_);
  show_all();
  signal_hide().connect(sigc::mem_fun(*this, &MainWindow::persist));
}

void MainWindow::load_css()
{
  const std::string css_path = find_data_file("skin/lcos/lcos.css");
  if (css_path.empty()) {
    std::cerr << "readomatic: lcos.css not found\n";
    return;
  }
  try {
    auto css = Gtk::CssProvider::create();
    css->load_from_path(css_path);
    Gtk::StyleContext::add_provider_for_screen(
        Gdk::Screen::get_default(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  } catch (const Glib::Error& e) {
    std::cerr << "readomatic: CSS: " << e.what() << "\n";
  }
}

void MainWindow::build_menu()
{
  auto add_menu = [this](const Glib::ustring& label, Gtk::Menu& menu) {
    auto* top = Gtk::manage(new Gtk::MenuItem(label, true));
    top->set_submenu(menu);
    menubar_.append(*top);
  };

  auto* file = Gtk::manage(new Gtk::Menu());
  add_item(*file, "_Open…", sigc::mem_fun(*this, &MainWindow::on_open));
  auto* recent_item = Gtk::manage(new Gtk::MenuItem("Open _Recent", true));
  recent_item->set_submenu(recent_menu_);
  file->append(*recent_item);
  add_item(*file, "_Close", sigc::mem_fun(*this, &MainWindow::on_close_book));
  file->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*file, "_Print…",
           sigc::bind(sigc::mem_fun(*this, &MainWindow::on_not_yet),
                      Glib::ustring("Print")));
  file->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*file, "E_xit", sigc::mem_fun(*this, &MainWindow::on_quit));
  add_menu("_File", *file);

  auto* edit = Gtk::manage(new Gtk::Menu());
  add_item(*edit, "_Copy",
           sigc::bind(sigc::mem_fun(*this, &MainWindow::on_not_yet),
                      Glib::ustring("Copy")));
  add_menu("_Edit", *edit);

  add_menu("_Bookmark", bookmark_menu_);

  auto* options = Gtk::manage(new Gtk::Menu());
  add_item(*options, "_Font…",
           sigc::bind(sigc::mem_fun(*this, &MainWindow::on_not_yet),
                      Glib::ustring("Font")));
  add_menu("_Options", *options);

  auto* help = Gtk::manage(new Gtk::Menu());
  add_item(*help, "_About Read-O-Matic", sigc::mem_fun(*this, &MainWindow::on_about));
  add_menu("_Help", *help);

  root_.pack_start(menubar_, Gtk::PACK_SHRINK);
}

void MainWindow::build_toolbar()
{
  toolbar_.set_border_width(4);
  auto set_btn_icon = [](Gtk::Button& btn, const char* file, const char* tip) {
    btn.set_tooltip_text(tip);
    const std::string path = find_data_file(std::string("skin/lcos/") + file);
    if (path.empty())
      return;
    try {
      auto pix = Gdk::Pixbuf::create_from_file(path, 16, 16);
      auto* img = Gtk::manage(new Gtk::Image(pix));
      btn.set_image(*img);
      btn.set_always_show_image(true);
      btn.set_label("");
    } catch (const Glib::Error&) {
    }
  };
  set_btn_icon(btn_back_, "btn-back.svg", "Back");
  set_btn_icon(btn_prev_, "btn-prev.svg", "Previous topic");
  set_btn_icon(btn_next_, "btn-next.svg", "Next topic");
  btn_contents_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_nav_page), 0));
  btn_index_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_nav_page), 1));
  btn_find_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_nav_page), 2));
  btn_back_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_back));
  btn_prev_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_spine_step), -1));
  btn_next_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_spine_step), 1));
  btn_print_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_not_yet),
                 Glib::ustring("Print")));

  toolbar_.pack_start(btn_contents_, Gtk::PACK_SHRINK);
  toolbar_.pack_start(btn_index_, Gtk::PACK_SHRINK);
  toolbar_.pack_start(btn_find_, Gtk::PACK_SHRINK);
  toolbar_.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL)),
                      Gtk::PACK_SHRINK);
  toolbar_.pack_start(btn_back_, Gtk::PACK_SHRINK);
  toolbar_.pack_start(btn_prev_, Gtk::PACK_SHRINK);
  toolbar_.pack_start(btn_next_, Gtk::PACK_SHRINK);
  toolbar_.pack_start(btn_print_, Gtk::PACK_SHRINK);
  btn_library_.set_tooltip_text("coming soon");
  btn_library_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_library));
  toolbar_.pack_end(btn_library_, Gtk::PACK_SHRINK);
  root_.pack_start(toolbar_, Gtk::PACK_SHRINK);
}

void MainWindow::style_list_column(Gtk::TreeView& view)
{
  view.set_hscroll_policy(Gtk::SCROLL_MINIMUM);
  if (auto* col = view.get_column(0)) {
    col->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    col->set_expand(true);
    const auto cells = col->get_cells();
    if (!cells.empty()) {
      if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cells[0])) {
        text->property_ellipsize() = Pango::ELLIPSIZE_END;
        text->property_xalign() = 0.0;
        text->property_xpad() = 6;
      }
    }
  }
}

void MainWindow::snap_nav_left(Gtk::TreeView& view, Gtk::ScrolledWindow& scroll)
{
  auto snap = [](const Glib::RefPtr<Gtk::Adjustment>& adj) {
    if (adj && adj->get_value() != adj->get_lower())
      adj->set_value(adj->get_lower());
  };
  snap(scroll.get_hadjustment());
  snap(view.get_hadjustment());
}

void MainWindow::keep_nav_left(Gtk::TreeView& view, Gtk::ScrolledWindow& scroll)
{
  auto* v = &view;
  auto* s = &scroll;
  auto hook = [this, v, s]() {
    snap_nav_left(*v, *s);
    auto attach = [this, v, s](const Glib::RefPtr<Gtk::Adjustment>& adj) {
      if (!adj)
        return;
      adj->signal_value_changed().connect([this, v, s]() {
        snap_nav_left(*v, *s);
      });
    };
    attach(s->get_hadjustment());
    attach(v->get_hadjustment());
  };
  if (view.get_realized())
    hook();
  view.signal_realize().connect(hook);
}

void MainWindow::scroll_nav_vertically(Gtk::TreeView& view, const Gtk::TreeModel::Path& path)
{
  auto* col = view.get_column(0);
  if (!col || path.empty())
    return;
  Gdk::Rectangle cell;
  view.get_background_area(path, *col, cell);
  auto v = view.get_vadjustment();
  if (!v)
    return;
  const double top = cell.get_y();
  const double bottom = top + cell.get_height();
  const double vis_top = v->get_value();
  const double vis_bot = vis_top + v->get_page_size();
  if (top < vis_top)
    v->set_value(top);
  else if (bottom > vis_bot)
    v->set_value(bottom - v->get_page_size());
}

void MainWindow::relayout_nav(Gtk::TreeView& view, Gtk::ScrolledWindow& scroll)
{
  view.queue_resize();
  scroll.queue_resize();
  snap_nav_left(view, scroll);
}

void MainWindow::build_body()
{
  Gtk::TreeModel::ColumnRecord rec;
  rec.add(col_text_);
  rec.add(col_href_);
  rec.add(col_occ_);
  contents_store_ = Gtk::TreeStore::create(rec);
  index_store_ = Gtk::ListStore::create(rec);
  find_store_ = Gtk::ListStore::create(rec);

  contents_view_.set_model(contents_store_);
  contents_view_.append_column("Contents", col_text_);
  contents_view_.set_headers_visible(false);
  contents_view_.get_selection()->set_mode(Gtk::SELECTION_NONE);
  contents_view_.set_can_focus(true);
  contents_view_.set_enable_search(false);
  contents_view_.get_style_context()->add_class("readomatic-nav");
  style_list_column(contents_view_);
  if (auto* col = contents_view_.get_column(0)) {
    const auto cells = col->get_cells();
    if (!cells.empty()) {
      if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cells[0])) {
        text->property_weight() = Pango::WEIGHT_BOLD;
        col->set_cell_data_func(*text, sigc::mem_fun(*this, &MainWindow::on_contents_cell_data));
      }
    }
  }
  contents_view_.add_events(Gdk::POINTER_MOTION_MASK | Gdk::LEAVE_NOTIFY_MASK);
  contents_view_.signal_motion_notify_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_contents_motion), false);
  contents_view_.signal_leave_notify_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_contents_leave), false);
  contents_view_.signal_key_press_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_contents_key), false);
  contents_view_.signal_row_activated().connect(
      sigc::mem_fun(*this, &MainWindow::on_contents_activated));
  contents_scroll_.add(contents_view_);
  contents_scroll_.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  contents_scroll_.set_margin_start(4);
  keep_nav_left(contents_view_, contents_scroll_);

  index_view_.set_model(index_store_);
  index_view_.append_column("Index", col_text_);
  index_view_.set_headers_visible(false);
  index_view_.set_show_expanders(false);
  index_view_.set_activate_on_single_click(true);
  index_view_.set_enable_search(true);
  index_view_.get_style_context()->add_class("readomatic-nav");
  style_list_column(index_view_);
  index_view_.signal_row_activated().connect(
      sigc::mem_fun(*this, &MainWindow::on_index_activated));
  index_scroll_.add(index_view_);
  index_scroll_.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  index_scroll_.set_margin_start(4);
  keep_nav_left(index_view_, index_scroll_);

  find_entry_.set_placeholder_text("Find in this book…");
  find_entry_.signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_find));
  auto* find_go = Gtk::manage(new Gtk::Button("Find"));
  find_go->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_find));
  find_go->set_valign(Gtk::ALIGN_START);

  find_view_.set_model(find_store_);
  find_view_.append_column("Find", col_text_);
  find_view_.set_headers_visible(false);
  find_view_.set_show_expanders(false);
  find_view_.get_selection()->set_mode(Gtk::SELECTION_NONE);
  find_view_.set_can_focus(true);
  find_view_.set_activate_on_single_click(true);
  find_view_.set_enable_search(false);
  find_view_.get_style_context()->add_class("readomatic-nav");
  style_list_column(find_view_);
  if (auto* col = find_view_.get_column(0)) {
    const auto cells = col->get_cells();
    if (!cells.empty()) {
      if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cells[0]))
        col->set_cell_data_func(*text, sigc::mem_fun(*this, &MainWindow::on_find_cell_data));
    }
  }
  find_view_.add_events(Gdk::POINTER_MOTION_MASK | Gdk::LEAVE_NOTIFY_MASK);
  find_view_.signal_motion_notify_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_find_motion), false);
  find_view_.signal_leave_notify_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_find_leave), false);
  find_view_.signal_key_press_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_find_key), false);
  find_view_.signal_row_activated().connect(
      sigc::mem_fun(*this, &MainWindow::on_find_activated));
  find_scroll_.add(find_view_);
  find_scroll_.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  keep_nav_left(find_view_, find_scroll_);

  auto* find_left = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
  find_left->pack_start(find_entry_, Gtk::PACK_SHRINK);
  find_left->pack_start(find_scroll_, Gtk::PACK_EXPAND_WIDGET);
  auto* find_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 4));
  find_row->pack_start(*find_left, Gtk::PACK_EXPAND_WIDGET);
  find_row->pack_start(*find_go, Gtk::PACK_SHRINK);
  find_box_.set_border_width(4);
  find_box_.pack_start(*find_row, Gtk::PACK_EXPAND_WIDGET);

  nav_.set_show_tabs(false);
  nav_.set_show_border(false);
  nav_.append_page(contents_scroll_, "Contents");
  nav_.append_page(index_scroll_, "Index");
  nav_.append_page(find_box_, "Find");
  nav_.set_size_request(220, -1);

  topic_view_.signal_jump().connect(sigc::mem_fun(*this, &MainWindow::on_jump));
  topic_view_.get_buffer()->set_text(
      "Open an EPUB from File → Open…\n\n"
      "Contents, Index, and Find will list the book. This pane shows the topic.");
  topic_scroll_.add(topic_view_);
  topic_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  topic_scroll_.set_shadow_type(Gtk::SHADOW_IN);

  paned_.pack1(nav_, false, true);
  paned_.pack2(topic_scroll_, true, true);
  paned_.set_position(220);

  root_.pack_start(paned_, Gtk::PACK_EXPAND_WIDGET);
  root_.pack_start(status_, Gtk::PACK_SHRINK);
}

void MainWindow::set_status(const Glib::ustring& text)
{
  status_.pop(status_ctx_);
  status_.push(text, status_ctx_);
}

void MainWindow::on_open()
{
  Gtk::FileChooserDialog dlg(*this, "Open EPUB", Gtk::FILE_CHOOSER_ACTION_OPEN);
  dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dlg.add_button("_Open", Gtk::RESPONSE_ACCEPT);
  auto filter = Gtk::FileFilter::create();
  filter->set_name("EPUB");
  filter->add_mime_type("application/epub+zip");
  filter->add_pattern("*.epub");
  dlg.add_filter(filter);
  dlg.set_current_folder(std::string(SOURCE_ROOT) + "/data/samples");
  if (dlg.run() != Gtk::RESPONSE_ACCEPT)
    return;
  open_path(dlg.get_filename());
}

void MainWindow::open_path(const std::string& path)
{
  persist_book();
  if (!Glib::file_test(path, Glib::FILE_TEST_EXISTS)) {
    set_status("File not found.");
    rebuild_recent();
    return;
  }
  if (!book_.open(path)) {
    set_status(book_.error().empty() ? "Could not open EPUB." : book_.error());
    rebuild_bookmarks();
    return;
  }
  set_title("Read-O-Matic — " + book_.title());
  history_.clear();
  find_store_->clear();
  find_current_path_.clear();
  find_hover_path_.clear();
  last_find_query_.clear();
  find_entry_.set_text("");
  settings_.touch_recent(book_.source_path(), book_.title());
  fill_contents();
  fill_index();
  const std::string key = book_key();
  const auto it = settings_.books.find(key);
  std::string restore_href;
  std::string restore_frag;
  double restore_scroll = 0;
  if (it != settings_.books.end()) {
    restore_href = it->second.href;
    restore_frag = it->second.fragment;
    restore_scroll = it->second.scroll;
  }
  if (!restore_href.empty() && book_.select_href(restore_href))
    show_current(restore_frag);
  else
    show_current();
  if (restore_frag.empty() && restore_scroll > 0) {
    const double scroll = restore_scroll;
    Glib::signal_idle().connect([this, scroll]() {
      set_topic_scroll(scroll);
      return false;
    });
  }
  rebuild_recent();
  rebuild_bookmarks();
  persist();
  Glib::signal_idle().connect(
      [this]() {
        snap_nav_left(contents_view_, contents_scroll_);
        contents_view_.queue_resize();
        return false;
      },
      Glib::PRIORITY_LOW);
}

void MainWindow::show_current(const std::string& fragment)
{
  if (!book_.is_open())
    return;
  const std::string href = book_.current_href();
  const std::string xhtml = book_.load_document(href);
  const std::string base = book_.resolve(href);
  const auto slash = base.find_last_of('/');
  const std::string dir = slash == std::string::npos ? book_.extract_dir() : base.substr(0, slash);
  topic_view_.load_xhtml(xhtml, dir);
  if (!fragment.empty())
    topic_view_.scroll_to_id(fragment);
  loaded_fragment_ = fragment;
  if (!suppress_history_)
    history_.push(href, fragment);
  highlight_contents();
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%s — %d of %d", book_.title().c_str(),
                book_.spine_index() + 1, book_.spine_count());
  set_status(buf);
}

void MainWindow::fill_contents()
{
  contents_current_path_.clear();
  contents_hover_path_.clear();
  contents_store_->clear();
  std::function<void(Gtk::TreeIter, const Book::NavNode&)> append_node;
  append_node = [&](Gtk::TreeIter parent, const Book::NavNode& node) {
    Gtk::TreeIter it;
    if (parent)
      it = contents_store_->append(parent->children());
    else
      it = contents_store_->append();
    (*it)[col_text_] = node.label;
    (*it)[col_href_] = node.href;
    for (const auto& ch : node.children)
      append_node(it, ch);
  };
  for (const auto& n : book_.nav())
    append_node(Gtk::TreeIter(), n);
  contents_view_.expand_all();
  relayout_nav(contents_view_, contents_scroll_);
}

void MainWindow::fill_index()
{
  index_store_->clear();
  for (const auto& e : book_.build_index()) {
    auto it = index_store_->append();
    (*it)[col_text_] = e.label;
    (*it)[col_href_] = e.href;
    (*it)[col_occ_] = 0;
  }
}

void MainWindow::on_contents_cell_data(Gtk::CellRenderer* cell,
                                       const Gtk::TreeModel::const_iterator& it)
{
  if (!it)
    return;
  paint_nav_cell(cell, contents_store_->get_path(it), contents_current_path_,
                 contents_hover_path_);
}

bool MainWindow::on_contents_motion(GdkEventMotion* event)
{
  return nav_motion(contents_view_, contents_hover_path_, event);
}

bool MainWindow::on_contents_leave(GdkEventCrossing* event)
{
  return nav_leave(contents_view_, contents_hover_path_, event);
}

void MainWindow::on_find_cell_data(Gtk::CellRenderer* cell,
                                   const Gtk::TreeModel::const_iterator& it)
{
  if (!it)
    return;
  paint_nav_cell(cell, find_store_->get_path(it), find_current_path_, find_hover_path_);
}

bool MainWindow::on_find_motion(GdkEventMotion* event)
{
  return nav_motion(find_view_, find_hover_path_, event);
}

bool MainWindow::on_find_leave(GdkEventCrossing* event)
{
  return nav_leave(find_view_, find_hover_path_, event);
}

void MainWindow::highlight_contents()
{
  if (!book_.is_open())
    return;
  const std::string cur_file = book_.resolve(book_.current_href());
  Gtk::TreeModel::Path exact;
  Gtk::TreeModel::Path file_only;
  std::function<void(const Gtk::TreeNodeChildren&)> walk;
  walk = [&](const Gtk::TreeNodeChildren& kids) {
    for (auto& row : kids) {
      const std::string h = row.get_value(col_href_);
      if (!h.empty()) {
        std::string file = h;
        std::string frag;
        const auto hash = h.find('#');
        if (hash != std::string::npos) {
          file = h.substr(0, hash);
          frag = h.substr(hash + 1);
        }
        if (book_.resolve(file) == cur_file) {
          auto path = contents_store_->get_path(row);
          if (file_only.size() == 0)
            file_only = path;
          if (!loaded_fragment_.empty() && frag == loaded_fragment_)
            exact = path;
        }
      }
      walk(row.children());
    }
  };
  walk(contents_store_->children());
  Gtk::TreeModel::Path chosen = exact.size() > 0 ? exact : file_only;
  if (chosen.size() == 0)
    return;
  contents_view_.expand_to_path(chosen);
  contents_current_path_ = chosen;
  scroll_nav_vertically(contents_view_, chosen);
  relayout_nav(contents_view_, contents_scroll_);
  Glib::signal_idle().connect(
      [this]() {
        snap_nav_left(contents_view_, contents_scroll_);
        contents_view_.queue_resize();
        return false;
      },
      Glib::PRIORITY_LOW);
  contents_view_.queue_draw();
}

double MainWindow::topic_scroll() const
{
  auto adj = topic_scroll_.get_vadjustment();
  return adj ? adj->get_value() : 0;
}

void MainWindow::set_topic_scroll(double value)
{
  auto adj = topic_scroll_.get_vadjustment();
  if (adj)
    adj->set_value(value);
}

void MainWindow::on_contents_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*)
{
  auto it = contents_store_->get_iter(path);
  if (!it)
    return;
  const Glib::ustring href = (*it)[col_href_];
  if (href.empty())
    return;
  history_.update_scroll(topic_scroll());
  on_jump(href);
}

void MainWindow::on_index_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*)
{
  auto it = index_store_->get_iter(path);
  if (!it)
    return;
  const Glib::ustring href = (*it)[col_href_];
  if (href.empty())
    return;
  history_.update_scroll(topic_scroll());
  on_jump(href);
}

void MainWindow::on_find()
{
  find_store_->clear();
  find_current_path_.clear();
  find_hover_path_.clear();
  last_find_query_.clear();
  if (!book_.is_open()) {
    set_status("No book open.");
    return;
  }
  const Glib::ustring q = find_entry_.get_text();
  if (q.size() < 2) {
    set_status("Type at least two letters.");
    return;
  }
  last_find_query_ = q;
  const auto hits = book_.search(std::string(q), 200);
  for (const auto& h : hits) {
    auto it = find_store_->append();
    (*it)[col_text_] = h.excerpt;
    (*it)[col_href_] = h.href;
    (*it)[col_occ_] = h.occurrence;
  }
  if (hits.empty()) {
    set_status("No matches.");
    return;
  }
  char buf[80];
  if (hits.size() >= 200)
    std::snprintf(buf, sizeof(buf), "200+ hits (showing first 200).");
  else
    std::snprintf(buf, sizeof(buf), "%zu hits.", hits.size());
  set_status(buf);
  relayout_nav(find_view_, find_scroll_);
}

void MainWindow::on_find_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*)
{
  auto it = find_store_->get_iter(path);
  if (!it)
    return;
  const Glib::ustring href = (*it)[col_href_];
  const int occ = (*it)[col_occ_];
  if (href.empty())
    return;
  find_current_path_ = path;
  snap_nav_left(find_view_, find_scroll_);
  find_view_.queue_draw();
  history_.update_scroll(topic_scroll());
  on_jump(href);
  const Glib::ustring q = last_find_query_;
  Glib::signal_idle().connect([this, q, occ]() {
    topic_view_.select_match(q, occ);
    return false;
  });
}

bool MainWindow::on_contents_key(GdkEventKey* event)
{
  if (!event)
    return false;
  if (event->keyval != GDK_KEY_Return && event->keyval != GDK_KEY_KP_Enter &&
      event->keyval != GDK_KEY_space)
    return false;
  Gtk::TreeModel::Path path;
  Gtk::TreeViewColumn* col = nullptr;
  contents_view_.get_cursor(path, col);
  if (path.size() == 0)
    return false;
  on_contents_activated(path, col);
  return true;
}

bool MainWindow::on_find_key(GdkEventKey* event)
{
  if (!event)
    return false;
  if (event->keyval != GDK_KEY_Return && event->keyval != GDK_KEY_KP_Enter &&
      event->keyval != GDK_KEY_space)
    return false;
  Gtk::TreeModel::Path path;
  Gtk::TreeViewColumn* col = nullptr;
  find_view_.get_cursor(path, col);
  if (path.size() == 0)
    return false;
  on_find_activated(path, col);
  return true;
}

void MainWindow::on_back()
{
  if (!book_.is_open())
    return;
  history_.update_scroll(topic_scroll());
  History::Entry e;
  if (!history_.back(e)) {
    set_status("No previous topic.");
    return;
  }
  suppress_history_ = true;
  Glib::ustring href = e.href;
  if (!e.fragment.empty())
    href += "#" + e.fragment;
  on_jump(href);
  suppress_history_ = false;
  const double scroll = e.scroll;
  Glib::signal_idle().connect([this, scroll]() {
    set_topic_scroll(scroll);
    return false;
  });
}

void MainWindow::on_spine_step(int delta)
{
  if (!book_.is_open())
    return;
  history_.update_scroll(topic_scroll());
  if (!book_.advance_spine(delta)) {
    set_status(delta > 0 ? "End of book." : "Start of book.");
    return;
  }
  show_current();
}

void MainWindow::on_jump(const Glib::ustring& href)
{
  if (!book_.is_open() || href.empty())
    return;
  std::string h(href);
  if (h.compare(0, 7, "http://") == 0 || h.compare(0, 8, "https://") == 0)
    return;
  std::string file = h;
  std::string frag;
  const auto hash = h.find('#');
  if (hash != std::string::npos) {
    file = h.substr(0, hash);
    frag = h.substr(hash + 1);
  }
  if (file.empty()) {
    if (topic_view_.scroll_to_id(frag))
      return;
    const std::string found = book_.href_for_id(frag);
    if (found.empty()) {
      set_status("Jump not in this book.");
      return;
    }
    for (int i = 0; i < book_.spine_count(); ++i) {
      if (book_.spine_href(i) == found) {
        book_.set_spine_index(i);
        show_current(frag);
        return;
      }
    }
    return;
  }
  const std::string path = book_.resolve(file);
  if (path.empty() || !Glib::file_test(path, Glib::FILE_TEST_IS_REGULAR)) {
    set_status("Jump not in this book.");
    return;
  }
  for (int i = 0; i < book_.spine_count(); ++i) {
    if (book_.resolve(book_.spine_href(i)) == path) {
      book_.set_spine_index(i);
      show_current(frag);
      return;
    }
  }
  const auto slash = path.find_last_of('/');
  const std::string dir = slash == std::string::npos ? book_.extract_dir() : path.substr(0, slash);
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  topic_view_.load_xhtml(ss.str(), dir);
  if (!frag.empty())
    topic_view_.scroll_to_id(frag);
  set_status(book_.title() + " — (jump)");
}

void MainWindow::on_close_book()
{
  persist_book();
  persist();
  book_.close();
  history_.clear();
  contents_store_->clear();
  index_store_->clear();
  find_store_->clear();
  find_current_path_.clear();
  find_hover_path_.clear();
  last_find_query_.clear();
  find_entry_.set_text("");
  topic_view_.clear_topic();
  topic_view_.get_buffer()->set_text(
      "Open an EPUB from File → Open…\n\n"
      "Contents, Index, and Find will list the book. This pane shows the topic.");
  set_title("Read-O-Matic");
  set_status("No book open.");
  rebuild_bookmarks();
}

void MainWindow::on_quit()
{
  persist();
  hide();
}

void MainWindow::on_about()
{
  AboutDialog dlg(*this);
  dlg.run();
}

void MainWindow::on_nav_page(int page)
{
  nav_.set_current_page(page);
  if (page == 2) {
    find_entry_.grab_focus();
    snap_nav_left(find_view_, find_scroll_);
  } else if (page == 0) {
    snap_nav_left(contents_view_, contents_scroll_);
  } else if (page == 1) {
    snap_nav_left(index_view_, index_scroll_);
  }
}

void MainWindow::on_not_yet(const Glib::ustring& feature)
{
  set_status(feature + " arrives after this stub.");
}

std::string MainWindow::book_key() const
{
  if (!book_.is_open())
    return {};
  const std::string id =
      book_.identifier().empty() ? book_.source_path() : book_.identifier();
  return Settings::key_for(id);
}

void MainWindow::persist_book()
{
  if (!book_.is_open())
    return;
  const std::string key = book_key();
  if (key.empty())
    return;
  auto& rec = settings_.book(key);
  rec.path = book_.source_path();
  rec.title = book_.title();
  rec.href = book_.current_href();
  rec.fragment = loaded_fragment_;
  rec.scroll = topic_scroll();
  settings_.touch_recent(rec.path, rec.title);
}

void MainWindow::persist()
{
  persist_book();
  int x = 0, y = 0, w = 0, h = 0;
  get_position(x, y);
  get_size(w, h);
  settings_.window_x = x;
  settings_.window_y = y;
  settings_.window_w = w;
  settings_.window_h = h;
  settings_.paned = paned_.get_position();
  settings_.save();
}

void MainWindow::rebuild_recent()
{
  for (auto* w : recent_menu_.get_children())
    recent_menu_.remove(*w);
  bool any = false;
  for (const auto& r : settings_.recent) {
    if (!Glib::file_test(r.path, Glib::FILE_TEST_EXISTS))
      continue;
    const Glib::ustring label = r.title.empty() ? Glib::path_get_basename(r.path) : r.title;
    auto* item = Gtk::manage(new Gtk::MenuItem(label));
    const std::string path = r.path;
    item->signal_activate().connect([this, path]() { open_path(path); });
    recent_menu_.append(*item);
    any = true;
  }
  if (!any) {
    auto* empty = Gtk::manage(new Gtk::MenuItem("(empty)"));
    empty->set_sensitive(false);
    recent_menu_.append(*empty);
  }
  recent_menu_.show_all();
}

void MainWindow::rebuild_bookmarks()
{
  for (auto* w : bookmark_menu_.get_children())
    bookmark_menu_.remove(*w);
  auto* def = add_item(bookmark_menu_, "_Define…",
                       sigc::mem_fun(*this, &MainWindow::on_define_bookmark));
  def->set_sensitive(book_.is_open());
  if (book_.is_open()) {
    const auto& rec = settings_.book(book_key());
    if (!rec.bookmarks.empty()) {
      bookmark_menu_.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
      for (const auto& m : rec.bookmarks) {
        auto* item = Gtk::manage(new Gtk::MenuItem(m.label));
        std::string jump = m.href;
        if (!m.fragment.empty())
          jump += "#" + m.fragment;
        item->signal_activate().connect([this, jump]() { on_jump(jump); });
        bookmark_menu_.append(*item);
      }
    }
  }
  bookmark_menu_.show_all();
}

void MainWindow::on_define_bookmark()
{
  if (!book_.is_open())
    return;
  Glib::ustring def = book_.title();
  if (contents_current_path_.size() > 0) {
    auto it = contents_store_->get_iter(contents_current_path_);
    if (it)
      def = (*it)[col_text_];
  }
  Gtk::Dialog dlg("Define Bookmark", *this, true);
  dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dlg.add_button("_OK", Gtk::RESPONSE_OK);
  dlg.set_default_response(Gtk::RESPONSE_OK);
  Gtk::Entry entry;
  entry.set_text(def);
  entry.set_activates_default(true);
  entry.set_hexpand(true);
  dlg.get_content_area()->set_border_width(8);
  dlg.get_content_area()->pack_start(entry, Gtk::PACK_SHRINK);
  dlg.show_all();
  if (dlg.run() != Gtk::RESPONSE_OK)
    return;
  const Glib::ustring label = entry.get_text();
  if (label.empty())
    return;
  Bookmark b;
  b.label = label;
  b.href = book_.current_href();
  b.fragment = loaded_fragment_;
  auto& rec = settings_.book(book_key());
  rec.path = book_.source_path();
  rec.title = book_.title();
  bool replaced = false;
  for (auto& existing : rec.bookmarks) {
    if (existing.href == b.href && existing.fragment == b.fragment) {
      existing.label = b.label;
      replaced = true;
      break;
    }
  }
  if (!replaced)
    rec.bookmarks.push_back(std::move(b));
  persist();
  rebuild_bookmarks();
  set_status("Bookmark saved.");
}

void MainWindow::on_library()
{
  btn_library_.trigger_tooltip_query();
  set_status("Library — coming soon.");
}

}  // namespace readomatic
