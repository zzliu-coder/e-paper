#pragma once
#include "components.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include <new>
namespace paper {
struct PatternImage {lv_image_dsc_t image{};uint8_t* bytes=nullptr;};
inline bool AddPattern(lv_obj_t* parent,const inkdesk::Draw& d){
    auto* p=new(std::nothrow) PatternImage;if(!p)return false;
    const size_t stride=(d.box.w+7)/8,size=8+stride*d.box.h;
    p->bytes=(uint8_t*)heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!p->bytes){delete p;return false;}
    // I1 image palette is BGRA8888: index 0 black, index 1 white.
    const uint8_t palette[8]={0,0,0,255,255,255,255,255};
    std::memcpy(p->bytes,palette,8);std::memset(p->bytes+8,255,size-8);
    for(int y=0;y<d.box.h;++y)for(int x=0;x<d.box.w;++x)
        if(PatternBlack(d,x,y))p->bytes[8+y*stride+x/8]&=uint8_t(~(0x80>>(x&7)));
    p->image.header.magic=LV_IMAGE_HEADER_MAGIC;p->image.header.cf=LV_COLOR_FORMAT_I1;
    p->image.header.w=d.box.w;p->image.header.h=d.box.h;p->image.header.stride=stride;
    p->image.data_size=size;p->image.data=p->bytes;
    auto* obj=lv_image_create(parent);lv_image_set_src(obj,&p->image);
    lv_obj_set_pos(obj,d.box.x,d.box.y);lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj,[](lv_event_t* e){auto* image=(PatternImage*)lv_event_get_user_data(e);heap_caps_free(image->bytes);delete image;},LV_EVENT_DELETE,p);
    return true;
}
}
