#include "paper/document.hpp"
#include "paper/rich_reader.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <dlfcn.h>
#include <cerrno>
#include <sys/stat.h>
// Model FAT's no-replace rename on the host. All test writes target a fresh fixture.
extern "C" int rename(const char *from,const char *to) noexcept(noexcept(::rename(from,to))) {
  struct stat st{};
  if(::lstat(to,&st)==0){errno=EEXIST;return -1;}
  using Rename=int(*)(const char*,const char*);
  static auto real=reinterpret_cast<Rename>(dlsym(RTLD_NEXT,"rename"));
  assert(real);return real(from,to);
}
using namespace paper;
namespace fs = std::filesystem;
struct Font : FontProvider {
  size_t reads = 0;
  Status glyph(uint32_t cp, const FontSpec &s, Glyph &g) override {
    ++reads;
    int w = cp < 128 ? std::max(1, s.px / 2) : s.px;
    g = {w * 64, 3, 6, 0, 6, {255, 255, 255, 255, 192}};
    return {};
  }
  std::string identity(const FontSpec &s) const override {
    return "fixture-sha-" + std::to_string(s.px);
  }
};
struct Source : DocumentSource {
  std::string html;
  size_t reads = 0;
  explicit Source(std::string h) : html(std::move(h)) {}
  bool hasRichContent() const override { return true; }
  std::string chapterHref(size_t n) const override {
    return n == 0 ? "OPS/c.xhtml" : "";
  }
  Status open(const std::string &, Metadata &m) override {
    m.identity = sha256(html.data(), html.size());
    m.engine = "paper-local-1";
    m.chapters = 1;
    m.toc = {{"start", 0, "start"}, {"end", 0, "end"}};
    return {};
  }
  Status chapter(size_t, Chapter &c) override {
    return markupToChapter(html, "OPS/c.xhtml", c, 768 * 1024);
  }
  Status chapterMarkup(size_t, std::string &h, std::string &css,
                       std::string &href) override {
    ++reads;
    h = html;
    css = "p{ text-indent:2em;} h1{text-align:center;}";
    href = "OPS/c.xhtml";
    return {};
  }
  Status resource(const std::string &p, std::vector<uint8_t> &v,
                  size_t) override {
    if (p != "OPS/c.xhtml")
      return Status::fail(Error::NotFound, "fixture");
    v.assign(html.begin(), html.end());
    return {};
  }
  void close() override {}
};
const std::vector<uint8_t> png = {
    137, 80,  78,  71, 13, 10, 26, 10,  0,  0,  0,  13,  73,  72,  68,
    82,  0,   0,   0,  2,  0,  0,  0,   2,  8,  6,  0,   0,   0,   114,
    182, 13,  36,  0,  0,  0,  18, 73,  68, 65, 84, 120, 156, 99,  96,
    96,  96,  248, 15, 2,  12, 80, 240, 31, 0,  60, 213, 5,   251, 109,
    11,  168, 27,  0,  0,  0,  0,  73,  69, 78, 68, 174, 66,  96,  130};
