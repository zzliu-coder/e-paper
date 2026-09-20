#pragma once
#include <cstdint>
struct EpdFontFamily {
  enum Style : uint8_t {
    REGULAR = 0,
    BOLD = 1,
    ITALIC = 2,
    BOLD_ITALIC = 3,
    UNDERLINE = 4,
    STRIKETHROUGH = 8,
    SUP = 16,
    SUB = 32,
    RUBY_CONTINUE = 64
  };
  static constexpr uint8_t TEXT_DECORATION_MASK = 12;
  static bool hasTextDecoration(Style s) {
    return (s & TEXT_DECORATION_MASK) != 0;
  }
};
