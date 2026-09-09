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
    int occurrence = 0;  // nth match of the query in this document
  };

  Book() = default;
  ~Book() { close(); }
  Book(const Book&) = delete;
  Book& operator=(const Book&) = delete;

  bool open(const std::string& path);
  void close();
  bool is_open() const { return !extract_dir_.empty(); }

  const std::string& title() const { return title_; }
  const std::string& error() const { return error_; }
  const std::string& extract_dir() const { return extract_dir_; }
  const std::string& opf_dir() const { return opf_dir_; }

  int spine_count() const { return static_cast<int>(spine_.size()); }
  int spine_index() const { return spine_index_; }
  std::string spine_href(int i) const;
  bool set_spine_index(int i);
  std::string current_href() const;

  std::string load_document(const std::string& href) const;
  std::string resolve(const std::string& href) const; // absolute path under extract
  std::string start_href() const;                     // first readable spine item
  bool advance_spine(int delta);                      // skip cover wrappers
  std::string href_for_id(const std::string& id) const;
  const std::vector<NavNode>& nav() const { return nav_; }
  std::vector<IndexEntry> build_index() const;
  std::vector<SearchHit> search(const std::string& query, int limit = 200) const;

 private:
  bool extract_zip(const std::string& path);
  bool parse_container();
  bool parse_opf();
  bool parse_nav();
  void set_error(const std::string& msg);
  bool skip_spine_href(const std::string& href) const;

  std::string error_;
  std::string extract_dir_;
  std::string opf_path_;
  std::string opf_dir_;
  std::string title_;
  std::map<std::string, Item> manifest_;
  std::vector<std::string> spine_; // hrefs
  std::vector<NavNode> nav_;
  std::string nav_href_;
  std::string ncx_href_;
  int spine_index_ = 0;
};

}  // namespace readomatic
