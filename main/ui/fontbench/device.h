#pragma once
#include "core.h"
#include "baseline_identity.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include <cstdio>
#include <cstring>
#include <new>
#include <sys/stat.h>

namespace paper::fontbench {
// Only this adapter touches LVGL. The page parser/state machine also runs natively.
class Device {
 public:
    bool enabled=true;
    Session session;
    Action Tap(int x,int y,const char* boot,uint32_t uptime){
        Action a=session.Tap(x,y);
        if(a==Action::Vote){
            ::mkdir("/sdcard/inkdesk",0755);
            session.SaveVote("/sdcard/inkdesk/fontbench-ratings.jsonl",boot,uptime);
            return Action::Redraw;
        }
        return a;
    }
    Action Control(int field,int value,const char* boot,uint32_t uptime){
        Action a=session.ControlValue(field,value);
        if(a==Action::Vote){::mkdir("/sdcard/inkdesk",0755);
            session.SaveVote("/sdcard/inkdesk/fontbench-ratings.jsonl",boot,uptime);return Action::Redraw;}
        return a;
    }
    void Enable(){enabled=true;if(staged_){staged_=false;return;}opened_=false;}
    bool OpenPage(unsigned id){enabled=true;opened_=true;staged_=true;if(!session.Enter())return false;return session.LegacyPage(id);}
    bool Open(){enabled=true;opened_=true;staged_=true;return session.Enter();}
    bool Apply(const Config& c){enabled=true;opened_=true;staged_=true;if(!session.Enter())return false;return session.Apply(c);}
    using BaselineResolver=const lv_font_t* (*)(int);
    bool Paint(lv_obj_t* parent,const lv_font_t* uiFont,BaselineResolver resolve=nullptr){
        if(!opened_){session.Enter();opened_=true;}
        staged_=false;
        if(session.ready()){
            if(!Image(parent,session.pixels()))return false;
            static constexpr const char* names[]={"SourceHan","MiSans","Harmony","LXGW","WQY","PingFang","Heiti","Noto"};
            for(size_t i=0;i<session.cards().size();++i){const auto& c=session.cards()[i];
                if(c.firmware){const auto* f=resolve?resolve(c.spec.px):nullptr;
                    const bool exact=c.spec.font==0&&c.spec.weight==500&&c.spec.threshold==128&&c.spec.bpp==2&&c.spec.output==0&&std::strlen(BaselineAssetSha)==64;
                    const bool ok=exact&&f&&Original(parent,f,c);
                    session.FirmwareResolved(i,ok,BaselineAssetSha);
                }
                char line[80];std::snprintf(line,sizeof(line),"%s%s %d %s %d/%d",c.baseline?"A ":"",names[c.spec.font],c.spec.px,Algorithms[c.spec.algorithm],c.spec.weight,c.spec.threshold);
                Label(parent,uiFont,c.box.x+2,c.box.y-24,206,24,line);
                if(session.calibration()){const char* shade[]={"WHITE / 255","LIGHT / 170","DARK / 85","BLACK / 0"};Label(parent,uiFont,c.box.x+2,c.box.y-24,206,24,shade[i]);}
                else if(!c.valid)Label(parent,uiFont,c.box.x+10,c.box.y+66,188,25,
                    c.error.empty()?"SAMPLE INVALID":c.error.c_str());
            }
            if(session.selectedIndex()<session.cards().size()){
                const auto& c=session.cards()[session.selectedIndex()];
                auto* marker=lv_obj_create(parent);lv_obj_remove_style_all(marker);
                lv_obj_set_pos(marker,c.box.x-9,c.box.y-20);lv_obj_set_size(marker,5,10);
                lv_obj_set_style_bg_color(marker,lv_color_black(),0);lv_obj_set_style_bg_opa(marker,LV_OPA_COVER,0);
                lv_obj_remove_flag(marker,LV_OBJ_FLAG_CLICKABLE);
            }
            Label(parent,uiFont,20,769,440,27,
                session.WantsGray()&&!session.grayArmed()?"GRAY LOCKED / TAP ENABLE":session.notice().c_str());
        }else{
            Label(parent,uiFont,20,18,180,40,"BACK / FONT BENCH");
            Label(parent,uiFont,24,170,430,50,"SD RESOURCE REQUIRED");
            Label(parent,uiFont,24,250,430,110,session.error().c_str());
            Label(parent,uiFont,24,360,432,120,"Copy SD folder:\n/inkdesk/fontbench\nThen tap FULL below.");
            Label(parent,uiFont,294,723,80,40,"FULL");
        }
        return true;
    }
    cJSON* Status(bool compact=false)const{
        auto* json=session.Status(compact);cJSON_AddBoolToObject(json,"enabled",enabled);return json;
    }
 private:
    bool opened_=false,staged_=false;
    static bool Original(lv_obj_t* parent,const lv_font_t* font,const Card& c){
        const char* texts[]={"阅读设置\n清晨光线","警器藏赢\n餐饮薄雾","中文 Aa\nIl1 O0","清晨光线\n慢慢移来"};
        if(c.spec.content<0||c.spec.content>3||font->line_height*2+4>c.box.h)return false;
        const auto* text=texts[c.spec.content];
        // Check every UTF-8 codepoint on the exact font callback, with no fallback.
        const unsigned char* p=reinterpret_cast<const unsigned char*>(text);int lineWidth=0,row=0;
        while(*p){uint32_t cp=*p++;if(cp=='\n'){lineWidth=0;++row;continue;}
            int tail=0;if(cp>=0xF0){cp&=7;tail=3;}else if(cp>=0xE0){cp&=15;tail=2;}else if(cp>=0xC0){cp&=31;tail=1;}
            for(int k=0;k<tail;++k){if((*p&0xC0)!=0x80)return false;cp=(cp<<6)|(*p++&63);}
            lv_font_glyph_dsc_t d{};if(!font->get_glyph_dsc||!font->get_glyph_dsc(font,&d,cp,0)||d.is_placeholder)return false;
            if(8+lineWidth+d.ofs_x<0||8+lineWidth+d.ofs_x+d.box_w>c.box.w)return false;
            const int top=4+(row+1)*font->line_height-font->base_line-d.box_h-d.ofs_y;
            if(top<0||top+d.box_h>c.box.h)return false;
            lineWidth+=d.adv_w;if(lineWidth>c.box.w-16)return false;
        }
        auto* cover=lv_obj_create(parent);lv_obj_remove_style_all(cover);lv_obj_set_pos(cover,c.box.x,c.box.y);lv_obj_set_size(cover,c.box.w,c.box.h);
        lv_obj_set_style_bg_color(cover,c.spec.inverse?lv_color_black():lv_color_white(),0);lv_obj_set_style_bg_opa(cover,LV_OPA_COVER,0);lv_obj_remove_flag(cover,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(cover,LV_OBJ_FLAG_SCROLLABLE);
        auto* label=lv_label_create(cover);lv_obj_remove_style_all(label);lv_obj_set_pos(label,8,4);lv_obj_set_size(label,c.box.w-16,c.box.h-4);
        lv_obj_set_style_text_font(label,font,0);lv_obj_set_style_text_color(label,c.spec.inverse?lv_color_white():lv_color_black(),0);
        lv_label_set_long_mode(label,LV_LABEL_LONG_CLIP);lv_label_set_text(label,text);lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(label,LV_OBJ_FLAG_SCROLLABLE);
        return true;
    }
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
} // namespace paper::fontbench
