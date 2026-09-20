#pragma once
#include <cstdint>
#include <string>
struct ImageDimensions {int16_t width=0,height=0;};
enum class DecodeOutput {FrameBuffer,PixelCache};
class ImageToFramebufferDecoder {
public:
 virtual ~ImageToFramebufferDecoder()=default;
 virtual bool getDimensions(const std::string&,ImageDimensions&)=0;
 static bool validateAndStoreDimensions(uint32_t w,uint32_t h,ImageDimensions&out,const char*){
 if(!w||!h||w>8192||h>8192||uint64_t(w)*h>4*1024*1024)return false;
 out={int16_t(w),int16_t(h)};return true;}
};
