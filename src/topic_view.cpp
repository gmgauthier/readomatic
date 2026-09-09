/* SPDX-License-Identifier: Unlicense */

#include "topic_view.hpp"

#include <libxml/HTMLparser.h>
#include <glibmm.h>

#include <algorithm>
#include <cctype>
#include <functional>

namespace readomatic {
namespace {

std::string lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string xml_name(xmlNode* n)
{
  if (!n || !n->name)
    return {};
  return lower(reinterpret_cast<const char*>(n->name));
}

std::string xml_prop(xmlNode* n, const char* key)
{
  xmlChar* v = xmlGetProp(n, BAD_CAST key);
  if (!v)
    return {};
  std::string out(reinterpret_cast<const char*>(v));
  xmlFree(v);
  return out;
}

bool skip_tag(const std::string& name)
{
  return name == "script" || name == "style" || name == "svg" || name == "iframe" ||
         name == "head" || name == "meta" || name == "link" || name == "title";
}

}  // namespace

TopicView::TopicView()
{
  set_editable(false);
  set_wrap_mode(Gtk::WRAP_WORD_CHAR);
  set_cursor_visible(false);
  set_left_margin(16);
  set_right_margin(16);
  set_top_margin(12);
  set_bottom_margin(16);
  Gdk::RGBA bg;
  bg.set("#F7F5EF");
  override_background_color(bg);
  get_style_context()->add_class("readomatic-topic");
  buf_ = get_buffer();
  ensure_tags();
}

void TopicView::ensure_tags()
{
  auto table = buf_->get_tag_table();
  auto mk = [&](const char* name, auto fn) {
    if (table->lookup(name))
      return;
    auto tag = Gtk::TextBuffer::Tag::create(name);
    fn(tag);
    table->add(tag);
  };
  mk("h1", [](auto t) { t->property_weight() = Pango::WEIGHT_BOLD; t->property_scale() = 1.6; });
  mk("h2", [](auto t) { t->property_weight() = Pango::WEIGHT_BOLD; t->property_scale() = 1.35; });
  mk("h3", [](auto t) { t->property_weight() = Pango::WEIGHT_BOLD; t->property_scale() = 1.2; });
  mk("h4", [](auto t) { t->property_weight() = Pango::WEIGHT_BOLD; t->property_scale() = 1.1; });
  mk("em", [](auto t) { t->property_style() = Pango::STYLE_ITALIC; });
  mk("strong", [](auto t) { t->property_weight() = Pango::WEIGHT_BOLD; });
  mk("pre", [](auto t) {
    t->property_family() = "monospace";
    t->property_wrap_mode() = Gtk::WRAP_NONE;
  });
  mk("blockquote", [](auto t) { t->property_left_margin() = 24; t->property_style() = Pango::STYLE_ITALIC; });
  mk("link", [](auto t) {
    t->property_underline() = Pango::UNDERLINE_SINGLE;
    t->property_foreground() = "#0B3A96";
  });
}

void TopicView::clear_topic()
{
  buf_->set_text("");
  link_hrefs_.clear();
}

void TopicView::insert_text(const std::string& text, const std::vector<Glib::ustring>& tag_names)
{
  if (text.empty())
    return;
  const int start_off = buf_->get_char_count();
  buf_->insert(buf_->end(), Glib::ustring(text));
  if (tag_names.empty())
    return;
  auto table = buf_->get_tag_table();
  auto start = buf_->get_iter_at_offset(start_off);
  auto end = buf_->end();
  for (const auto& n : tag_names) {
    if (auto t = table->lookup(n))
      buf_->apply_tag(t, start, end);
  }
}

void TopicView::insert_break()
{
  auto end = buf_->end();
  buf_->insert(end, "\n");
}

std::string TopicView::resolve_src(const std::string& src, const std::string& base_dir) const
{
  std::string s = src;
  const auto hash = s.find('#');
  if (hash != std::string::npos)
    s = s.substr(0, hash);
  if (s.empty())
    return {};
  if (s[0] == '/' )
    return s;
  if (base_dir.empty())
    return s;
  return Glib::build_filename(base_dir, s);
}

void TopicView::walk(xmlNode* node, const std::string& base_dir, int list_depth)
{
  for (xmlNode* n = node; n; n = n->next) {
    if (n->type == XML_TEXT_NODE || n->type == XML_CDATA_SECTION_NODE) {
      if (n->content)
        insert_text(reinterpret_cast<const char*>(n->content), tag_stack_);
      continue;
    }
    if (n->type != XML_ELEMENT_NODE)
      continue;
    const std::string name = xml_name(n);
    if (skip_tag(name))
      continue;

    const std::string elem_id = xml_prop(n, "id");
    if (!elem_id.empty())
      buf_->create_mark(elem_id, buf_->end(), true);

    if (name == "br") {
      insert_break();
      continue;
    }
    if (name == "img") {
      const std::string path = resolve_src(xml_prop(n, "src"), base_dir);
      if (!path.empty()) {
        try {
          auto pix = Gdk::Pixbuf::create_from_file(path);
          if (pix->get_width() > 480)
            pix = pix->scale_simple(480, pix->get_height() * 480 / pix->get_width(),
                                    Gdk::INTERP_BILINEAR);
          auto end = buf_->end();
          buf_->insert_pixbuf(end, pix);
          insert_break();
        } catch (const Glib::Error&) {
        }
      }
      continue;
    }

    if (name == "p" || name == "div" || name == "blockquote" || name == "li" ||
        name == "h1" || name == "h2" || name == "h3" || name == "h4" || name == "h5" ||
        name == "h6" || name == "pre" || name == "tr") {
      if (buf_->get_char_count() > 0)
        insert_break();
    }

    std::string pushed;
    if (name == "h1" || name == "h2" || name == "h3" || name == "h4")
      pushed = name;
    else if (name == "h5" || name == "h6")
      pushed = "h4";
    else if (name == "em" || name == "i")
      pushed = "em";
    else if (name == "strong" || name == "b")
      pushed = "strong";
    else if (name == "pre" || name == "code")
      pushed = "pre";
    else if (name == "blockquote")
      pushed = "blockquote";

    Glib::RefPtr<Gtk::TextTag> link_tag;
    std::string href;
    if (name == "a") {
      href = xml_prop(n, "href");
      if (!href.empty()) {
        pushed = "link";
        auto table = buf_->get_tag_table();
        link_tag = table->lookup("link");
      }
    }

    if (name == "li") {
      insert_text(std::string(static_cast<size_t>(list_depth + 1) * 2, ' ') + "• ",
                  tag_stack_);
    }

    if (!pushed.empty())
      tag_stack_.push_back(pushed);
    const int child_list = (name == "ul" || name == "ol") ? list_depth + 1 : list_depth;
    const auto start_off = buf_->get_char_count();
    walk(n->children, base_dir, child_list);
    if (name == "a" && !href.empty()) {
      auto start = buf_->get_iter_at_offset(start_off);
      auto end = buf_->end();
      auto unique = Gtk::TextBuffer::Tag::create();
      unique->property_underline() = Pango::UNDERLINE_SINGLE;
      unique->property_foreground() = "#0B3A96";
      buf_->get_tag_table()->add(unique);
      buf_->apply_tag(unique, start, end);
      link_hrefs_[unique] = href;
    }
    if (!pushed.empty() && !tag_stack_.empty() && tag_stack_.back() == pushed)
      tag_stack_.pop_back();

    if (name == "p" || name == "div" || name == "blockquote" || name == "li" ||
        name == "h1" || name == "h2" || name == "h3" || name == "h4" || name == "h5" ||
        name == "h6" || name == "pre")
      insert_break();
  }
}

void TopicView::load_xhtml(const std::string& xhtml, const std::string& base_dir)
{
  clear_topic();
  tag_stack_.clear();
  if (xhtml.empty()) {
    buf_->set_text("(empty document)");
    return;
  }
  xmlDoc* doc = htmlReadMemory(xhtml.data(), static_cast<int>(xhtml.size()), "topic.xhtml",
                               "UTF-8",
                               HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING |
                                   HTML_PARSE_NONET | HTML_PARSE_NOBLANKS);
  if (!doc) {
    buf_->set_text(xhtml);
    return;
  }
  xmlNode* root = xmlDocGetRootElement(doc);
  xmlNode* body = root;
  for (xmlNode* n = root; n; n = n->next) {
    /* find body via walk of root */
  }
  std::function<xmlNode*(xmlNode*)> find_body = [&](xmlNode* n) -> xmlNode* {
    for (; n; n = n->next) {
      if (n->type == XML_ELEMENT_NODE && xml_name(n) == "body")
        return n;
      if (xmlNode* hit = find_body(n->children))
        return hit;
    }
    return nullptr;
  };
  if (xmlNode* b = find_body(root))
    body = b;
  walk(body ? body->children : root, base_dir, 0);
  xmlFreeDoc(doc);
}

bool TopicView::scroll_to_id(const std::string& id)
{
  if (id.empty())
    return false;
  auto mark = buf_->get_mark(id);
  if (!mark)
    return false;
  scroll_to(mark, 0.1, 0.0, 0.15);
  return true;
}

bool TopicView::on_button_release_event(GdkEventButton* event)
{
  if (event->button != 1)
    return Gtk::TextView::on_button_release_event(event);
  int x = 0, y = 0;
  window_to_buffer_coords(Gtk::TEXT_WINDOW_TEXT, static_cast<int>(event->x),
                          static_cast<int>(event->y), x, y);
  Gtk::TextIter iter;
  get_iter_at_location(iter, x, y);
  auto tags = iter.get_tags();
  for (const auto& tag : tags) {
    auto it = link_hrefs_.find(tag);
    if (it != link_hrefs_.end()) {
      signal_jump_.emit(it->second);
      return true;
    }
  }
  return Gtk::TextView::on_button_release_event(event);
}

}  // namespace readomatic
