#pragma once
#include "text.hpp"
namespace paper {
// Decode bounded PNG/JPEG, composite alpha onto paper white, render 1-bit.
Status paintBookImage(const std::vector<uint8_t>&,Canvas&,Rect);
}
