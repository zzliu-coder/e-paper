#pragma once
#include "Epub/css/CssParser.h"
#include "HalStorage.h"
#include <functional>
#include <vector>
class Epub {
public:
  struct SpineItem {
    std::string href;
  };
  struct TocItem {
    int spineIndex;
    std::string anchor;
  };
  std::string cache, language = "en";
  std::vector<SpineItem> spine;
  std::vector<TocItem> toc;
  CssParser *css = nullptr;
  std::function<bool(const std::string &, Print &, bool)> read;
  const std::string &getCachePath() const { return cache; }
  const SpineItem &getSpineItem(int i) const { return spine.at(i); }
  CssParser *getCssParser() { return css; }
  int getTocIndexForSpineIndex(int i) const {
    for (size_t n = 0; n < toc.size(); ++n)
      if (toc[n].spineIndex == i)
        return int(n);
    return -1;
  }
  size_t getTocItemsCount() const { return toc.size(); }
  const TocItem &getTocItem(size_t i) const { return toc.at(i); }
  const std::string &getLanguage() const { return language; }
  bool readItemContentsToStream(const std::string &p, Print &out, size_t,
                                bool early = false) {
    return read && read(p, out, early);
  }
  bool extractItemToFile(const std::string &p, const std::string &dest) {
    const auto tmp = dest + ".part";
    HalFile f;
    if (!Storage.openFileForWrite("EPUB", tmp, f))
      return false;
    bool ok = readItemContentsToStream(p, f, 8192) && f.good();
    ok = f.close() && ok;
    if (ok && Storage.exists(dest.c_str()))
      ok = Storage.remove(dest.c_str());
    if (ok)
      ok = Storage.rename(tmp.c_str(), dest.c_str());
    if (!ok)
      Storage.remove(tmp.c_str());
    return ok;
  }
};
