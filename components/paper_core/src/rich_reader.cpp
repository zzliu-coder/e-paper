#include "paper/rich_reader.hpp"
#include "paper/image.hpp"
#include <BidiUtils.h>
#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <Utf8.h>
#include <cerrno>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <sys/stat.h>
namespace paper {
Status crossmuxSearchHtml(const std::string &, const std::string &, size_t,
                          std::vector<size_t> &, DocumentProgress);
struct RichReader::Impl {
  Store &store;
  FontProvider &fonts;
  DocumentSource &source;
  Metadata meta;
  FontSpec spec;
  int width = 432, height = 560, lineHeight = 40;
  std::string key, root, relative, chapterHref, pageText, warn;
  Locator loc;
  DocumentProgress observer;
  Status operation;
  PaperCacheIO io;
  GfxRenderer renderer;
  ReaderRenderSpec renderSpec;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<CssParser> css;
  std::unique_ptr<Section> section;
  std::unique_ptr<Page> page;
  size_t chapter = SIZE_MAX;
  bool ready = false;
  uint64_t cacheHits = 0, buildSteps = 0, loadedPages = 0;
  std::set<std::string> verified;
  std::string prepKey;
  size_t prepAt = 0;
  std::string prepText;
  Impl(Store &s, FontProvider &f, DocumentSource &d, const Metadata &m)
      : store(s), fonts(f), source(d), meta(m) {
    loc = {meta.identity, "paper-crossmux-1", 0, 0};
    io.allowed = [this](const std::string &p, bool missing) {
      if (root.empty() || p.compare(0, root.size() + 1, root + "/") != 0)
        return false;
      std::string abs;
      return bool(
                 store.path(p.substr(store.root().size() + 1), abs, missing)) &&
             abs == p;
    };
    io.progress = [this](size_t done, size_t total) {
      if (!operation)
        return false;
      if (observer)
        operation = observer("parse", done, total);
      return bool(operation);
    };
    renderer.measure = [this](const char *t, EpdFontFamily::Style style) {
      int width64 = 0;
      const unsigned char *p = reinterpret_cast<const unsigned char *>(t);
      while (*p) {
        auto cp = utf8NextCodepoint(&p);
        int a = 0;
        auto st = fonts.advance(cp, spec, a);
        if (!st) {
          operation = st;
          renderer.failed = true;
          return 0;
        }
        width64 += a;
      }
      if (style & (EpdFontFamily::SUP | EpdFontFamily::SUB))
        width64 = width64 * 2 / 3;
      return (width64 + 32) / 64;
    };
  }
  ~Impl() {
    PaperCacheScope scope(io);
    if (section) {
      section->suspendBuild();
      seal();
      section.reset();
    }
    ImageBlock::setExtractor(nullptr, nullptr);
  }
  Status fail(const char *s) {
    return !operation ? operation : Status::fail(Error::Corrupt, s);
  }
  std::string path(const std::string &suffix) const {
    return root + "/" + suffix;
  }
  std::string rel(const std::string &suffix) const {
    return relative + "/" + suffix;
  }
  bool mkdirPath(const std::string &r) {
    std::string abs;
    if (!store.path(r, abs, true))
      return false;
    return ::mkdir(abs.c_str(), 0755) == 0 || errno == EEXIST;
  }
  std::string sectionName() const {
    return "sections/" + std::to_string(chapter) + ".bin";
  }
  void seal() {
    if (!section || section->isBuilding() || chapter == SIZE_MAX || !operation)
      return;
    std::string hash;
    auto st = store.hash(rel(sectionName()), hash);
    if (!st)
      return;
    const auto data = hash + "\n";
    st = store.writeReaderCache(rel(sectionName() + ".sha"), data.data(),
                                data.size());
    if (st)
      verified.insert(path(sectionName()));
  }
  bool validFile(const std::string &suffix) {
    const auto abs = path(suffix);
    if (verified.count(abs))
      return true;
    std::vector<uint8_t> bytes;
    if (!store.read(rel(suffix + ".sha"), bytes, 65) || bytes.size() != 65)
      return false;
    std::string actual;
    auto st = store.hash(rel(suffix), actual, [this](size_t d, size_t t) {
      if (observer)
        return observer("cache_verify", d, t);
      return Status{};
    });
    if (!st) {
      if (st.code == Error::Canceled)
        operation = st;
      return false;
    }
    if (actual != std::string(bytes.begin(), bytes.begin() + 64))
      return false;
    verified.insert(abs);
    return true;
  }
  bool extract(const std::string &src, Print &out, bool early) {
    std::vector<uint8_t> bytes;
    auto st = source.resource(src, bytes, 1024 * 1024);
    if (!st) {
      if (st.code == Error::Canceled)
        operation = st;
      else
        warn = st.message;
      return false;
    }
    for (size_t at = 0; at < bytes.size(); at += 8192) {
      if (observer) {
        operation = observer("resource_read", at, bytes.size());
        if (!operation)
          return false;
      }
      auto n = std::min<size_t>(8192, bytes.size() - at);
      if (out.write(bytes.data() + at, n) != n)
        return early;
    }
    return true;
  }
  Status select(size_t n) {
    if (n >= meta.chapters)
      return Status::fail(Error::NotFound, "章节不存在");
    if (chapter == n && section)
      return {};
    PaperCacheScope scope(io);
    if (section) {
      section->suspendBuild();
      seal();
      section.reset();
    }
    page.reset();
    chapter = n;
    chapterHref = source.chapterHref(n);
    pageText.clear();
    prepKey.clear();
    ready = false;
    operation = {};
    // Cache identity includes the book hash and exact resolved font/render
    // identity.
    if (chapterHref.empty())
      return Status::fail(Error::Corrupt, "章节路径缺失");
    css = std::make_unique<CssParser>("");
    epub->cache = root;
    epub->css = css.get();
    epub->spine.resize(meta.chapters);
    epub->spine[n].href = chapterHref;
    epub->toc.clear();
    for (const auto &t : meta.toc)
      epub->toc.push_back({int(t.chapter), t.fragment});
    epub->read = [this](const std::string &s, Print &o, bool early) {
      return extract(s, o, early);
    };
    section = std::make_unique<Section>(epub, int(n), renderer);
    const auto bin = sectionName();
    if (validFile(bin) && section->loadSectionFile(renderSpec)) {
      ++cacheHits;
      return {};
    }
    verified.erase(path(bin));
    if (!operation)
      return operation;
    auto st = prepareSource();
    if (!st)
      return st;
    // Rebuild invalid cache in the same deterministic namespace. No user record
    // removed.
    if (!section->startBuild(renderSpec))
      return fail("图文分页初始化失败");
    return {};
  }
  Status prepareSource() {
    if (ready)
      return {};
    std::string html, styles, href;
    auto st = source.chapterMarkup(chapter, html, styles, href);
    if (!st)
      return st;
    css->clear();
    HalFile sheet{std::string_view(styles)};
    if (css->loadFromStream(sheet) != CssParser::ParseResult::Complete)
      return Status::fail(Error::Corrupt, "章节样式超过预算或损坏");
    // The canonical ZIP entry has already passed length and CRC checks.
    st =
        store.writeReaderCache(rel("html/" + std::to_string(chapter) + ".html"),
                               html.data(), html.size());
    if (st)
      ready = true;
    return st;
  }
  Status extend() {
    renderer.failed = false;
    if (!section)
      return Status::fail(Error::Conflict, "无章节");
    if (!section->isBuilding()) {
      if (section->isBuildComplete())
        return Status::fail(Error::NotFound, "章节末尾");
      auto st = prepareSource();
      if (!st)
        return st;
      if (!section->startBuild(renderSpec))
        return fail("续建分页失败");
    }
    if (observer) {
      operation = observer("pagination", section->pageCount, 0);
      if (!operation) {
        section->abandonBuild();
        return operation;
      }
    }
    ++buildSteps;
    if (!section->buildSomeMore(1)) {
      auto st = fail("章节解析或分页失败");
      section->abandonBuild();
      return st;
    }
    if (renderer.failed) {
      section->abandonBuild();
      return fail("字体度量失败");
    }
    if (section->isBuildComplete())
      seal();
    return {};
  }
  Status ensure(size_t p) {
    if (p >= 4096)
      return Status::fail(Error::TooLarge, "单章超过4096页");
    while (section->pageCount <= p) {
      if (section->isBuildComplete())
        return Status::fail(Error::NotFound, "章节末尾");
      auto st = extend();
      if (!st)
        return st;
    }
    return {};
  }
  static std::string words(const Page &p) {
    std::string s;
    for (const auto &e : p.elements)
      if (e->getTag() == TAG_PageLine) {
        auto &b = *static_cast<const PageLine &>(*e).getBlock();
        for (uint16_t i = 0; i < b.wordCount(); ++i) {
          if (!s.empty() && i && static_cast<unsigned char>(s.back()) < 128 &&
              static_cast<unsigned char>(b.wordText(i)[0]) < 128)
            s += ' ';
          s += b.wordText(i);
        }
        s += '\n';
      }
    return s;
  }
  Status show(size_t p) {
    auto st = ensure(p);
    if (!st)
      return st;
    auto next = section->loadPage(int(p));
    if (!next)
      return fail("图文页缓存损坏");
    loc = {meta.identity,
           "paper-crossmux-1",
           chapter,
           next->visibleTextOffset,
           p,
           key};
    auto text = words(*next);
    std::vector<uint32_t> missing;
    st = fonts.coverage(spec, text, missing);
    if (!st)
      return st;
    page = std::move(next);
    pageText = std::move(text);
    ++loadedPages;
    return {};
  }
  Status target(const Locator &where) {
    PaperCacheScope scope(io);
    operation = {};
    renderer.failed = false;
    if (where.identity != meta.identity || where.engine != "paper-crossmux-1")
      return Status::fail(Error::Conflict, "图文位置所属引擎不符");
    auto st = select(where.chapter);
    if (!st)
      return st;
    if (where.renderKey == key && where.pageHint != SIZE_MAX)
      return show(where.pageHint);
    if (!where.offset)
      return show(0);
    // Build until a page boundary brackets the visible offset; never treat the
    // old engine's UTF-8 byte offsets as CrossMux codepoint offsets.
    while (!section->isBuildComplete()) {
      auto n = section->pageCount;
      if (n && section->getVisibleTextOffsetForPage(n - 1).value_or(0) >=
                   where.offset)
        break;
      st = extend();
      if (!st)
        return st;
    }
    auto p = section->getPageForVisibleTextOffset(uint32_t(where.offset), true);
    if (!p)
      return Status::fail(Error::NotFound, "阅读位置无法定位");
    return show(*p);
  }
};
RichReader::RichReader(Store &s, FontProvider &f, DocumentSource &d,
                       const Metadata &m)
    : impl_(std::make_unique<Impl>(s, f, d, m)) {}
RichReader::~RichReader() = default;
void RichReader::progress(DocumentProgress p) {
  impl_->observer = std::move(p);
}
Status RichReader::configure(const FontSpec &s, int w, int h, int l) {
  auto &i = *impl_;
  auto st = i.fonts.validate(s);
  if (!st)
    return st;
  std::string fallback;
  if (s.allowFallback && s.family != "misans") {
    FontSpec f{s.px, s.weight, s.gray};
    st = i.fonts.validate(f);
    if (!st)
      return st;
    fallback = i.fonts.identity(f);
  }
  auto identity = "crossmux-7dcd8b1-paper1:image0:" + i.fonts.identity(s) +
                  ":" + fallback + ":" + std::to_string(s.allowFallback) + ":" +
                  std::to_string(s.gray) + ":" + std::to_string(s.px) + ":" +
                  std::to_string(s.weight) + ":" + std::to_string(w) + ":" +
                  std::to_string(h) + ":" + std::to_string(l);
  const auto key = sha256(identity.data(), identity.size());
  if (i.key == key) {
    i.spec = s;
    return {};
  }
  {
    PaperCacheScope scope(i.io);
    if (i.section) {
      i.section->suspendBuild();
      i.seal();
      i.section.reset();
    }
  }
  i.key = key;
  i.relative = ".paper/cache/crossmux-1/" + i.meta.identity + "/" + key;
  i.root = i.store.root() + "/" + i.relative;
  for (auto &r : {std::string(".paper/cache/crossmux-1"),
                  ".paper/cache/crossmux-1/" + i.meta.identity, i.relative,
                  i.relative + "/sections", i.relative + "/html"})
    if (!i.mkdirPath(r))
      return Status::fail(Error::Io, "无法创建阅读缓存");
  i.chapter = SIZE_MAX;
  i.spec = s;
  i.width = w;
  i.height = h;
  i.lineHeight = l;
  i.renderer.ascender = s.px;
  i.renderer.lineHeight = l;
  i.renderSpec = {};
  i.renderSpec.viewportWidth = w;
  i.renderSpec.viewportHeight = h;
  i.renderSpec.paragraphAlignment = uint8_t(CssTextAlign::None);
  i.renderSpec.imageRendering = 0;
  i.renderSpec.collectTouchLinks = true;
  i.renderSpec.hyphenationEnabled = true;
  i.verified.clear();
  return {};
}
Status RichReader::go(const Locator &l) { return impl_->target(l); }
Status RichReader::next() {
  auto &i = *impl_;
  PaperCacheScope scope(i.io);
  i.operation = {};
  auto st = i.show(i.loc.pageHint + 1);
  if (st.code == Error::NotFound && i.loc.chapter + 1 < i.meta.chapters) {
    st = i.select(i.loc.chapter + 1);
    if (st)
      st = i.show(0);
  }
  return st;
}
Status RichReader::previous() {
  auto &i = *impl_;
  PaperCacheScope scope(i.io);
  i.operation = {};
  if (i.loc.pageHint)
    return i.show(i.loc.pageHint - 1);
  if (!i.loc.chapter)
    return Status::fail(Error::NotFound, "已到开头");
  auto st = i.select(i.loc.chapter - 1);
  if (!st)
    return st;
  while (!i.section->isBuildComplete()) {
    st = i.extend();
    if (!st)
      return st;
  }
  return i.show(i.section->pageCount - 1);
}
Status RichReader::anchor(size_t ch, const std::string &a) {
  auto &i = *impl_;
  PaperCacheScope scope(i.io);
  i.operation = {};
  auto st = i.select(ch);
  if (!st)
    return st;
  if (a.empty())
    return i.show(0);
  for (;;) {
    auto p = i.section->findAnchor(a);
    if (p)
      return i.show(*p);
    if (i.section->isBuildComplete())
      return Status::fail(Error::NotFound, "书内锚点不存在");
    st = i.extend();
    if (!st)
      return st;
  }
}
Status RichReader::find(const std::string &q, std::vector<Locator> &out,
                        size_t limit) {
  auto &i = *impl_;
  out.clear();
  for (size_t ch = 0; ch < i.meta.chapters && out.size() < limit; ++ch) {
    std::string html, css, href;
    auto st = i.source.chapterMarkup(ch, html, css, href);
    if (!st)
      return st;
    std::vector<size_t> offsets;
    st = crossmuxSearchHtml(html, q, limit - out.size(), offsets, i.observer);
    if (!st)
      return st;
    for (auto offset : offsets)
      out.push_back({i.meta.identity, "paper-crossmux-1", ch, offset});
  }
  return {};
}
Status RichReader::paint(Canvas &canvas, Rect area) {
  auto &i = *impl_;
  if (!i.page)
    return Status::fail(Error::Conflict, "无图文页");
  PaperCacheScope scope(i.io);
  FontReadBatch batch(i.fonts);
  if (!batch.status())
    return batch.status();
  i.operation = {};
  i.warn.clear();
  i.renderer.failed = false;
  i.renderer.text = [&](int x, int y, const char *t, EpdFontFamily::Style style,
                        int dir) {
    std::string visual;
    const char *str = t;
    if (BidiUtils::applyBidiVisual(t, visual, dir))
      str = visual.c_str();
    size_t at = 0;
    std::string text(str);
    int pen = x * 64;
    while (at < text.size()) {
      Rune r;
      auto st = readRune(text, at, r);
      if (!st) {
        i.operation = st;
        return;
      }
      Glyph g;
      st = i.fonts.glyph(r.cp, i.spec, g);
      if (!st) {
        i.operation = st;
        return;
      }
      if (style & (EpdFontFamily::SUP | EpdFontFamily::SUB)) {
        Glyph small = g;
        small.width = (g.width * 2 + 2) / 3;
        small.height = (g.height * 2 + 2) / 3;
        small.left = g.left * 2 / 3;
        small.top = g.top * 2 / 3;
        small.advance64 = g.advance64 * 2 / 3;
        small.coverage2.assign((size_t(small.width) * small.height + 3) / 4, 0);
        for (int yy = 0; yy < small.height; ++yy)
          for (int xx = 0; xx < small.width; ++xx) {
            const auto src = size_t(std::min(g.height - 1, yy * 3 / 2)) *
                                 g.width +
                             std::min(g.width - 1, xx * 3 / 2),
                       dst = size_t(yy) * small.width + xx;
            small.coverage2[dst / 4] |=
                ((g.coverage2[src / 4] >> (6 - 2 * (src % 4))) & 3)
                << (6 - 2 * (dst % 4));
          }
        g = std::move(small);
      }
      if (style & EpdFontFamily::ITALIC) {
        const auto width = g.width + std::max(0, g.height / 6);
        std::vector<uint8_t> data((size_t(width) * g.height + 3) / 4, 0);
        for (int yy = 0; yy < g.height; ++yy)
          for (int xx = 0; xx < g.width; ++xx) {
            auto src = size_t(yy) * g.width + xx,
                 dst = size_t(yy) * width + xx + (g.height - 1 - yy) / 6;
            data[dst / 4] |= ((g.coverage2[src / 4] >> (6 - 2 * (src % 4))) & 3)
                             << (6 - 2 * (dst % 4));
          }
        g.width = width;
        g.coverage2 = std::move(data);
      }
      canvas.glyph(g, pen, y + i.spec.px, area, i.spec.gray, false);
      if (style & EpdFontFamily::BOLD)
        canvas.glyph(g, pen + 64, y + i.spec.px, area, i.spec.gray, false);
      pen += g.advance64;
    }
  };
  i.renderer.line = [&](int x, int y, int xx, int yy, int w, bool black) {
    // All runs share the content viewport; decoration cannot overwrite the
    // shell.
    x = std::clamp(x, area.x, area.x + area.w - 1);
    xx = std::clamp(xx, area.x, area.x + area.w - 1);
    if (y < area.y || y >= area.y + area.h || yy < area.y ||
        yy >= area.y + area.h)
      return;
    canvas.line(x, y, xx, yy, black ? 0 : 3, w);
  };
  i.renderer.image = [&](const std::string &p, int x, int y, int w, int h) {
    std::vector<uint8_t> bytes;
    auto st =
        i.store.read(p.substr(i.store.root().size() + 1), bytes, 1024 * 1024);
    if (st)
      st = paintBookImage(bytes, canvas,
                          {x, y, std::min(w, area.x + area.w - x),
                           std::min(h, area.y + area.h - y)});
    if (!st)
      i.warn = st.message;
    return bool(st);
  };
  ImageBlock::setExtractor(&i, [](void *ctx, const char *s, const char *d) {
    auto &i = *static_cast<Impl *>(ctx);
    const std::string path(d);
    if (!i.io.allowed(path, true))
      return false;
    const auto suffix = path.substr(i.root.size() + 1);
    if (i.validFile(suffix))
      return true;
    if (!i.operation)
      return false;
    if (!i.epub->extractItemToFile(s, path)) {
      if (i.operation.code != Error::Canceled) {
        i.warn =
            i.operation.message.empty() ? "插图提取失败" : i.operation.message;
        i.operation = {};
      }
      return false;
    }
    std::string hash;
    auto st = i.store.hash(i.rel(suffix), hash);
    if (!st) {
      i.warn = st.message;
      return false;
    }
    hash += '\n';
    st = i.store.writeReaderCache(i.rel(suffix + ".sha"), hash.data(),
                                  hash.size());
    if (st)
      i.verified.insert(path);
    return bool(st);
  });
  i.page->render(i.renderer, 0, area.x, area.y);
  ImageBlock::setExtractor(nullptr, nullptr);
  i.renderer.text = {};
  i.renderer.line = {};
  i.renderer.image = {};
  return i.operation;
}
Status RichReader::prepare(size_t limit, const std::function<bool()> &stop,
                           size_t &done) {
  auto &i = *impl_;
  done = 0;
  if (!i.section)
    return {};
  PaperCacheScope scope(i.io);
  // Only use an already-built next page. Never delay input to paginate ahead.
  if (i.loc.pageHint + 1 >= i.section->pageCount)
    return {};
  auto token = i.key + ":" + std::to_string(i.chapter) + ":" +
               std::to_string(i.loc.pageHint + 1);
  if (token != i.prepKey) {
    auto page = i.section->loadPage(i.loc.pageHint + 1);
    if (!page)
      return {};
    i.prepText = Impl::words(*page);
    i.prepAt = 0;
    i.prepKey = token;
  }
  FontReadBatch batch(i.fonts);
  if (!batch.status())
    return batch.status();
  while (done < limit && i.prepAt < i.prepText.size()) {
    if (stop && stop())
      break;
    Rune r;
    auto st = readRune(i.prepText, i.prepAt, r);
    if (!st)
      return st;
    Glyph g;
    if (r.cp != '\n') {
      st = i.fonts.glyph(r.cp, i.spec, g);
      if (!st)
        return st;
    }
    ++done;
  }
  return {};
}
const Locator &RichReader::location() const { return impl_->loc; }
std::vector<ReaderLink> RichReader::links(Rect area) const {
  std::vector<ReaderLink> out;
  if (!impl_->page)
    return out;
  for (const auto &l : impl_->page->links) {
    int x = std::max(area.x, area.x + l.x - 2),
        y = std::max(area.y, area.y + l.y - 2);
    int right = std::min(area.x + area.w, area.x + l.x + l.width + 2),
        bottom = std::min(area.y + area.h,
                          area.y + l.y + std::max<int>(l.height, 24) + 2);
    if (right > x && bottom > y && l.href[0])
      out.push_back({{x, y, right - x, bottom - y}, l.href});
  }
  return out;
}
const std::string &RichReader::text() const { return impl_->pageText; }
const std::string &RichReader::href() const { return impl_->chapterHref; }
const std::string &RichReader::warning() const { return impl_->warn; }
int RichReader::progress28() const {
  auto &i = *impl_;
  return i.section
             ? int(std::min<size_t>(
                   28,
                   (i.loc.pageHint + 1) * 28 /
                       std::max<size_t>(1, i.section->estimatedTotalPages())))
             : 0;
}
std::string RichReader::diagnostics() const {
  auto &i = *impl_;
  size_t images = 0;
  if (i.page)
    for (auto &e : i.page->elements)
      images += e->getTag() == TAG_PageImage;
  return "{\"pipeline\":\"crossmux-section-page-1\",\"cache_hits\":" +
         std::to_string(i.cacheHits) +
         ",\"build_steps\":" + std::to_string(i.buildSteps) +
         ",\"page\":" + std::to_string(i.loc.pageHint) + ",\"pages_built\":" +
         std::to_string(i.section ? i.section->pageCount : 0) +
         ",\"complete\":" +
         (i.section && i.section->isBuildComplete() ? "true" : "false") +
         ",\"page_images\":" + std::to_string(images) +
         ",\"image_page\":" + (images ? "true" : "false") +
         ",\"image_error\":" + jsonString(i.warn) + "}";
}
} // namespace paper
