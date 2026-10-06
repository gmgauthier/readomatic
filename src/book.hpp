/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>
#include <vector>
#include <map>

namespace readomatic {

class Book {
 public:
  struct Item {
    std::string id;
    std::string href;
    std::string media_type;
    std::string properties;
  };

  struct NavNode {
    std::string label;
    std::string href;
    std::vector<NavNode> children;
  };

  struct IndexEntry {
    std::string label;
    std::string href;
  };

  struct SearchHit {
    std::string href;
    std::string excerpt;
    int occurrence = 0;  // nth match in this document; a paragraph break splits a phrase
  };

  Book() = default;
  ~Book()
  {
    close();
  }
  Book(const Book&) = delete;
  Book& operator=(const Book&) = delete;

  bool open(const std::string& path);
  void close();
  bool is_open() const
  {
    return !extract_dir_.empty();
  }

  const std::string& title() const
  {
    return title_;
  }
  const std::string& error() const
  {
    return error_;
  }
  const std::string& extract_dir() const
  {
    return extract_dir_;
  }
  const std::string& opf_dir() const
  {
    return opf_dir_;
  }
  const std::string& source_path() const
  {
    return source_path_;
  }
  const std::string& identifier() const
  {
    return identifier_;
  }

  int spine_count() const
  {
    return static_cast<int>(spine_.size());
  }
  int spine_index() const
  {
    return spine_index_;
  }
  std::string spine_href(int i) const;
  bool set_spine_index(int i);
  bool select_href(const std::string& href);
  bool follow_path(const std::string& path);  // spine item, or a file beside the chapter
  bool off_spine() const
  {
    return !off_spine_href_.empty();
  }
  std::string current_href() const;

  std::string load_document(const std::string& href) const;
  std::string resolve(const std::string& href) const;  // OPF-relative path under extract
  std::string resolve_against(const std::string& href, const std::string& base_dir) const;
  std::string resolve_content_link(const std::string& href) const;
  std::string start_href() const;                     // first readable spine item
  bool advance_spine(int delta);                      // skip cover wrappers
  void readable_span(int& number, int& total) const;  // 1-based, skips covers
  std::string href_for_id(const std::string& id) const;
  const std::vector<NavNode>& nav() const
  {
    return nav_;
  }
  std::vector<IndexEntry> build_index() const;
  std::vector<SearchHit> search(const std::string& query, int limit = 200) const;

 private:
  bool extract_zip(const std::string& path);
  bool extract_mobi(const std::string& path);
  bool parse_container();
  bool parse_opf();
  bool parse_nav();
  void set_error(const std::string& msg);
  bool fail_open();
  bool skip_spine_href(const std::string& href) const;

  std::string error_;
  std::string source_path_;
  std::string identifier_;
  std::string extract_dir_;
  std::string opf_path_;
  std::string opf_dir_;
  std::string title_;
  std::map<std::string, Item> manifest_;
  std::vector<std::string> spine_;  // hrefs
  std::vector<NavNode> nav_;
  std::string nav_href_;
  std::string ncx_href_;
  int spine_index_ = 0;
  std::string off_spine_href_;
};

/* page_index is zero-based, matching Book::spine_index(). The page numbers are never clipped. */
inline std::string topic_status_text(const std::string& title, int page_index, int page_count)
{
  return title + " — " + std::to_string(page_index + 1) + " of " + std::to_string(page_count);
}

inline std::string off_spine_status(const std::string& title, const std::string& href)
{
  return title + " — " + href;
}

// One pass. '+' stays '+'. '%2520' stays '%20'.
inline std::string percent_decode(const std::string& in)
{
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size()) {
      const int hi = hex(in[i + 1]);
      const int lo = hex(in[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(in[i]);
  }
  return out;
}

// Byte range in `shown` of one search hit. `shown` is topic text, with paragraph
// breaks as newlines. Spaces are squeezed the same way as Book::search.
bool find_squeezed_occurrence(const std::string& shown, const std::string& query, int occurrence,
                              size_t& begin, size_t& end);

}  // namespace readomatic
