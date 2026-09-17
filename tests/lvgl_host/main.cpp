#include "lvgl.h"
#include "ui_demo.h"
#include "ui/components.h"
#include "ui/font_metrics.h"
#include "ui/font_lab.h"
#include "ui/lab_fonts.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
extern "C" {
extern const lv_font_t ui_font_a18,ui_font_a20,ui_font_a22,ui_font_a25,ui_font_a28,ui_font_a30;
extern const lv_font_t ui_font_b18,ui_font_b20,ui_font_b22,ui_font_b25,ui_font_b28,ui_font_b30;
}
const lv_font_t* fonts[2][6]={{&ui_font_a18,&ui_font_a20,&ui_font_a22,&ui_font_a25,&ui_font_a28,&ui_font_a30},{&ui_font_b18,&ui_font_b20,&ui_font_b22,&ui_font_b25,&ui_font_b28,&ui_font_b30}};
alignas(64) std::array<uint8_t,48008> pixels{};
std::array<uint8_t,48000> captured{};
lv_obj_t* screen=nullptr;
void Flush(lv_display_t* display,const lv_area_t* area,uint8_t* data){assert(area->x1==0&&area->y1==0&&area->x2==479&&area->y2==799);std::copy(data+8,data+48008,captured.begin());lv_display_flush_ready(display);}
lv_obj_t* NewScreen(){auto* old=lv_screen_active();auto* s=lv_obj_create(nullptr);lv_obj_remove_style_all(s);lv_obj_set_style_bg_color(s,lv_color_white(),0);lv_obj_set_style_bg_opa(s,255,0);lv_screen_load(s);lv_obj_delete(old);return s;}
void Label(lv_obj_t* s,int x,int y,int w,int h,const char* text,const lv_font_t* font,bool black){
    auto* l=lv_label_create(s);lv_obj_remove_style_all(l);lv_obj_set_pos(l,x,y);lv_obj_set_size(l,w,h);lv_obj_set_style_text_font(l,font,0);lv_obj_set_style_text_color(l,black?lv_color_black():lv_color_white(),0);lv_label_set_long_mode(l,LV_LABEL_LONG_CLIP);lv_label_set_text(l,text);
}
void Rect(lv_obj_t* s,inkdesk::Rect r,bool black){auto* o=lv_obj_create(s);lv_obj_remove_style_all(o);lv_obj_set_pos(o,r.x,r.y);lv_obj_set_size(o,r.w,r.h);lv_obj_set_style_bg_color(o,black?lv_color_black():lv_color_white(),0);lv_obj_set_style_bg_opa(o,255,0);}
bool White(int x,int y){return captured[y*60+x/8]&(0x80>>(x%8));}
struct Pattern {lv_image_dsc_t d{};std::vector<uint8_t> bytes;};
std::vector<std::unique_ptr<Pattern>> images;
const lv_font_t* LabFontForHost(int profile,int size){
    if(profile<paper::LabFontpack2)return LabFont(profile,size);
    // The host renderer has no flash fontpack. Use a nearest static glyph
    // set for bounds/coverage smoke checks; page 5 is excluded from pixel
    // identity assertions against the device for this reason.
    if(profile==paper::LabFontpack4)return LabFont(0,32);
    const int fallback=size<=16?16:size<=18?18:size<=20?20:size<=22?22:size<=24?24:size<=28?28:size<=32?32:40;
    return LabFont(0,fallback);
}
void Render(const ui_demo::Model& model,const std::filesystem::path& path){
    screen=NewScreen();images.clear();inkdesk::Frame f;model.Draw(f);assert(!f.overflow);
    for(size_t i=0;i<f.drawCount;++i){auto& d=f.draws[i];auto b=d.box;auto color=d.black?lv_color_black():lv_color_white();
        if(d.fontProfile>=0){
            const auto* font=LabFontForHost(d.fontProfile,d.size);assert(font);
            lv_point_t extent{};lv_text_get_size(&extent,d.text.c_str(),font,0,0,10000,LV_TEXT_FLAG_NONE);
            assert(extent.x<=b.w&&extent.y<=b.h);
            size_t at=0;while(at<d.text.size()){uint32_t cp=0;at+=inkdesk::utf8Next(d.text.c_str()+at,d.text.size()-at,cp);lv_font_glyph_dsc_t glyph{};assert(lv_font_get_glyph_dsc(font,&glyph,cp,0));}
        }
        if(d.kind==inkdesk::DrawKind::Text){Label(screen,b.x,b.y,b.w,b.h,d.text.c_str(),d.fontProfile>=0?LabFontForHost(d.fontProfile,d.size):fonts[model.theme][paper::FontIndex(d.size)],d.black);continue;}
        if(d.kind==inkdesk::DrawKind::Texture||d.kind==inkdesk::DrawKind::Dashed){
            auto p=std::make_unique<Pattern>();size_t stride=(b.w+7)/8;p->bytes.assign(8+stride*b.h,255);p->bytes[0]=p->bytes[1]=p->bytes[2]=0;
            for(int y=0;y<b.h;++y)for(int x=0;x<b.w;++x)if(paper::PatternBlack(d,x,y))p->bytes[8+y*stride+x/8]&=~(0x80>>(x%8));
            p->d.header.magic=LV_IMAGE_HEADER_MAGIC;p->d.header.cf=LV_COLOR_FORMAT_I1;p->d.header.w=b.w;p->d.header.h=b.h;p->d.header.stride=stride;p->d.data_size=p->bytes.size();p->d.data=p->bytes.data();
            auto* o=lv_image_create(screen);lv_image_set_src(o,&p->d);lv_obj_set_pos(o,b.x,b.y);images.push_back(std::move(p));continue;
        }
        if(d.kind==inkdesk::DrawKind::Dots){for(size_t n=0;n<d.text.size();++n)for(int y=0;y<7;++y)for(int x=0;x<5;++x)if(ui_demo::DotRow(d.text.c_str()[n],y)&(1<<(4-x)))Rect(screen,{b.x+int(n)*6*d.size+x*d.size,b.y+y*d.size,d.size-1,d.size-1},d.black);continue;}
        auto* o=lv_obj_create(screen);lv_obj_remove_style_all(o);lv_obj_set_pos(o,b.x,b.y);lv_obj_set_size(o,b.w,b.h);
        if(d.kind==inkdesk::DrawKind::Panel||d.kind==inkdesk::DrawKind::PanelFill){lv_obj_set_style_radius(o,d.radius,0);lv_obj_set_style_border_width(o,d.stroke,0);lv_obj_set_style_border_color(o,color,0);lv_obj_set_style_bg_color(o,d.kind==inkdesk::DrawKind::Panel?lv_color_white():color,0);lv_obj_set_style_bg_opa(o,255,0);}
        else if(d.kind==inkdesk::DrawKind::Rect){lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,color,0);}
        else{lv_obj_set_style_bg_color(o,color,0);lv_obj_set_style_bg_opa(o,255,0);}
    }
    lv_refr_now(nullptr);std::ofstream out(path,std::ios::binary);out<<"P4\n480 800\n";for(auto b:captured)out.put(char(b^255));
}
int main(int argc,char** argv){
    assert(argc==2);std::filesystem::create_directories(argv[1]);lv_init();auto* display=lv_display_create(480,800);lv_display_set_color_format(display,LV_COLOR_FORMAT_I1);lv_display_set_buffers(display,pixels.data(),nullptr,pixels.size(),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,Flush);
    for(int theme=0;theme<2;++theme)for(int size=0;size<6;++size){
        screen=NewScreen();Rect(screen,{240,0,240,80},true);
        Label(screen,8,10,220,60,"阅读进度已加入",fonts[theme][size],true);Label(screen,248,10,220,60,"阅读进度已加入",fonts[theme][size],false);lv_refr_now(nullptr);
        int ink=0;for(int y=10;y<70;++y)for(int x=8;x<228;++x){assert(White(x,y)!=White(x+240,y));ink+=!White(x,y);}assert(ink>50);
    }
    ui_demo::Model m;auto save=[&](const char* name){Render(m,std::filesystem::path(argv[1])/(std::string(name)+".pbm"));};
    save("home");m.page=ui_demo::Page::Reading;save("reading");m.controls=true;save("reading-controls");m.page=ui_demo::Page::Settings;save("settings");
    m.page=ui_demo::Page::Fonts;save("fonts-A");m.theme=1;save("fonts-B");m.theme=0;m.page=ui_demo::Page::Gallery;
    for(int i=0;i<4;++i){m.gallery=i;save(("gallery-"+std::to_string(i)).c_str());
        if(i==1)for(int row=0;row<4;++row)for(int col=0;col<2;++col){
            const int bx=20+226*col,by=184+108*row;
            for(int y=0;y<20;++y)for(int x=0;x<20;++x){
                const bool bit=White(bx+x,by+y);
                assert(bit==White(bx+213-x,by+y));assert(bit==White(bx+x,by+91-y));assert(bit==White(bx+213-x,by+91-y));
            }
        }
        if(i==2)for(int row=0;row<3;++row){
            int black=0;for(int y=198+132*row;y<274+132*row;++y)for(int x=132;x<440;++x)black+=!White(x,y);
            assert(black*100==308*76*25*(row+1));
        }
    }
    m.page=ui_demo::Page::FontLab;
    for(int profile=0;profile<4;++profile)for(int page=0;page<paper::LabPageCount;++page)for(int size=0;size<8;++size){m.labProfile=profile;m.labPage=page;m.labSize=size;save(("lab-"+std::to_string(profile)+"-"+std::to_string(page)+"-"+std::to_string(size)).c_str());}
    for(int profile=0;profile<4;++profile)for(int size:{16,18,20,22,24,28,32,40}){
        screen=NewScreen();Rect(screen,{240,0,240,100},true);
        Label(screen,8,10,220,80,"阅读美晨",LabFont(profile,size),true);Label(screen,248,10,220,80,"阅读美晨",LabFont(profile,size),false);lv_refr_now(nullptr);
        int ink=0;for(int y=10;y<90;++y)for(int x=8;x<228;++x){assert(White(x,y)!=White(x+240,y));ink+=!White(x,y);}assert(ink>50);
    }
    std::cout<<"PASS: native LVGL I1 font complement, existing UI and 192 lab configurations\n";
}
