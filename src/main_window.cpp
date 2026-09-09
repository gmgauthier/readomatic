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

}  // namespace

MainWindow::MainWindow()
{
  set_title("Read-O-Matic");
  set_default_size(800, 560);
  set_border_width(0);
  get_style_context()->add_class("readomatic-window");

  load_css();
  build_menu();
  build_toolbar();
  build_body();

  status_ctx_ = status_.get_context_id("main");
  set_status("No book open.");

  add(root_);
  show_all();
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

  auto* bookmark = Gtk::manage(new Gtk::Menu());
  add_item(*bookmark, "_Define…",
           sigc::bind(sigc::mem_fun(*this, &MainWindow::on_not_yet),
                      Glib::ustring("Bookmark")));
  add_menu("_Bookmark", *bookmark);

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
  root_.pack_start(toolbar_, Gtk::PACK_SHRINK);
}

void MainWindow::build_body()
{
  Gtk::TreeModel::ColumnRecord rec;
  rec.add(col_text_);
  rec.add(col_href_);
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
  contents_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

  index_view_.set_model(index_store_);
  index_view_.append_column("Index", col_text_);
  index_view_.set_headers_visible(false);
  index_view_.get_style_context()->add_class("readomatic-nav");
  index_scroll_.add(index_view_);
  index_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

  find_entry_.set_placeholder_text("Find in this book…");
  find_view_.set_model(find_store_);
  find_view_.append_column("Find", col_text_);
  find_view_.set_headers_visible(false);
  find_scroll_.add(find_view_);
  find_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  find_box_.set_border_width(4);
  find_box_.pack_start(find_entry_, Gtk::PACK_SHRINK);
  find_box_.pack_start(find_scroll_, Gtk::PACK_EXPAND_WIDGET);

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
  if (!book_.open(dlg.get_filename())) {
    set_status(book_.error().empty() ? "Could not open EPUB." : book_.error());
    return;
  }
  set_title("Read-O-Matic — " + book_.title());
  history_.clear();
  fill_contents();
  show_current();
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
}

void MainWindow::on_contents_cell_data(Gtk::CellRenderer* cell,
                                       const Gtk::TreeModel::const_iterator& it)
{
  if (!cell || !it)
    return;
  const auto path = contents_store_->get_path(it);
  const bool current = contents_current_path_.size() > 0 && path.size() > 0 &&
                       path == contents_current_path_;
  const bool hover = contents_hover_path_.size() > 0 && path.size() > 0 &&
                     path == contents_hover_path_;
  if (current || hover) {
    cell->property_cell_background() = "#C4C4BC";
    cell->property_cell_background_set() = true;
  } else {
    cell->property_cell_background_set() = false;
  }
}

bool MainWindow::on_contents_motion(GdkEventMotion* event)
{
  Gtk::TreeModel::Path path;
  Gtk::TreeViewColumn* col = nullptr;
  int cx = 0, cy = 0;
  int bx = 0, by = 0;
  contents_view_.convert_widget_to_bin_window_coords(static_cast<int>(event->x),
                                                     static_cast<int>(event->y), bx, by);
  if (contents_view_.get_path_at_pos(bx, by, path, col, cx, cy) && path.size() > 0) {
    if (contents_hover_path_.size() == 0 || contents_hover_path_ != path) {
      contents_hover_path_ = path;
      contents_view_.queue_draw();
    }
  } else if (contents_hover_path_.size() > 0) {
    contents_hover_path_.clear();
    contents_view_.queue_draw();
  }
  return false;
}

bool MainWindow::on_contents_leave(GdkEventCrossing* event)
{
  if (event && event->detail == GDK_NOTIFY_INFERIOR)
    return false;
  if (contents_hover_path_.size() > 0) {
    contents_hover_path_.clear();
    contents_view_.queue_draw();
  }
  return false;
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
  contents_view_.set_cursor(chosen);
  contents_view_.scroll_to_row(chosen);
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
  book_.close();
  history_.clear();
  contents_store_->clear();
  index_store_->clear();
  find_store_->clear();
  topic_view_.clear_topic();
  topic_view_.get_buffer()->set_text(
      "Open an EPUB from File → Open…\n\n"
      "Contents, Index, and Find will list the book. This pane shows the topic.");
  set_title("Read-O-Matic");
  set_status("No book open.");
}

void MainWindow::on_quit()
{
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
}

void MainWindow::on_not_yet(const Glib::ustring& feature)
{
  set_status(feature + " arrives after this stub.");
}

}  // namespace readomatic
