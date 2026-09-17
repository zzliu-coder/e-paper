#pragma once
#include <cstdint>
#include <cstddef>
using lv_coord_t=int32_t;
constexpr int LV_IMAGE_HEADER_MAGIC=0,LV_COLOR_FORMAT_L8=0;
struct lv_image_dsc_t {
 struct {int magic,cf,flags,w,h,stride;} header{};
 size_t data_size=0;
 const uint8_t* data=nullptr;
};
