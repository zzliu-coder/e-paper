#include "paper/document.hpp"
#include <Epub/VisibleTextUtils.h>
#include <Epub/htmlEntities.h>
#include <Utf8.h>
#include <cstring>
#include <deque>
#include <expat.h>
namespace paper {
// Uses the same visible-codepoint counter as the upstream chapter parser.
// Search never depends on line/page wrapping, justification or inserted
// hyphens.
Status crossmuxSearchHtml(const std::string &html, const std::string &query,
                          size_t limit, std::vector<size_t> &offsets,
                          DocumentProgress progress) {
  struct Scan {
    XML_Parser xml = nullptr;
    bool body = false, bad = false, space = false;
    unsigned hidden = 0, depth = 0;
    size_t visible = 0, limit = 0;
    std::string window;
    std::deque<std::pair<size_t, size_t>>
        points; // UTF-8 byte length, original visible offset
    std::string query;
    std::vector<size_t> *out;
    void data(const char *s, int length) {
      if (!body || hidden || bad)
        return;
      const auto *p = reinterpret_cast<const unsigned char *>(s),
                 *end = p + length;
      while (p < end) {
        auto cp = utf8NextCodepoint(&p);
        auto at = visible++;
        const bool ws =
            cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t' || cp == 0xa0;
        if (ws && space)
          continue;
        space = ws;
        if (ws)
          cp = ' ';
        auto bytes = encodeUtf8(cp);
        window += bytes;
        points.push_back({bytes.size(), at});
        while (window.size() > query.size() && !points.empty()) {
          window.erase(0, points.front().first);
          points.pop_front();
        }
        if (window == query && !points.empty()) {
          out->push_back(points.front().second);
          if (out->size() >= limit) {
            XML_StopParser(xml, XML_FALSE);
            return;
          }
        }
      }
    }
  } scan;
  scan.limit = limit;
  scan.out = &offsets;
  offsets.clear();
  bool space = false;
  size_t p = 0;
  while (p < query.size()) {
    Rune r;
    auto st = readRune(query, p, r);
    if (!st)
      return st;
    bool ws = r.cp == ' ' || r.cp == '\n' || r.cp == '\r' || r.cp == '\t' ||
              r.cp == 0xa0;
    if (ws && space)
      continue;
    space = ws;
    scan.query += encodeUtf8(ws ? ' ' : r.cp);
  }
  if (scan.query.empty() || !limit)
    return {};
  scan.xml = XML_ParserCreate(nullptr);
  if (!scan.xml)
    return Status::fail(Error::Unavailable, "搜索解析器内存不足");
  XML_SetUserData(scan.xml, &scan);
  XML_SetElementHandler(
      scan.xml,
      [](void *v, const char *name, const char **) {
        auto &s = *static_cast<Scan *>(v);
        if (++s.depth > 128) {
          s.bad = true;
          XML_StopParser(s.xml, XML_FALSE);
          return;
        }
        if (VisibleTextUtils::equalsTag(name, "body"))
          s.body = true;
        if (s.body && (s.hidden || VisibleTextUtils::isNonVisibleElement(name)))
          ++s.hidden;
      },
      [](void *v, const char *name) {
        auto &s = *static_cast<Scan *>(v);
        if (s.hidden)
          --s.hidden;
        if (VisibleTextUtils::equalsTag(name, "body"))
          s.body = false;
        if (s.depth)
          --s.depth;
      });
  XML_SetCharacterDataHandler(scan.xml, [](void *v, const char *s, int n) {
    static_cast<Scan *>(v)->data(s, n);
  });
  XML_SetDefaultHandlerExpand(scan.xml, [](void *v, const char *s, int n) {
    if (n >= 3 && s[0] == '&' && s[n - 1] == ';') {
      auto value = lookupHtmlEntity(s, n);
      static_cast<Scan *>(v)->data(value ? value : s,
                                   value ? int(strlen(value)) : n);
    }
  });
  XML_SetStartDoctypeDeclHandler(
      scan.xml,
      [](void *v, const char *, const char *, const char *, int subset) {
        if (subset) {
          auto &s = *static_cast<Scan *>(v);
          s.bad = true;
          XML_StopParser(s.xml, XML_FALSE);
        }
      });
  XML_SetExternalEntityRefHandler(scan.xml,
                                  [](XML_Parser, const char *, const char *,
                                     const char *, const char *) { return 0; });
  Status st;
  for (size_t at = 0; at < html.size(); at += 1024) {
    if (progress) {
      st = progress("search", at, html.size());
      if (!st)
        break;
    }
    auto n = std::min<size_t>(1024, html.size() - at);
    auto result =
        XML_Parse(scan.xml, html.data() + at, int(n), at + n == html.size());
    if (offsets.size() >= limit)
      break;
    if (result == XML_STATUS_ERROR || scan.bad) {
      st = Status::fail(Error::Corrupt, "搜索章节格式无效");
      break;
    }
  }
  XML_ParserFree(scan.xml);
  return st;
}
} // namespace paper
