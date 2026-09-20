#pragma once
#include <string>
#include <vector>
namespace FsHelpers {
inline std::string decodeUriEscapes(const std::string &s) {
  std::string out;
  auto hex = [](char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
  };
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 &&
        hex(s[i + 2]) >= 0) {
      out += char(hex(s[i + 1]) * 16 + hex(s[i + 2]));
      i += 2;
    } else
      out += s[i];
  }
  return out;
}
inline std::string normalisePath(const std::string &s) {
  if (s.empty() || s[0] == '/' || s.find(':') != s.npos ||
      s.find('\\') != s.npos || s.find('\0') != s.npos)
    return {};
  std::vector<std::string> parts;
  size_t at = 0;
  while (at <= s.size()) {
    auto end = s.find('/', at);
    auto part = s.substr(at, end == s.npos ? s.size() - at : end - at);
    if (part == "..") {
      if (parts.empty())
        return {};
      parts.pop_back();
    } else if (!part.empty() && part != ".")
      parts.push_back(part);
    if (end == s.npos)
      break;
    at = end + 1;
  }
  std::string out;
  for (auto &p : parts) {
    if (!out.empty())
      out += '/';
    out += p;
  }
  return out;
}
} // namespace FsHelpers
