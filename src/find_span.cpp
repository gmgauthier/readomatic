/* SPDX-License-Identifier: Unlicense */

#include "book.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace readomatic {
namespace {

std::string ascii_lower(std::string s)
{
  for (char& c : s) {
    if (c >= 'a' && c <= 'z')
      continue;
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

bool nbsp_at(const std::string& s, size_t i, size_t end)
{
  return i + 1 < end && static_cast<unsigned char>(s[i]) == 0xC2 &&
         static_cast<unsigned char>(s[i + 1]) == 0xA0;
}

size_t utf8_width(const std::string& s, size_t i, size_t end)
{
  if (i >= end)
    return 1;
  const unsigned char c = static_cast<unsigned char>(s[i]);
  size_t n = 1;
  if ((c & 0xE0) == 0xC0)
    n = 2;
  else if ((c & 0xF0) == 0xE0)
    n = 3;
  else if ((c & 0xF8) == 0xF0)
    n = 4;
  if (i + n > end)
    return 1;
  return n;
}

struct RawSpan {
  size_t begin = 0;
  size_t end = 0;
};

// Same collapse as squeeze_ws in book.cpp, keeping the raw byte span of each output byte.
void squeeze_line_mapped(const std::string& raw, size_t begin, size_t end, std::string& out,
                         std::vector<RawSpan>& map)
{
  struct Unit {
    bool space = false;
    size_t begin = 0;
    size_t end = 0;
  };
  std::vector<Unit> units;
  for (size_t i = begin; i < end;) {
    if (nbsp_at(raw, i, end)) {
      units.push_back({true, i, i + 2});
      i += 2;
      continue;
    }
    const unsigned char c = static_cast<unsigned char>(raw[i]);
    if (std::isspace(c)) {
      units.push_back({true, i, i + 1});
      ++i;
      continue;
    }
    const size_t n = utf8_width(raw, i, end);
    units.push_back({false, i, i + n});
    i += n;
  }
  bool skipping = true;
  for (size_t u = 0; u < units.size(); ++u) {
    if (units[u].space) {
      if (skipping)
        continue;
      bool rest_space = true;
      for (size_t v = u + 1; v < units.size(); ++v) {
        if (!units[v].space)
          rest_space = false;
      }
      if (rest_space)
        break;
      out.push_back(' ');
      map.push_back({units[u].begin, units[u].end});
      skipping = true;
      continue;
    }
    for (size_t b = units[u].begin; b < units[u].end; ++b) {
      out.push_back(raw[b]);
      map.push_back({units[u].begin, units[u].end});
    }
    skipping = false;
  }
}

void squeeze_shown(const std::string& raw, std::string& out, std::vector<RawSpan>& map)
{
  out.clear();
  map.clear();
  size_t line = 0;
  size_t pending_break = std::string::npos;
  auto flush = [&](size_t line_end) {
    if (!out.empty()) {
      out.push_back('\n');
      const size_t at = pending_break == std::string::npos ? line_end : pending_break;
      map.push_back({at, at + 1});
    }
    squeeze_line_mapped(raw, line, line_end, out, map);
  };
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\n') {
      flush(i);
      pending_break = i;
      line = i + 1;
    }
  }
  flush(raw.size());
}

}  // namespace

bool find_squeezed_occurrence(const std::string& shown, const std::string& query, int occurrence,
                              size_t& begin, size_t& end)
{
  begin = 0;
  end = 0;
  if (query.empty() || occurrence < 0)
    return false;
  std::string squeezed;
  std::vector<RawSpan> map;
  squeeze_shown(shown, squeezed, map);
  const std::string needle = ascii_lower(query);
  const std::string hay = ascii_lower(squeezed);
  if (needle.empty() || map.size() != hay.size())
    return false;
  size_t pos = 0;
  int occ = 0;
  while (pos <= hay.size()) {
    pos = hay.find(needle, pos);
    if (pos == std::string::npos || pos + needle.size() > map.size())
      return false;
    if (occ == occurrence) {
      begin = map[pos].begin;
      end = map[pos + needle.size() - 1].end;
      return end > begin;
    }
    ++occ;
    pos += needle.size();
  }
  return false;
}

}  // namespace readomatic
