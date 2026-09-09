/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"
#include "about_dialog.hpp"
#include "paths.hpp"

#include <iostream>

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
  btn_contents_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_nav_page), 0));
  btn_index_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_nav_page), 1));
  btn_find_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_nav_page), 2));
  btn_back_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_not_yet),
                 Glib::ustring("Back")));
  btn_prev_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_not_yet),
                 Glib::ustring("Previous topic")));
  btn_next_.signal_clicked().connect(
      sigc::bind(sigc::mem_fun(*this, &MainWindow::on_not_yet),
                 Glib::ustring("Next topic")));
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
  contents_store_ = Gtk::TreeStore::create(rec);
  index_store_ = Gtk::ListStore::create(rec);
  find_store_ = Gtk::ListStore::create(rec);

  contents_view_.set_model(contents_store_);
  contents_view_.append_column("Contents", col_text_);
  contents_view_.set_headers_visible(false);
  contents_view_.get_style_context()->add_class("readomatic-nav");
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

  topic_view_.set_editable(false);
  topic_view_.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
  topic_view_.get_style_context()->add_class("readomatic-topic");
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
  if (dlg.run() != Gtk::RESPONSE_ACCEPT)
    return;
  set_status("Will open: " + dlg.get_filename());
}

void MainWindow::on_close_book()
{
  contents_store_->clear();
  index_store_->clear();
  find_store_->clear();
  topic_view_.get_buffer()->set_text(
      "Open an EPUB from File → Open…\n\n"
      "Contents, Index, and Find will list the book. This pane shows the topic.");
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
