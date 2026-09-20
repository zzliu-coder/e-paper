#include "paper/image.hpp"
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <cstddef>
#include <memory>
#include "../vendor/tjpgd/tjpgd.h"
namespace {
// Enforce a cumulative decoder budget, including PNG inflate scratch. stb uses
// only these allocation functions; failure returns through its normal API.
std::mutex imageMutex;
size_t allocated=0;
constexpr size_t kDecodeBudget=3*1024*1024;
struct alignas(std::max_align_t) Allocation {size_t bytes;};
void* imageAlloc(size_t n){
    if(n>kDecodeBudget-allocated)return nullptr;
    auto* p=static_cast<Allocation*>(std::malloc(sizeof(Allocation)+n));
    if(!p)return nullptr;
    p->bytes=n;allocated+=n;return p+1;
}
void imageFree(void* p){if(!p)return;auto* h=static_cast<Allocation*>(p)-1;allocated-=h->bytes;std::free(h);}
void* imageResize(void* p,size_t n){
    if(!p)return imageAlloc(n);
    auto* h=static_cast<Allocation*>(p)-1;
    if(n>kDecodeBudget-(allocated-h->bytes))return nullptr;
    auto old=h->bytes;
    auto* q=static_cast<Allocation*>(std::realloc(h,sizeof(Allocation)+n));
    if(!q)return nullptr;
    q->bytes=n;allocated=allocated-old+n;return q+1;
}
}
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 4096
#define STBI_MALLOC(n) imageAlloc(n)
#define STBI_REALLOC(p,n) imageResize(p,n)
#define STBI_FREE(p) imageFree(p)
#include "stb_image.h"
namespace paper {
namespace {
struct JpegStream {
    const std::vector<uint8_t>& input;
    size_t position=0;
    int width=0,height=0;
    std::unique_ptr<uint8_t[]> pixels;
};
size_t jpegInput(JDEC* decoder,uint8_t* out,size_t count){
    auto& stream=*static_cast<JpegStream*>(decoder->device);
    count=std::min(count,stream.input.size()-stream.position);
    if(out)memcpy(out,stream.input.data()+stream.position,count);
    stream.position+=count;return count;
}
int jpegOutput(JDEC* decoder,void* bitmap,JRECT* rect){
    auto& s=*static_cast<JpegStream*>(decoder->device);
    if(rect->right>=s.width||rect->bottom>=s.height)return 0;
    size_t width=rect->right-rect->left+1;
    for(unsigned y=rect->top;y<=rect->bottom;++y)
        memcpy(s.pixels.get()+size_t(y)*s.width+rect->left,
               static_cast<uint8_t*>(bitmap)+(y-rect->top)*width,width);
    return 1;
}
}
Status paintBookImage(const std::vector<uint8_t>&bytes,Canvas&canvas,Rect area){
    if(bytes.empty()||bytes.size()>1024*1024||area.w<1||area.h<1)
        return Status::fail(Error::TooLarge,"插图文件超过解码预算");
    std::lock_guard<std::mutex> lock(imageMutex);
    int w=0,h=0,n=0;
    if(!stbi_info_from_memory(bytes.data(),int(bytes.size()),&w,&h,&n)||w<1||h<1)
        return Status::fail(Error::Unsupported,"插图格式未支持或已损坏");
    const int sourceW=w,sourceH=h;
    JpegStream jpeg{bytes,0,0,0,{}};
    unsigned channels=2;
    unsigned char* pixels=nullptr;
    if(uint64_t(w)*h>512*1024){
        if(bytes.size()<2||bytes[0]!=0xff||bytes[1]!=0xd8)
            return Status::fail(Error::TooLarge,"大尺寸 PNG 超过解码预算，请缩小原图");
        if(w>4096||h>4096)return Status::fail(Error::TooLarge,"插图边长超过 4096 像素");
        // Baseline JPEG is decoded MCU-by-MCU, with IDCT downscaling. Never
        // allocate the full-size image; output <=512 KiB plus a 16 KiB pool.
        std::unique_ptr<uint8_t[]> pool(new(std::nothrow)uint8_t[16384]);
        if(!pool)return Status::fail(Error::Unavailable,"插图解码工作区不足");
        JDEC decoder{};
        auto result=jd_prepare(&decoder,jpegInput,pool.get(),16384,&jpeg);
        if(result!=JDR_OK)return Status::fail(Error::Unsupported,"大图需标准 JPEG，渐进 JPEG 暂未支持");
        if(decoder.width!=w||decoder.height!=h)return Status::fail(Error::Corrupt,"JPEG 图像尺寸声明不一致");
        unsigned scale=0;
        const double fit=std::min(1.0,std::min(double(area.w)/w,double(area.h)/h));
        while(scale<3&&((uint64_t(w>>scale)*(h>>scale)>512*1024)||
              ((w>>(scale+1))>=w*fit&&(h>>(scale+1))>=h*fit)))++scale;
        jpeg.width=w>>scale;jpeg.height=h>>scale;
        if(jpeg.width<1||jpeg.height<1)return Status::fail(Error::TooLarge,"插图比例超出缩小范围");
        const size_t pixelBytes=size_t(jpeg.width)*jpeg.height;
        if(pixelBytes>512*1024)return Status::fail(Error::TooLarge,"插图缩小后仍超过预算");
        jpeg.pixels.reset(new(std::nothrow)uint8_t[pixelBytes]);
        if(!jpeg.pixels)return Status::fail(Error::Unavailable,"插图像素缓冲不足");
        result=jd_decomp(&decoder,jpegOutput,scale);
        if(result!=JDR_OK)return Status::fail(Error::Corrupt,"JPEG 数据截断或解码失败");
        w=jpeg.width;h=jpeg.height;channels=1;pixels=jpeg.pixels.get();
    }else pixels=stbi_load_from_memory(bytes.data(),int(bytes.size()),&w,&h,&n,2);
    if(!pixels)return Status::fail(Error::Corrupt,"插图解码失败或内存预算不足");
    const double scale=std::min(1.0,std::min(double(area.w)/sourceW,double(area.h)/sourceH));
    const int dw=std::max(1,int(sourceW*scale)),dh=std::max(1,int(sourceH*scale));
    const int ox=area.x+(area.w-dw)/2,oy=area.y+(area.h-dh)/2;
    static constexpr unsigned bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    for(int y=0;y<dh;++y)for(int x=0;x<dw;++x){
        // Area average before 1-bit ordered dither preserves thin illustration
        // details better than point sampling; never asks the panel for gray.
        const int x0=x*w/dw,x1=std::max(x0+1,(x+1)*w/dw);
        const int y0=y*h/dh,y1=std::max(y0+1,(y+1)*h/dh);
        uint64_t sum=0,count=0;
        for(int sy=y0;sy<y1;++sy)for(int sx=x0;sx<x1;++sx){
            auto* p=pixels+(size_t(sy)*w+sx)*channels;
            sum+=channels==1?p[0]:(unsigned(p[0])*p[1]+255u*(255-p[1])+127)/255;++count;
        }
        const unsigned level=unsigned(sum/count);
        canvas.pixel(ox+x,oy+y,level>=(bayer[y&3][x&3]*16+8)?3:0);
    }
    if(channels==2)stbi_image_free(pixels);
    return {};
}
}
