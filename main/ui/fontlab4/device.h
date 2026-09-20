#pragma once
#include "core.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include <cstdio>
#include <cstring>
#include <new>
#include <sys/stat.h>

namespace paper::lab4 {
// Only this adapter touches LVGL. The page parser/state machine also runs natively.
class Device {
 public:
    bool enabled=true;
    Session session;
    Action Tap(int x,int y,const char* boot,uint32_t uptime){
        Action a=session.Tap(x,y);
        if(a==Action::Legacy){enabled=false;return a;}
        if(a==Action::Vote){
            ::mkdir("/sdcard/inkdesk",0755);
            session.SaveVote("/sdcard/inkdesk/font-lab4-ratings.jsonl",boot,uptime);
            return Action::Redraw;
        }
        return a;
    }
    void Enable(){enabled=true;opened_=false;}
    bool OpenPage(unsigned id){
        enabled=true;opened_=true;
        if(!session.Enter())return false;
        return id==0?true:session.Load(id);
    }
    bool Paint(lv_obj_t* parent,const lv_font_t* uiFont){
        if(!opened_){session.Enter();opened_=true;}
        if(session.page()){
            if(!Image(parent,session.page()->pixels))return false;
            const auto* selected=session.selected();
            if(selected){
                // Marker sits outside all sample ROIs; changing selection preserves sample pixels.
                auto* dot=lv_obj_create(parent);lv_obj_remove_style_all(dot);
                lv_obj_set_pos(dot,13,selected->box.y+4);lv_obj_set_size(dot,6,12);
                lv_obj_set_style_bg_color(dot,lv_color_black(),0);lv_obj_set_style_bg_opa(dot,LV_OPA_COVER,0);
                lv_obj_remove_flag(dot,LV_OBJ_FLAG_CLICKABLE);
                char label[96];std::snprintf(label,sizeof(label),"S%u  %dpx %s T%d %s",session.selectedIndex()+1,
                    selected->px,selected->algorithm.c_str(),selected->threshold,selected->valid?"OK":"INVALID");
                Label(parent,uiFont,24,699,292,27,label);
            }
            const auto& feedback=session.feedbackText();
            const char* notice=feedback=="SAVED TO SD"?"SAVED":feedback=="SAVE FAILED"?"NO SAVE":
                feedback=="LOG FULL"?"LOG FULL":session.running()?"RUNNING":"I1 / V4";
            Label(parent,uiFont,270,15,125,33,notice);
        }else{
            Label(parent,uiFont,20,18,140,40,"BACK");Label(parent,uiFont,392,18,80,40,"OLD");
            Label(parent,uiFont,184,126,112,44,"RETRY");
            Label(parent,uiFont,24,180,432,60,"FONT LAB 4 / RESOURCE ERROR");
            Label(parent,uiFont,24,260,432,60,session.error().c_str());
            Label(parent,uiFont,24,340,432,120,"Copy SD folder:\n/inkdesk/font-lab4\nTap FULL area above to retry.");
        }
        return true;
    }
    cJSON* Status()const{
        auto* json=session.Status();cJSON_AddBoolToObject(json,"enabled",enabled);return json;
    }
 private:
    bool opened_=false;
    struct ImageOwner {lv_image_dsc_t image{};uint8_t* bytes=nullptr;};
    static bool Image(lv_obj_t* parent,const std::vector<uint8_t>& pixels){
        if(pixels.size()!=FrameBytes)return false;
        auto* p=new(std::nothrow) ImageOwner;if(!p)return false;
        p->bytes=static_cast<uint8_t*>(heap_caps_malloc(FrameBytes+8,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!p->bytes){delete p;return false;}
        // Same palette, stride and deletion ownership as ui/lvgl_patterns.h.
        const uint8_t palette[]={0,0,0,255,255,255,255,255};
        std::memcpy(p->bytes,palette,8);std::memcpy(p->bytes+8,pixels.data(),FrameBytes);
        p->image.header.magic=LV_IMAGE_HEADER_MAGIC;p->image.header.cf=LV_COLOR_FORMAT_I1;
        p->image.header.w=Width;p->image.header.h=Height;p->image.header.stride=Width/8;
        p->image.data_size=FrameBytes+8;p->image.data=p->bytes;
        auto* obj=lv_image_create(parent);lv_image_set_src(obj,&p->image);lv_obj_set_pos(obj,0,0);
        lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj,[](lv_event_t* e){auto* image=static_cast<ImageOwner*>(lv_event_get_user_data(e));
            heap_caps_free(image->bytes);delete image;},LV_EVENT_DELETE,p);
        return true;
    }
    static void Label(lv_obj_t* parent,const lv_font_t* font,int x,int y,int w,int h,const char* text){
        auto* label=lv_label_create(parent);lv_obj_remove_style_all(label);
        lv_obj_set_pos(label,x,y);lv_obj_set_size(label,w,h);lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(label,lv_color_white(),0);lv_obj_set_style_bg_opa(label,LV_OPA_COVER,0);
        lv_obj_set_style_text_color(label,lv_color_black(),0);lv_obj_set_style_text_font(label,font,0);
        lv_label_set_long_mode(label,LV_LABEL_LONG_CLIP);lv_label_set_text(label,text);
    }
};
} // namespace paper::lab4
