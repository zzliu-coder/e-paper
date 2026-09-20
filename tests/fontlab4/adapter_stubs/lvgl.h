#pragma once
// Test-only API/lifetime stand-in. This does NOT implement the LVGL rasterizer.
#include <cstdint>
#include <vector>
#include <string>
#include <cstdlib>
struct lv_font_t {};
struct lv_image_header_t {int magic=0,cf=0,w=0,h=0,stride=0;};
struct lv_image_dsc_t {lv_image_header_t header;uint32_t data_size=0;const uint8_t* data=nullptr;};
struct lv_event_t {void* user_data=nullptr;};
struct lv_obj_t {
    std::vector<lv_obj_t*> children;
    const lv_image_dsc_t* image=nullptr;
    void(*on_delete)(lv_event_t*)=nullptr;void* user=nullptr;
    int x=0,y=0,w=0,h=0;std::string text;
};
constexpr int LV_IMAGE_HEADER_MAGIC=0x19,LV_COLOR_FORMAT_I1=7;
constexpr int LV_OBJ_FLAG_CLICKABLE=1,LV_OPA_COVER=255,LV_EVENT_DELETE=2,LV_LABEL_LONG_CLIP=3;
inline lv_obj_t* lv_obj_create(lv_obj_t* parent){auto* o=new lv_obj_t;if(parent)parent->children.push_back(o);return o;}
inline lv_obj_t* lv_label_create(lv_obj_t* p){return lv_obj_create(p);}
inline lv_obj_t* lv_image_create(lv_obj_t* p){return lv_obj_create(p);}
inline void lv_obj_remove_style_all(lv_obj_t*){}
inline void lv_obj_set_pos(lv_obj_t* o,int x,int y){o->x=x;o->y=y;}
inline void lv_obj_set_size(lv_obj_t* o,int w,int h){o->w=w;o->h=h;}
inline void lv_obj_remove_flag(lv_obj_t*,int){}
inline int lv_color_black(){return 0;}
inline int lv_color_white(){return 255;}
inline void lv_obj_set_style_bg_color(lv_obj_t*,int,int){}
inline void lv_obj_set_style_bg_opa(lv_obj_t*,int,int){}
inline void lv_obj_set_style_text_color(lv_obj_t*,int,int){}
inline void lv_obj_set_style_text_font(lv_obj_t*,const lv_font_t*,int){}
inline void lv_label_set_long_mode(lv_obj_t*,int){}
inline void lv_label_set_text(lv_obj_t* o,const char* s){o->text=s;}
inline void lv_image_set_src(lv_obj_t* o,const void* image){o->image=static_cast<const lv_image_dsc_t*>(image);}
inline void lv_obj_add_event_cb(lv_obj_t* o,void(*cb)(lv_event_t*),int,void* user){o->on_delete=cb;o->user=user;}
inline void* lv_event_get_user_data(lv_event_t* e){return e->user_data;}
inline void lv_obj_delete(lv_obj_t* o){for(auto* child:o->children)lv_obj_delete(child);if(o->on_delete){lv_event_t e{o->user};o->on_delete(&e);}delete o;}
