#include "inkdesk_app.h"
#include "inkdesk_reader.h"
#include "ui_demo.h"
#include "ui/lab_fonts.h"
#include "ui/font_lab.h"
#include "ui/fontbench/device.h"
#include "ui/lvgl_patterns.h"
#include "inkdesk_r2/core/desk.h"
#include "inkdesk_r2/core/journal.h"
#include "selftest.h"
#include "personal_sdk.h"
#include "display/lv_adapter_display.h"
#include "fontpack_lvgl.h"
#include "esp_lv_adapter.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <sys/stat.h>
extern "C" {
extern const lv_font_t ui_font_a18,ui_font_a25,ui_font_a28,ui_font_a30;
extern const lv_font_t ui_font_b18,ui_font_b25,ui_font_b28,ui_font_b30;
extern const lv_font_t ui_font_a20,ui_font_a22,ui_font_b20,ui_font_b22;
}

namespace inkdesk_app {
namespace {
using namespace inkdesk;
uint32_t Now() { return esp_timer_get_time()/1000; }
std::mutex labLogMutex;
class Store : public FileStore {
 public:
 IoStatus read(const char* path,uint8_t* data,size_t cap,size_t& size) override {
    size=0; const std::string name=std::string("/sdcard")+path;
    FILE* f=fopen(name.c_str(),"rb");
    if (!f) return errno==ENOENT ? IoStatus::Missing : IoStatus::Unavailable;
    if (fseek(f,0,SEEK_END)!=0) { fclose(f); return IoStatus::Error; }
    long n=ftell(f); rewind(f);
    if (n<0 || size_t(n)>cap) { fclose(f); return IoStatus::TooLarge; }
    size=fread(data,1,n,f); bool ok=size==size_t(n) && !ferror(f);
    ok=fclose(f)==0 && ok; return ok ? IoStatus::Ok : IoStatus::Error;
 }
 bool write(const char* path,const uint8_t* data,size_t size) override {
    mkdir("/sdcard/inkdesk",0755);
    FILE* f=fopen((std::string("/sdcard")+path).c_str(),"wb");
    if (!f) return false;
    bool ok=fwrite(data,1,size,f)==size;
    ok=fflush(f)==0 && ok; return fclose(f)==0 && ok;
 }
};
struct Runtime {
 Engine engine;
 Store files;
 Journal journal{files};
 std::array<uint8_t,kMaxPayload> payload{};
 Frame fallback{};
 InkDeskReader reader;
 ui_demo::Model demo;
 paper::fontbench::Device fontbench;
 Frame demoFrame{};
 ui_demo::Page paintedDemoPage=ui_demo::Page::Home;
 bool demoPainted=false;
 int paintedDemoTheme=-1;
 unsigned demoFast=0;
 bool lastFull=false;
 std::array<uint8_t,48000> uiPixels{};
 std::array<uint8_t,96000> fontbenchPixels{};
 uint32_t fontbenchPixelRevision=0;
 uint32_t uiPixelRevision=0;
 uint32_t lastSave=0;
 int renderError=0;
 bool loaded=false;
};
Runtime* state=nullptr;
struct InputRow { int kind=0,x=0,y=0; uint32_t epoch=0; paper::fontbench::Config config{}; };
QueueHandle_t inputs=nullptr;
std::atomic<bool> opening{true},active{false};
std::atomic<bool> accepting{false};
// Automated FontBench runs must not be invalidated by phantom touch/key
// events from the capacitive panel.  Remote protocol commands remain active.
std::atomic<bool> fontbenchInputLocked{false};
std::atomic<uint32_t> rejected{0},visibleEpoch{0};
std::mutex snapshotMutex;
std::mutex frameMutex;
cJSON* snapshot=nullptr;
cJSON* fontbenchSnapshot=nullptr;
lv_obj_t* screen=nullptr;

bool Post(InputRow row) {
 if(!accepting && row.kind!=3) { ++rejected; return false; }
 row.epoch=visibleEpoch;
 if (!inputs || xQueueSend(inputs,&row,0)!=pdTRUE) {++rejected;return false;}
 return true;
}
void Click(lv_event_t* e) {
 if (lv_event_get_code(e)!=LV_EVENT_CLICKED || !active) return;
 auto* input=lv_indev_active(); if (!input) return;
 lv_point_t p{}; lv_indev_get_point(input,&p); Post({0,p.x,p.y});
}
const lv_font_t* OriginalUiExact(int size) {
 switch(size){case 18:return &ui_font_a18;case 20:return &ui_font_a20;case 22:return &ui_font_a22;
 case 25:return &ui_font_a25;case 28:return &ui_font_a28;case 30:return &ui_font_a30;default:return nullptr;}
}
const lv_font_t* Font(int size) {
 const int physical=size<=20 ? 18 : size<=25 ? 25 : size<=28 ? 28 : 30;
 if(state->demo.visible){
   if(size>18&&size<=20)return state->demo.theme?&ui_font_b20:&ui_font_a20;
   if(size>20&&size<=22)return state->demo.theme?&ui_font_b22:&ui_font_a22;
   if(state->demo.theme==1)return physical==18?&ui_font_b18:physical==25?&ui_font_b25:physical==28?&ui_font_b28:&ui_font_b30;
   return physical==18?&ui_font_a18:physical==25?&ui_font_a25:physical==28?&ui_font_a28:&ui_font_a30;
 }
 const auto* f=fontpack_lv_font_get(physical,2);
 return f ? f : fontpack_lv_font_ui();
}
int LabFontpackSize(int size) {
 // An experiment must never silently map 25 px to 28 px.
 return size;
}
const lv_font_t* LabFontForDevice(int profile,int size) {
 if(profile<0)return Font(size);
 if(profile<paper::LabFontpack2)return LabFont(profile,size);
 const bool four=profile==paper::LabFontpack4;
 const auto* packed=fontpack_lv_font_get(static_cast<uint16_t>(LabFontpackSize(size)),
                                         static_cast<uint16_t>(four?4:2));
 if(packed)return packed;
 // Missing specifications stay unavailable; never substitute another candidate.
 return nullptr;
}
void DotEvent(lv_event_t* event) {
 auto* d=static_cast<Draw*>(lv_event_get_user_data(event));
 if(lv_event_get_code(event)==LV_EVENT_DELETE){delete d;return;}
 if(lv_event_get_code(event)!=LV_EVENT_DRAW_MAIN)return;
 auto* obj=static_cast<lv_obj_t*>(lv_event_get_target(event));
 lv_area_t bounds{};lv_obj_get_coords(obj,&bounds);
 auto* layer=lv_event_get_layer(event);
 lv_draw_rect_dsc_t ink;lv_draw_rect_dsc_init(&ink);
 ink.bg_color=d->black?lv_color_black():lv_color_white();ink.bg_opa=LV_OPA_COVER;
 const int cell=d->size;
 for(size_t n=0;n<d->text.size();++n)for(int row=0;row<7;++row)for(int col=0;col<5;++col){
   if(!(ui_demo::DotRow(d->text.c_str()[n],row)&(1<<(4-col))))continue;
   const int x=bounds.x1+int(n)*6*cell+col*cell,y=bounds.y1+row*cell;
   lv_area_t a{x,y,x+cell-2,y+cell-2};lv_draw_rect(layer,&ink,&a);
 }
}
bool Paint(const Frame& frame,bool full) {
 if (esp_lv_adapter_lock(2000)!=ESP_OK) return false;
 if (selftest::Visible()) { esp_lv_adapter_unlock(); return false; }
 auto* old=lv_screen_active();
 screen=lv_obj_create(nullptr); lv_obj_remove_style_all(screen);
 lv_obj_set_style_bg_color(screen,lv_color_white(),0);
 lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
 lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
 const Frame* drawing=&frame;
 if (!state->demo.visible && frame.view==View::Reader) {
    state->reader.Draw(state->fallback);
    drawing=&state->fallback;
 }
 const bool modernLab=state->demo.visible&&state->demo.page==ui_demo::Page::FontLab&&state->fontbench.enabled;
 if(modernLab&&(!state->demoPainted||state->paintedDemoPage!=ui_demo::Page::FontLab))state->fontbench.Enable();
 if(modernLab&&!state->fontbench.Paint(screen,LabFont(0,16),OriginalUiExact)){lv_obj_delete(screen);esp_lv_adapter_unlock();return false;}
 if(!modernLab) for (size_t i=0;i<drawing->drawCount;++i) {
    const auto& d=drawing->draws[i]; const auto b=clip(d.box); if (b.empty()) continue;
    if(d.kind==DrawKind::Texture||d.kind==DrawKind::Dashed){
        if(!paper::AddPattern(screen,d)){lv_obj_delete(screen);esp_lv_adapter_unlock();return false;}
        continue;
    }
    auto* obj=d.kind==DrawKind::Text ? lv_label_create(screen) : lv_obj_create(screen);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,b.x,b.y); lv_obj_set_size(obj,b.w,b.h);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    const auto color=d.black ? lv_color_black() : lv_color_white();
    if (d.kind==DrawKind::Text) {
        const auto* selectedFont=d.fontProfile>=0?LabFontForDevice(d.fontProfile,d.size):Font(d.size);
        lv_obj_set_style_text_font(obj,selectedFont?selectedFont:Font(18),0);
        lv_obj_set_style_text_color(obj,color,0);
        lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);
        lv_label_set_text(obj,selectedFont?(!strcmp(d.text.c_str(),"设备设置") ? "设备自检" : d.text.c_str()):"SAMPLE UNAVAILABLE");
    } else if(d.kind==DrawKind::Dots){
        auto* copy=new(std::nothrow) Draw(d);
        if(!copy){lv_obj_delete(screen);esp_lv_adapter_unlock();return false;}
        lv_obj_add_event_cb(obj,DotEvent,LV_EVENT_ALL,copy);
    } else if(d.kind==DrawKind::Panel || d.kind==DrawKind::PanelFill){
        lv_obj_set_style_radius(obj,d.radius,0);
        lv_obj_set_style_border_width(obj,d.stroke,0);lv_obj_set_style_border_color(obj,color,0);
        lv_obj_set_style_bg_color(obj,d.kind==DrawKind::PanelFill?color:lv_color_white(),0);
        lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    } else if (d.kind==DrawKind::Rect) {
        lv_obj_set_style_border_width(obj,1,0); lv_obj_set_style_border_color(obj,color,0);
    } else { lv_obj_set_style_bg_color(obj,color,0); lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0); }
 }
 lv_obj_add_flag(screen,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(screen,Click,LV_EVENT_CLICKED,nullptr);
 lv_screen_load(screen); if (old!=screen) lv_obj_delete(old);
 auto* display=LVAdapterDisplay::Instance();
 {
   std::lock_guard<std::mutex> lock(frameMutex);
   state->fontbenchPixelRevision=0;
   state->renderError=display ? (modernLab?display->RefreshFontBench(state->fontbench.session,full,state->fontbenchPixels.data(),state->fontbenchPixels.size()):display->RefreshDiagnostic(full)) : ESP_ERR_INVALID_STATE;
 }
 if(state->demo.visible && state->renderError==ESP_OK){
   std::lock_guard<std::mutex> lock(frameMutex);
   if(display->CopyDiagnosticFrame(state->uiPixels.data(),state->uiPixels.size())){
     ++state->uiPixelRevision;if(modernLab)state->fontbenchPixelRevision=state->uiPixelRevision;
   }
 }
 esp_lv_adapter_unlock(); return state->renderError==ESP_OK;
}
void Save() {
 auto& d=state->engine.desk(); if (!d.dirty()) return;
 size_t n=0; const auto rev=d.dataRevision();
 bool ok=d.encode(state->payload.data(),state->payload.size(),n) && state->journal.save(state->payload.data(),n);
 d.saved(rev,ok); state->lastSave=Now();
}
void Publish() {
 auto* p=cJSON_CreateObject(); auto& e=state->engine; auto& d=e.desk();
 cJSON_AddStringToObject(p,"app","InkDesk R2");
 auto* ui=cJSON_AddObjectToObject(p,"ui_demo");
 if(state->demo.visible&&state->demo.page==ui_demo::Page::FontLab&&state->fontbench.enabled)
     cJSON_AddItemToObject(ui,"fontbench",state->fontbench.Status(true));
 cJSON_AddBoolToObject(ui,"visible",state->demo.visible);
 cJSON_AddNumberToObject(ui,"page",int(state->demo.page));
 cJSON_AddNumberToObject(ui,"revision",state->demo.revision);
 cJSON_AddNumberToObject(ui,"rendered_revision",state->demoFrame.revision);
 cJSON_AddBoolToObject(ui,"pending",state->demo.dirty);
 cJSON_AddBoolToObject(ui,"controls",state->demo.controls);
 cJSON_AddBoolToObject(ui,"bookmark",state->demo.bookmarked);
 cJSON_AddNumberToObject(ui,"font",state->demo.font);
 cJSON_AddStringToObject(ui,"theme",state->demo.theme==0?"A-SourceHanSans-Medium":"B-LXGWWenKai-GB-Screen");
 cJSON_AddNumberToObject(ui,"reading_page",state->demo.readingPage);
 cJSON_AddNumberToObject(ui,"reading_pages",state->demo.PageCount());
 cJSON_AddNumberToObject(ui,"reading_offset",state->demo.ReadingOffset());
 cJSON_AddNumberToObject(ui,"gallery",state->demo.gallery);
 cJSON_AddNumberToObject(ui,"lab_page",state->demo.labPage);
 cJSON_AddNumberToObject(ui,"lab_page_count",paper::LabPageCount);
 cJSON_AddNumberToObject(ui,"lab_profile",state->demo.labProfile);
 cJSON_AddNumberToObject(ui,"lab_size",paper::LabSizes[state->demo.labSize]);
 cJSON_AddNumberToObject(ui,"lab_vote",state->demo.labVote);
 cJSON_AddNumberToObject(ui,"lab_feedback",state->demo.labFeedback);
 cJSON_AddNumberToObject(ui,"lab_saved",state->demo.labSaved);
 cJSON_AddBoolToObject(ui,"gallery_selected",state->demo.gallerySelected);
 cJSON_AddStringToObject(ui,"design_system",paper::kDesignVersion);
 cJSON_AddStringToObject(ui,"last_refresh",state->lastFull?"full":"fast");
 cJSON_AddStringToObject(ui,"gray_mode","spatial-1bit");
 cJSON_AddStringToObject(ui,"lab_font_modes","static-1bit,fontpack-2bpp,fontpack-4bpp-mask");
 cJSON_AddNumberToObject(ui,"tab",state->demo.tab);
 cJSON_AddNumberToObject(ui,"wallpaper",state->demo.wallpaper);
 cJSON_AddBoolToObject(ui,"sample_data",true);
 cJSON_AddBoolToObject(ui,"accepting_input",accepting);
 cJSON_AddNumberToObject(ui,"pixel_revision",state->uiPixelRevision);
 cJSON_AddBoolToObject(p,"active",active && !selftest::Visible());
 cJSON_AddNumberToObject(p,"view",int(d.view()));
 cJSON_AddNumberToObject(p,"revision",d.revision());
 cJSON_AddNumberToObject(p,"epoch",d.epoch());
 cJSON_AddNumberToObject(p,"notes",d.documents().noteCount);
 cJSON_AddNumberToObject(p,"todos",d.documents().todoCount);
 cJSON_AddBoolToObject(p,"dirty",d.dirty());
 cJSON_AddNumberToObject(p,"journal_sequence",state->journal.sequence());
 cJSON_AddBoolToObject(p,"writes_blocked",state->journal.writesBlocked());
 cJSON_AddBoolToObject(p,"refresh_fault",e.refresh().fault());
 cJSON_AddNumberToObject(p,"refresh_error",state->renderError);
 cJSON_AddNumberToObject(p,"frames",e.refresh().stats().completed);
 cJSON_AddNumberToObject(p,"unchanged",e.refresh().stats().unchanged);
 cJSON_AddNumberToObject(p,"input_rejected",rejected.load()+e.rejected());
 cJSON_AddStringToObject(p,"notice",d.notice());
 state->reader.Status(cJSON_AddObjectToObject(p,"reader"));
 std::lock_guard<std::mutex> lock(snapshotMutex);
 cJSON_Delete(snapshot); snapshot=p;
 cJSON_Delete(fontbenchSnapshot);fontbenchSnapshot=state->fontbench.Status();
}
void Export() {
 Save(); auto& d=state->engine.desk();
 if (d.dirty()) return;
 mkdir("/sdcard/inkdesk/export",0755); bool ok=true;
 const auto& docs=d.documents();
 for (size_t i=0;i<docs.noteCount && ok;++i) {
    char path[64]; snprintf(path,sizeof(path),"/inkdesk/export/note-%02u.md",unsigned(i+1));
    const auto& t=docs.notes[i].body;
    ok=state->files.write(path,reinterpret_cast<const uint8_t*>(t.c_str()),t.size());
 }
 size_t n=0;
 for (size_t i=0;i<docs.todoCount;++i) {
    const auto& t=docs.todos[i]; const size_t need=t.text.size()+7;
    if (n+need>state->payload.size()) { ok=false; break; }
    memcpy(state->payload.data()+n,t.done ? "- [x] " : "- [ ] ",6); n+=6;
    memcpy(state->payload.data()+n,t.text.c_str(),t.text.size()); n+=t.text.size(); state->payload[n++]='\n';
 }
 if (ok) ok=state->files.write("/inkdesk/export/todo.md",state->payload.data(),n);
 d.message(ok ? "已导出到 inkdesk/export" : "导出失败，原文保留");
}
void Worker(void*) {
 size_t n=0; auto loaded=state->journal.load(state->payload.data(),state->payload.size(),n);
 if (loaded==IoStatus::Ok) {
    if (!state->engine.desk().decode(state->payload.data(),n)) {
        state->journal.blockWrites(); state->engine.desk().message("存档格式未知，本次只读");
    }
 } else if (loaded!=IoStatus::Missing) state->engine.desk().message("存档不可读，保留原文件");
 uint32_t publishAt=0;
 for (;;) {
    if (opening && !selftest::Busy()) {
        opening=false; selftest::Hide(); active=true;
        state->engine.boot(Now());
        state->demo.dirty=true;
    }
    if (selftest::Visible() || selftest::Busy()) {
        if (active.exchange(false)) Save();
        if (Now()-publishAt>500) { Publish(); publishAt=Now(); }
        vTaskDelay(pdMS_TO_TICKS(50)); continue;
    }
    InputRow r{};
    while (xQueueReceive(inputs,&r,0)==pdTRUE) {
        if (fontbenchInputLocked.load() && (r.kind==0 || r.kind==2)) continue;
        if (r.kind!=3 && r.epoch!=visibleEpoch) { ++rejected; continue; }
        auto& e=state->engine; auto& d=e.desk();
        if(r.kind==3){state->demo.Home();state->demo.dirty=true;accepting=false;++visibleEpoch;continue;}
        if(r.kind==4){state->demo.visible=true;state->demo.page=ui_demo::Page::FontLab;
            state->fontbench.OpenPage(static_cast<unsigned>(r.x));state->demo.dirty=true;++state->demo.revision;
            state->demo.refreshRequest=1;accepting=false;++visibleEpoch;continue;}
        if(r.kind==7){state->demo.visible=true;state->demo.page=ui_demo::Page::FontLab;
            state->fontbench.Apply(r.config);state->demo.dirty=true;++state->demo.revision;
            state->demo.refreshRequest=1;accepting=false;++visibleEpoch;continue;}
        if(r.kind==5){
            if(state->demo.visible&&state->demo.page==ui_demo::Page::FontLab){
                std::lock_guard<std::mutex> lock(labLogMutex);
                const auto action=state->fontbench.Control(r.x,r.y,personal_sdk::BootId(),Now());
                if(action!=paper::fontbench::Action::None){state->demo.dirty=true;++state->demo.revision;
                    state->demo.refreshRequest=state->fontbench.session.fullRequested()?1:2;
                    accepting=false;++visibleEpoch;}
            }
            continue;
        }
        if(r.kind==6){state->demo.visible=true;state->demo.page=ui_demo::Page::FontLab;
            state->fontbench.Open();state->demo.dirty=true;++state->demo.revision;
            state->demo.refreshRequest=1;accepting=false;++visibleEpoch;continue;}
        if(!state->demo.visible && d.view()==View::Home && r.kind==0 && r.y<58){
            state->demo.Home();accepting=false;++visibleEpoch;continue;
        }
        if(state->demo.visible){
            if(state->demo.dirty){++rejected;continue;}
            auto exit=ui_demo::Exit::None;
            // One input route. Old lab pages and OLD buttons are no longer reachable.
            if(state->demo.page==ui_demo::Page::FontLab){
                auto action=paper::fontbench::Action::None;
                if(r.kind==0){std::lock_guard<std::mutex> lock(labLogMutex);
                    action=state->fontbench.Tap(r.x,r.y,personal_sdk::BootId(),Now());}
                if(r.kind==2){const auto key=static_cast<Action>(r.x);
                    if(key==Action::Home||key==Action::Tools)action=paper::fontbench::Action::Exit;}
                if(action==paper::fontbench::Action::Exit)state->demo.Home();
                if(action!=paper::fontbench::Action::None){
                    state->demo.dirty=true;++state->demo.revision;
                    state->demo.refreshRequest=(action==paper::fontbench::Action::Exit||state->fontbench.session.fullRequested())?1:2;
                    accepting=false;++visibleEpoch;
                }
                continue;
            }
            const auto oldVote=state->demo.labVote;
            if(r.kind==0)exit=state->demo.Tap(state->demoFrame,r.x,r.y);
            if(state->demo.labVote!=oldVote){
                std::lock_guard<std::mutex> lock(labLogMutex);
                // Dedicated bounded append-only experiment log; never touches books.
                mkdir("/sdcard/inkdesk",0755);
                const char* path="/sdcard/inkdesk/font-lab.jsonl";
                struct stat st{};bool room=stat(path,&st)!=0||st.st_size<262144;
                FILE* log=room?fopen(path,"a"):nullptr;
                bool ok=false;
                if(log){int n=fprintf(log,"{\"schema\":1,\"build\":\"1.0.0-fontlab4-legacy\",\"boot_id\":\"%s\",\"uptime_ms\":%lu,\"revision\":%lu,\"profile\":%d,\"page\":%d,\"size\":%d,\"feedback\":%d,\"full_refresh\":%s}\n",
                  personal_sdk::BootId(),(unsigned long)Now(),(unsigned long)state->demoFrame.revision,state->demo.labProfile,state->demo.labPage,paper::LabSizes[state->demo.labSize],state->demo.labFeedback,state->lastFull?"true":"false");
                  ok=n>0;int close=fclose(log);ok=ok&&close==0;}
                state->demo.labSaved=ok?1:-1;
            }
            if(r.kind==2){
                const auto a=static_cast<Action>(r.x);
                if(a==Action::Home)state->demo.Home();
                if(a==Action::Next)state->demo.Step(1);
                if(a==Action::Previous)state->demo.Step(-1);
                if(a==Action::Tools)state->demo.Tools();
            }
            if(state->demo.dirty || exit!=ui_demo::Exit::None){accepting=false;++visibleEpoch;}
            if(exit!=ui_demo::Exit::None){
                auto a=exit==ui_demo::Exit::Reader?Action::Reader:exit==ui_demo::Exit::Notes?Action::Notes:
                       exit==ui_demo::Exit::Diagnostics?Action::Settings:Action::Home;
                e.enqueue({a,0,d.epoch(),d.composition()});e.pump(Now());
                e.refresh().request(Intent::Full,fullRect(),d.revision(),Now());
            }
            continue;
        }
        if (d.view()==View::Reader) {
            bool stay=true;
            if (r.kind==0) stay=state->reader.Tap(r.x,r.y);
            else if (r.kind==2 && static_cast<Action>(r.x)==Action::Next) state->reader.Next();
            else if (r.kind==2 && static_cast<Action>(r.x)==Action::Previous) state->reader.Previous();
            else if (r.kind==2 && static_cast<Action>(r.x)==Action::Home) stay=false;
            if (!stay) e.enqueue({Action::Home,0,d.epoch(),d.composition()});
            e.pump(Now());
            e.refresh().request(Intent::Probe,fullRect(),d.revision(),Now());
            continue;
        }
        if (r.kind==0) e.tap(r.x,r.y);
        else if (r.kind==1) e.key(char(r.x));
        else e.enqueue({static_cast<Action>(r.x),0,d.epoch(),d.composition()});
        e.pump(Now());
    }
    auto& e=state->engine; auto& d=e.desk(); e.pump(Now());
    if (d.takeReader()) { state->reader.Library(); }
    if (d.takeSettings()) {
        Save(); auto* q=cJSON_CreateObject(); auto* a=cJSON_CreateObject();
        selftest::Handle("selftest.open",q,a); cJSON_Delete(q); cJSON_Delete(a);
    }
    if (d.takeExport()) Export();
    if (d.saveDue(Now()) && Now()-state->lastSave>1500) Save();
    e.pump(Now());
    if(active && state->demo.visible && state->demo.dirty){
        accepting=false;state->demo.Draw(state->demoFrame);
        bool full=paper::NeedsFull(state->demoPainted,int(state->paintedDemoPage),int(state->demo.page),state->paintedDemoTheme,state->demo.theme,state->demoFast,state->demo.refreshRequest==1);
        if(state->demo.refreshRequest==2&&state->demoPainted&&state->paintedDemoPage==state->demo.page&&state->paintedDemoTheme==state->demo.theme)full=false;
        const bool modernLab=state->demo.page==ui_demo::Page::FontLab&&state->fontbench.enabled;
        bool ok=!state->demoFrame.overflow && Paint(state->demoFrame,full);
        if(modernLab){if(ok)state->fontbench.session.Painted(full,state->uiPixelRevision);else state->fontbench.session.PaintFailed();}
        if(ok){state->paintedDemoPage=state->demo.page;state->paintedDemoTheme=state->demo.theme;state->demoPainted=true;state->demoFast=full?0:state->demoFast+1;state->lastFull=full;}
        state->demo.refreshRequest=0;
        state->demo.dirty=false; // Failure is reported; retry requires explicit ui.open, not a flash loop.
        ++visibleEpoch;accepting=ok||modernLab;

    }
    if (active && !state->demo.visible && e.prepare(Now())) {
        accepting=false;
        bool ok=Paint(e.inflight(),e.job().mode!=RefreshMode::Fast);
        e.complete(Now(),ok); ++visibleEpoch;accepting=ok;
    }
    if (Now()-publishAt>500) { Publish(); publishAt=Now(); }
    vTaskDelay(pdMS_TO_TICKS(30));
 }
}
}
void Start() {
 auto* memory=heap_caps_malloc(sizeof(Runtime),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if (!memory) return;
 state=new(memory) Runtime;
 inputs=xQueueCreate(32,sizeof(InputRow));
 if (inputs) xTaskCreate(Worker,"inkdesk_r2",16384,nullptr,3,nullptr);
}
void Open() { opening=true; }
void Suspend() { opening=false; active=false; }
void Input(const char* name,bool pressed) {
 if (!pressed || !active || selftest::Visible()) return;
 Action a=Action::None;
 if (!strcmp(name,"vk_home")) a=Action::Home;
 if (!strcmp(name,"vk_next") || !strcmp(name,"volume_down")) a=Action::Next;
 if (!strcmp(name,"vk_prev") || !strcmp(name,"volume_up")) a=Action::Previous;
 if (!strcmp(name,"boot")) a=Action::Tools;
 if (a!=Action::None) Post({2,int(a),0});
}
bool Handle(const char* cmd,cJSON* req,cJSON* reply) {
 if (strncmp(cmd,"inkdesk.",8)) return false;
 if (!strcmp(cmd,"inkdesk.status")) {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    if (snapshot) { auto* p=cJSON_Duplicate(snapshot,true); cJSON_AddItemToObject(reply,"app",p); }
    else cJSON_AddStringToObject(reply,"error","app_starting");
 } else if ((!strcmp(cmd,"inkdesk.fontlab.log")||!strcmp(cmd,"inkdesk.fontlab4.log")||!strcmp(cmd,"inkdesk.fontbench.log"))) {
    auto* offset=cJSON_GetObjectItem(req,"offset");
    if(!cJSON_IsNumber(offset)||offset->valuedouble!=offset->valueint||offset->valueint<0||offset->valueint>262144){cJSON_AddStringToObject(reply,"error","invalid_offset");return true;}
    std::lock_guard<std::mutex> lock(labLogMutex);
    FILE* file=fopen(!strcmp(cmd,"inkdesk.fontbench.log")?"/sdcard/inkdesk/fontbench-ratings.jsonl":!strcmp(cmd,"inkdesk.fontlab4.log")?"/sdcard/inkdesk/font-lab4-ratings.jsonl":"/sdcard/inkdesk/font-lab.jsonl","rb");
    if(!file){cJSON_AddStringToObject(reply,"error","log_unavailable");return true;}
    if(fseek(file,offset->valueint,SEEK_SET)!=0){fclose(file);cJSON_AddStringToObject(reply,"error","seek_failed");return true;}
    unsigned char bytes[512];size_t n=fread(bytes,1,sizeof(bytes),file);bool ok=!ferror(file);fclose(file);
    if(!ok){cJSON_AddStringToObject(reply,"error","read_failed");return true;}
    static const char digits[]="0123456789abcdef";char hex[1025];
    for(size_t i=0;i<n;++i){hex[i*2]=digits[bytes[i]>>4];hex[i*2+1]=digits[bytes[i]&15];}hex[n*2]=0;
    cJSON_AddStringToObject(reply,"hex",hex);cJSON_AddNumberToObject(reply,"bytes",n);cJSON_AddNumberToObject(reply,"offset",offset->valueint);
 } else if (!strcmp(cmd,"inkdesk.fontbench.frame")) {
    auto* offset=cJSON_GetObjectItem(req,"offset");auto* length=cJSON_GetObjectItem(req,"length");auto* rev=cJSON_GetObjectItem(req,"revision");
    if(!state||!cJSON_IsNumber(offset)||!cJSON_IsNumber(length)||!cJSON_IsNumber(rev)||
       offset->valuedouble!=offset->valueint||length->valuedouble!=length->valueint||rev->valuedouble!=rev->valueint||
       offset->valueint<0||length->valueint<1||length->valueint>512||offset->valueint>96000-length->valueint){
       cJSON_AddStringToObject(reply,"error","invalid_frame_range");return true;
    }
    std::lock_guard<std::mutex> lock(frameMutex);
    if(!state->fontbenchPixelRevision||uint32_t(rev->valueint)!=state->fontbenchPixelRevision){cJSON_AddStringToObject(reply,"error","stale_frame");return true;}
    static const char digits[]="0123456789abcdef";char hex[1025];
    for(int i=0;i<length->valueint;++i){uint8_t b=state->fontbenchPixels[offset->valueint+i];hex[i*2]=digits[b>>4];hex[i*2+1]=digits[b&15];}hex[length->valueint*2]=0;
    cJSON_AddStringToObject(reply,"hex",hex);cJSON_AddNumberToObject(reply,"revision",state->fontbenchPixelRevision);
    cJSON_AddNumberToObject(reply,"width",800);cJSON_AddNumberToObject(reply,"height",480);cJSON_AddNumberToObject(reply,"bpp",2);
    cJSON_AddStringToObject(reply,"evidence","committed-target-buffer-not-optical-readback");
 } else if (!strcmp(cmd,"inkdesk.fontbench.config")) {
    InputRow row{};row.kind=7;
    if(!active||selftest::Visible()||!paper::fontbench::ParseConfig(req,row.config)||!Post(row))
        cJSON_AddStringToObject(reply,"error","invalid_or_busy_fontbench_config");
 } else if (!strcmp(cmd,"inkdesk.fontbench.state")) {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    if(fontbenchSnapshot){
      auto* snapshotCopy=cJSON_Duplicate(fontbenchSnapshot,true);
      cJSON_AddBoolToObject(snapshotCopy,"input_locked",fontbenchInputLocked.load());
      cJSON_AddItemToObject(reply,"fontbench",snapshotCopy);
    }
    else cJSON_AddStringToObject(reply,"error","app_starting");
 } else if (!strcmp(cmd,"inkdesk.fontbench.lock")) {
    auto* enabled=cJSON_GetObjectItem(req,"enabled");
    if(!cJSON_IsBool(enabled)) cJSON_AddStringToObject(reply,"error","invalid_fontbench_lock");
    else {
      fontbenchInputLocked.store(cJSON_IsTrue(enabled));
      cJSON_AddBoolToObject(reply,"input_locked",fontbenchInputLocked.load());
    }
 } else if (!strcmp(cmd,"inkdesk.fontbench.open")) {
    if(!active||selftest::Visible()||!accepting)cJSON_AddStringToObject(reply,"error","fontbench_busy");
    else Post({6,0,0});
 } else if (!strcmp(cmd,"inkdesk.fontbench.control")) {
    auto* field=cJSON_GetObjectItem(req,"field");auto* value=cJSON_GetObjectItem(req,"value");int index=-1;
    if(cJSON_IsString(field))for(int i=0;i<11;++i)if(!strcmp(field->valuestring,paper::fontbench::Fields[i]))index=i;
    if(!active||selftest::Visible()||!accepting||
       !cJSON_IsNumber(value)||value->valuedouble!=value->valueint||!paper::fontbench::Valid(index,value->valueint))
        cJSON_AddStringToObject(reply,"error","invalid_or_busy_fontbench_control");
    else Post({5,index,value->valueint});
 } else if (!strcmp(cmd,"inkdesk.fontlab4.page")) {
    auto* page=cJSON_GetObjectItem(req,"page");
    if(!active||selftest::Visible()||!accepting||!cJSON_IsNumber(page)||page->valuedouble!=page->valueint||page->valueint<0||page->valueint>=4096)
        cJSON_AddStringToObject(reply,"error","invalid_or_busy_fontlab_page");
    else Post({4,page->valueint,0});
 } else if (!strcmp(cmd,"inkdesk.open")) Open();
 else if(!strcmp(cmd,"inkdesk.ui.frame")) {
    auto* offset=cJSON_GetObjectItem(req,"offset");auto* length=cJSON_GetObjectItem(req,"length");
    auto* revision=cJSON_GetObjectItem(req,"revision");
    if(!state||!cJSON_IsNumber(offset)||!cJSON_IsNumber(length)||!cJSON_IsNumber(revision)||
       offset->valuedouble!=offset->valueint||length->valuedouble!=length->valueint||revision->valuedouble!=revision->valueint||
       offset->valueint<0||length->valueint<1||length->valueint>512||offset->valueint>48000-length->valueint){
       cJSON_AddStringToObject(reply,"error","invalid_frame_range");
    }else{
       std::lock_guard<std::mutex> lock(frameMutex);
       if(!state->uiPixelRevision||uint32_t(revision->valueint)!=state->uiPixelRevision)cJSON_AddStringToObject(reply,"error","stale_frame");
       else{
          static const char hex[]="0123456789abcdef";std::string out;out.reserve(length->valueint*2);
          for(int i=0;i<length->valueint;++i){uint8_t b=state->uiPixels[offset->valueint+i];out+=hex[b>>4];out+=hex[b&15];}
          cJSON_AddStringToObject(reply,"hex",out.c_str());cJSON_AddNumberToObject(reply,"revision",state->uiPixelRevision);
          cJSON_AddNumberToObject(reply,"width",800);cJSON_AddNumberToObject(reply,"height",480);
       }
    }
 }
 else if(!strcmp(cmd,"inkdesk.ui.open")) { Open();Post({3,0,0}); }
 else if (!strcmp(cmd,"inkdesk.tap")) {
    auto* x=cJSON_GetObjectItem(req,"x"); auto* y=cJSON_GetObjectItem(req,"y");
    if (!active || selftest::Visible() || !cJSON_IsNumber(x) || !cJSON_IsNumber(y) ||
        x->valuedouble!=x->valueint || y->valuedouble!=y->valueint || x->valueint<0 || x->valueint>=480 || y->valueint<0 || y->valueint>=800)
        cJSON_AddStringToObject(reply,"error","invalid_or_inactive_tap");
    else Post({0,x->valueint,y->valueint});
 } else if (!strcmp(cmd,"inkdesk.key")) {
    auto* key=cJSON_GetObjectItem(req,"key");
    if (!active || selftest::Visible() || !cJSON_IsString(key) || strlen(key->valuestring)!=1)
        cJSON_AddStringToObject(reply,"error","invalid_or_inactive_key");
    else Post({1,int(static_cast<unsigned char>(key->valuestring[0])),0});
 } else cJSON_AddStringToObject(reply,"error","unknown_inkdesk_command");
 return true;
}
}