struct ImageSource : Source {
  bool broken = false;
  ImageSource(std::string h, bool b = false)
      : Source(std::move(h)), broken(b) {}
  Status resource(const std::string &p, std::vector<uint8_t> &v,
                  size_t cap) override {
    if (p == "OPS/a.png") {
      v = png;
      if (broken)
        v.resize(33);
      return {};
    }
    return Source::resource(p, v, cap);
  }
};
void ok(Status st) {
  if (!st) {
    std::cerr << errorName(st.code) << ": " << st.message << "\n";
    std::abort();
  }
}
int main(int argc, char **argv) {
  assert(argc == 2);
  assert(!fs::exists(argv[1]));
  fs::create_directories(argv[1]);
  ResourceGate gate;
  Store store(argv[1], gate);
  ok(store.initialize());
  Font font;
  std::string html =
      "<html><body><h1 id='start'>中文标题</h1><p>混合 English "
      "<strong>bold</strong> <em>italic</em> <ruby>汉<rt>han</rt></ruby> "
      "字。</p><table><tr><td>左列</td><td>右列</td></tr></table>";
  for (int n = 0; n < 160; ++n)
    html += "<p>第" + std::to_string(n) +
            "段。清晨，窗边的光一点点移到桌上。软件应该快速响应，图片和中文都清"
            "楚。English words remain readable.</p>";
  html += "<p id='end'>终点。</p></body></html>";
  auto source = std::make_unique<Source>(html);
  auto *raw = source.get();
  Reader reader(store, font, std::move(source));
  ok(reader.open("fixture"));
  assert(reader.metadata().engine == "paper-crossmux-1");
  assert(reader.location().pageHint == 0);
  Canvas canvas;
  ok(reader.paint(canvas, {24, 100, 432, 560}));
  assert(canvas.checksum() != Canvas().checksum());
  ok(reader.next());
  auto second = reader.location();
  assert(second.pageHint == 1);
  ok(reader.bookmark());
  ok(reader.next());
  ok(reader.previous());
  assert(reader.location().pageHint == second.pageHint);
  ok(reader.applyStyle({{30, 400, false}, 24, 44}, ReaderStyleScope::Book));
  assert(reader.location().offset <= second.offset);
  ok(reader.go(reader.bookmarks().front().location));
  assert(reader.location().offset <= second.offset);
  ok(reader.jump(1));
  assert(reader.chapter().text.find("终点") != std::string::npos);
  std::vector<Locator> matches;
  ok(reader.find("清晨", matches, 3));
  assert(matches.size() == 3);
  auto saved = reader.location();
  ok(reader.close());
  auto before = raw->reads;
  ok(reader.open("fixture"));
  assert(reader.location().offset == saved.offset);
  assert(raw->reads == before);
  assert(reader.contentDiagnostics().find("\"cache_hits\":1") !=
         std::string::npos);
  ok(reader.close());
  // Corrupted section body must never deserialize; rebuild only derived cache.
  bool changed = false;
  for (const auto &e : fs::recursive_directory_iterator(
           fs::path(argv[1]) / ".paper/cache/crossmux-1"))
    if (e.path().extension() == ".bin") {
      std::fstream f(e.path(), std::ios::binary | std::ios::in | std::ios::out);
      f.seekp(55);
      f.put(char(0xff));
      changed = true;
    }
  assert(changed);
  ok(reader.open("fixture"));
  ok(reader.paint(canvas, {24, 100, 432, 560}));
  ok(reader.close());
  // Legacy book records remain intact when switching engine.
  ok(reader.setRichEnabled(false));
  ok(reader.open("fixture"));
  ok(reader.next());
  auto legacy = reader.location();
  ok(reader.bookmark());
  ok(reader.close());
  std::string legacyBytes;
  ok(store.loadRecord("book-" + legacy.identity, legacyBytes));
  ok(reader.setRichEnabled(true));
  ok(reader.open("fixture"));
  ok(reader.close());
  std::string unchanged;
  ok(store.loadRecord("book-" + legacy.identity, unchanged));
  assert(legacyBytes == unchanged);
  // Cancel while parsing: no published partial is trusted without its seal.
  Reader canceled(store, font, std::make_unique<Source>(html + " "));
  canceled.setDocumentProgressObserver([](const char *stage, size_t, size_t) {
    return std::string(stage) == "parse"
               ? Status::fail(Error::Canceled, "fixture cancel")
               : Status{};
  });
  assert(canceled.open("fixture").code == Error::Canceled);
  canceled.setDocumentProgressObserver({});
  ok(canceled.open("fixture"));
  ok(canceled.close());
  Reader entity(store, font,
                std::make_unique<Source>(
                    "<!DOCTYPE html [<!ENTITY x "
                    "'expanded'>]><html><body><p>&x;</p></body></html>"));
  assert(!entity.open("fixture"));
  // Search must survive line/page wrapping and markup boundaries.
  std::string unit = "天地玄黄宇宙洪荒日月盈昃辰宿列张寒来暑往", body;
  for (int n = 0; n < 100; ++n)
    body += unit;
  Reader searched(store, font,
                  std::make_unique<Source>("<html><body><p>" + body +
                                           "</p></body></html>"));
  ok(searched.open("fixture"));
  for (int n = 0; n < 4; ++n)
    ok(searched.next());
  Canvas preview;
  ok(searched.previewStyle(searched.style(), preview, {24, 376, 432, 144}));
  assert(preview.checksum() != Canvas().checksum());
  auto original = searched.location();
  ok(searched.find(unit + unit, matches, 8));
  assert(matches.size() == 8);
  assert(searched.location().renderKey == original.renderKey &&
         searched.location().pageHint == original.pageHint);
  // A failed persistent style commit restores the actual renderer, not just its
  // public settings.
  fs::create_directory(fs::path(argv[1]) /
                       ".paper/records/reader-styles-v1.b.paper-tmp");
  fs::create_directory(fs::path(argv[1]) /
                       ".paper/records/reader-styles-v1.a.paper-tmp");
  auto oldStyle = searched.style();
  auto failed =
      searched.applyStyle({{34, 400, false}, 24, 48}, ReaderStyleScope::Book);
  assert(!failed && searched.style().font == oldStyle.font &&
         searched.location().renderKey == original.renderKey &&
         searched.location().pageHint == original.pageHint);
  fs::remove(fs::path(argv[1]) / ".paper/records/reader-styles-v1.b.paper-tmp");
  fs::remove(fs::path(argv[1]) / ".paper/records/reader-styles-v1.a.paper-tmp");
  ok(searched.close());
  Reader linked(
      store, font,
      std::make_unique<Source>(
          "<html><body><p><a href='#end'>跳到末尾</a></p>" + html.substr(12)));
  ok(linked.open("fixture"));
  auto links = linked.links({24, 100, 432, 560});
  assert(!links.empty());
  auto start = linked.location();
  ok(linked.follow("#end"));
  assert(linked.hasLinkReturn());
  ok(linked.returnLink());
  assert(linked.location().pageHint == start.pageHint);
  ok(linked.close());
  // Actual image blocks (not alt-text placeholders) and malformed-image
  // recovery.
  for (bool broken : {false, true}) {
    std::string ih = "<html><body><img src='a.png'/>";
    for (int n = 0; n < 60; ++n)
      ih += "<p>图后文字持续可读。</p>";
    ih += "</body></html>";
    Reader imageReader(
        store, font,
        std::make_unique<ImageSource>(ih + (broken ? " " : ""), broken));
    ok(imageReader.open("fixture"));
    assert(imageReader.contentDiagnostics().find("\"image_page\":true") !=
           std::string::npos);
    Canvas picture;
    ok(imageReader.paint(picture, {24, 100, 432, 560}));
    assert(picture.checksum() != Canvas().checksum());
    auto diag = imageReader.contentDiagnostics();
    assert((diag.find("\"image_error\":\"\"") != std::string::npos) == !broken);
    size_t turns = 0;
    for (;;) {
      auto st = imageReader.next();
      if (!st) {
        assert(st.code == Error::NotFound);
        break;
      }
      assert(++turns < 100);
    }
    assert(turns > 2);
    ok(imageReader.close());
  }
  Reader missingImage(
      store, font,
      std::make_unique<Source>("<html><body><p>图前</p><img src='missing.png' "
                               "alt='缺图'/><p>图后</p></body></html>"));
  ok(missingImage.open("fixture"));
  ok(missingImage.close());
  Reader cancelRead(store, font, std::make_unique<ImageSource>(
      "<html><body><img src='a.png'/><p>" + body + "</p></body></html>"));
  ok(cancelRead.open("fixture"));
  cancelRead.setDocumentProgressObserver([](const char *stage,size_t,size_t){
    return std::string(stage)=="parse"?Status::fail(Error::Canceled,"cancel cached read"):Status{};
  });
  assert(cancelRead.next().code==Error::Canceled);
  cancelRead.setDocumentProgressObserver({});
  ok(cancelRead.next());ok(cancelRead.close());
  std::cout
      << "PASS: cross-line search, failed-style rollback, preview after page "
         "4, links/return, real PNG, malformed/missing image isolation\n";
  std::cout << "PASS: actual CrossMux Section/Page, CJK/mixed "
               "styles/ruby/table, pagination, visible locators, font "
               "repagination, TOC, search, restart cache hit, corrupt rebuild, "
               "preserved legacy records, cancellation and DTD rejection\n";
}
