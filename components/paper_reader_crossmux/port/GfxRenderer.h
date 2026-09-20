#pragma once
#include "Arduino.h"
#include "EpdFontFamily.h"
#include <deque>
#include <functional>
#include <string>
namespace BidiUtils {
enum class BidiBaseDir { Auto = -1, Ltr = 0, Rtl = 1 };
}
class GfxRenderer {
public:
  using Style = EpdFontFamily::Style;
  int ascender = 26, lineHeight = 40;
  std::function<int(const char *, Style)> measure;
  std::function<void(int, int, const char *, Style, int)> text;
  std::function<void(int, int, int, int, int, bool)> line;
  std::function<bool(const std::string &, int, int, int, int)> image;
  mutable bool failed = false;
  int getTextAdvanceX(int, const char *s,
                      Style style = EpdFontFamily::REGULAR) const {
    return measure ? measure(s, style) : 0;
  }
  int getTextWidth(int id, const char *s, Style style = EpdFontFamily::REGULAR,
                   int = -1) const {
    return getTextAdvanceX(id, s, style);
  }
  int getTextWidth(int id, const char *s, Style style,
                   BidiUtils::BidiBaseDir) const {
    return getTextAdvanceX(id, s, style);
  }
  int getSpaceWidth(int id, Style style = EpdFontFamily::REGULAR) const {
    return getTextAdvanceX(id, " ", style);
  }
  int getKerning(int, uint32_t, uint32_t,
                 Style = EpdFontFamily::REGULAR) const {
    return 0;
  }
  int getSpaceAdvance(int id, uint32_t, uint32_t,
                      Style s = EpdFontFamily::REGULAR) const {
    return getSpaceWidth(id, s);
  }
  int getFontAscenderSize(int) const { return ascender; }
  int getLineHeight(int, float compression = 1) const {
    return std::max(1, int(lineHeight * compression));
  }
  bool isSdCardFont(int) const {
    return false;
  } // PAPER owns validation/batching.
  bool isFontCacheScanning() const { return false; }
  void ensureSdCardFontReady(int, const std::deque<std::string> &, bool,
                             uint8_t) const {}
  template <class D = int>
  void drawText(int, int x, int y, const char *s, bool,
                Style style = EpdFontFamily::REGULAR, D dir = D(-1)) const {
    if (text)
      text(x, y, s, style, int(dir));
  }
  void drawLine(int x, int y, int xx, int yy, int width, bool black) const {
    if (line)
      line(x, y, xx, yy, width, black);
  }
};
