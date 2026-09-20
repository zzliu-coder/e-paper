// Candidate replacement of the legacy inkdesk_app.cc entry points.
// Does not initialize a second screen driver, SD controller, Wi-Fi or audio stack.
#include "inkdesk_app.h"
#include "paper/runtime.hpp"
#include "metalio_hardware.hpp"
#include "maintenance.hpp"
#include "usb_virtual_disk.h"
#include "network_service.hpp"
#include "rescue_fonts.h"
#include "paper/generated/ui_tokens.hpp"
#include "display/lv_adapter_display.h"
#include "esp_lv_adapter.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "power_policy.h"
#include "haptic_feedback.h"
#include "selftest.h"
#include "cJSON.h"
#include <atomic>
#include <cstring>
#include <cmath>
#include <mutex>
#include <new>
namespace {
    struct Input {
        int kind=0,x=0,y=0;
        uint64_t revision=0;
        int64_t queuedUs=0;
        char action[48] {
        };
        char value[193] {
        };
        char token[33]{};
    };
    struct ImageState {
        lv_image_dsc_t desc {
        };
        uint8_t* bytes=nullptr;
        ~ImageState() {
            if(bytes)heap_caps_free(bytes);
        }
    };
    paper::MetalioHardware hardware;
    paper::Runtime* runtime=nullptr;
    QueueHandle_t queue=nullptr;
    std::atomic<bool>active {
        false
    };
    std::atomic<uint64_t>displayed {
        0
    };
    std::mutex snapshotMutex;
    std::mutex keyMapMutex;
    uint64_t keyEpoch=0;
    std::vector<paper::Hit> keyHits;
    std::string statusJson="{\"phase\":\"starting\"}";
    std::string lastAction,lastActionError,lastToken,publishedToken;
    std::atomic<uint64_t> rejectedInputs{0},completedInputs{0};
    std::atomic<int64_t> queueUs{0},actionUs{0},presentUs{0};
    std::atomic<bool> loadingBusy{false},loadingCancel{false};
    std::atomic<size_t> loadingDone{0},loadingTotal{0};
    std::atomic<int64_t> loadingDisplayUs{0};
    std::atomic<unsigned> loadingFrames{0};
    std::atomic<const char*> loadingStage{"font_validation"}; // static stage literals only
    bool observeLoading=false; // worker-owned; callbacks never read runtime/SD fonts
    int64_t loadingStarted=0,lastLoadingFrame=0;
    void paintLoading(size_t done,size_t total) {
        if(selftest::Visible()||selftest::Busy()||esp_lv_adapter_lock(2000)!=ESP_OK)return;
        if(selftest::Visible()||selftest::Busy()){esp_lv_adapter_unlock();return;}
        const int64_t started=esp_timer_get_time();
        auto*screen=lv_obj_create(nullptr);
        lv_obj_remove_style_all(screen);
        lv_obj_set_style_bg_color(screen,lv_color_white(),0);
        lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
        lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
        namespace token=paper::ui::token;
        auto label=[&](const char*text,int y,const lv_font_t*font){
            auto*obj=lv_label_create(screen);lv_obj_set_style_text_font(obj,font,0);
            lv_obj_set_style_text_color(obj,lv_color_black(),0);lv_label_set_text(obj,text);
            lv_obj_set_pos(obj,token::Margin,y);return obj;
        };
        const char*stage=loadingStage.load();
        const char*title=!strcmp(stage,"book_hash")||!strcmp(stage,"cache_verify")?"正在检查书籍":!strcmp(stage,"archive_index")||!strcmp(stage,"metadata")||!strcmp(stage,"txt_index")?"正在读取目录":!strcmp(stage,"resource_read")?"正在读取章节":!strcmp(stage,"decompress")?"正在解压章节":!strcmp(stage,"parse")||!strcmp(stage,"pagination")||!strcmp(stage,"search")?"正在整理排版":"正在准备字体";
        label(title,180,&paper_rescue_32);
        char text[96];
        if(total)snprintf(text,sizeof text,"当前步骤 %u%%",unsigned(std::min(done,total)*100/total));
        else snprintf(text,sizeof text,"正在处理，请稍候");
        label(text,248,&paper_rescue_22);
        auto*bar=lv_bar_create(screen);lv_obj_set_pos(bar,token::Margin,310);
        lv_obj_set_size(bar,token::ContentWidth,20);
        lv_obj_set_style_bg_color(bar,lv_color_white(),LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,LV_PART_MAIN);
        lv_obj_set_style_border_width(bar,token::Outline,LV_PART_MAIN);
        lv_obj_set_style_border_color(bar,lv_color_black(),LV_PART_MAIN);
        lv_obj_set_style_radius(bar,0,LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar,lv_color_black(),LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar,0,LV_PART_INDICATOR);
        lv_bar_set_value(bar,int(total?done*100/total:0),LV_ANIM_OFF);
        label("准备完成后显示页面",360,&paper_rescue_18);
        auto*b=lv_button_create(screen);lv_obj_set_pos(b,token::Margin,620);
        lv_obj_set_size(b,token::ContentWidth,token::ButtonHeight);
        lv_obj_set_style_radius(b,token::ButtonRadius,0);
        lv_obj_set_style_bg_color(b,lv_color_white(),0);lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);
        lv_obj_set_style_border_width(b,token::Outline,0);lv_obj_set_style_border_color(b,lv_color_black(),0);
        lv_obj_set_style_shadow_width(b,0,0);
        auto*t=lv_label_create(b);lv_obj_set_style_text_font(t,&paper_rescue_22,0);
        lv_obj_set_style_text_color(t,lv_color_black(),0);lv_label_set_text(t,"取消并返回首页");lv_obj_center(t);
        lv_obj_add_event_cb(b,[](lv_event_t*){loadingCancel=true;},LV_EVENT_CLICKED,nullptr);
        auto*old=lv_screen_active();lv_screen_load(screen);if(old!=screen)lv_obj_delete(old);
        auto*d=LVAdapterDisplay::Instance();
        auto result=d?d->RefreshDiagnostic(false,true):ESP_ERR_INVALID_STATE;
        esp_lv_adapter_unlock();
        loadingDisplayUs+=esp_timer_get_time()-started;
        if(result==ESP_OK)++loadingFrames;
    }
    paper::Status workProgress(const char*stage,size_t done,size_t total){
        if(!observeLoading)return {};
        loadingStage=stage;
        loadingBusy=true;loadingDone=done;loadingTotal=total;
        if(loadingCancel)return paper::Status::fail(paper::Error::Canceled,"已取消准备");
        const auto now=esp_timer_get_time();
        // Intermediate refresh itself costs ~1 s. Do not add a progress frame
        // to a warm sub-second load. Long imports still show timed progress.
        const bool largeImport=total>16*1024*1024;
        if(now-loadingStarted>=1000000&&loadingFrames.load()<(largeImport?32u:2u)&&
           (!lastLoadingFrame||now-lastLoadingFrame>=10000000)&&(!total||done<total)){
            paintLoading(done,total);lastLoadingFrame=esp_timer_get_time();
        }
        if(loadingCancel)return paper::Status::fail(paper::Error::Canceled,"已取消本次准备");
        return {};
    }
    paper::Status fontProgress(size_t done,size_t total){return workProgress("font_validation",done,total);}
    // Publish receipts with the frame they acknowledge, never with an older frame.
    std::string publishedAction,publishedError;
    std::string displayReport="{}",publishedDisplayReport="{}";
    uint64_t publishedCompleted=0;
    int64_t publishedQueueUs=0,publishedActionUs=0,publishedPresentUs=0;
    void publish() {
        auto s=runtime->snapshot();
        std::lock_guard<std::mutex>l(snapshotMutex);
        statusJson=std::move(s);
        publishedAction=lastAction;
        publishedToken=lastToken;
        publishedError=lastActionError;
        publishedCompleted=completedInputs.load();
        publishedQueueUs=queueUs.load();
        publishedActionUs=actionUs.load();
        publishedPresentUs=presentUs.load();
        publishedDisplayReport=displayReport;
    }
    bool post(const Input&in) {
        if(loadingBusy){
            if(in.kind==0&&!strcmp(in.action,"home")){loadingCancel=true;return true;}
            if(in.kind==2)loadingCancel=true;
            else{++rejectedInputs;return false;}
        }
        auto stamped=in;stamped.queuedUs=esp_timer_get_time();
        const bool ok=queue&&xQueueSend(queue,&stamped,0)==pdTRUE;
        if(!ok)++rejectedInputs;
        return ok;
    }
    void resolveKey(Input&cmd){
        std::lock_guard<std::mutex> lock(keyMapMutex);
        if(keyEpoch)for(const auto&h:keyHits)if(h.enabled&&h.box.contains(cmd.x,cmd.y)){
            if(h.action=="key"||h.action=="delete"||h.action=="cursor"||h.action=="literal"){
                cmd.kind=4;cmd.revision=keyEpoch;
                strncpy(cmd.action,h.action.c_str(),sizeof(cmd.action)-1);
                strncpy(cmd.value,h.value.c_str(),sizeof(cmd.value)-1);
            }
            break;
        }
    }
    void click(lv_event_t*e) {
        if(lv_event_get_code(e)!=LV_EVENT_CLICKED||!active)return;
        auto*i=lv_indev_active();
        if(!i)return;
        lv_point_t p {
        };
        lv_indev_get_point(i,&p);
        Input cmd;
        cmd.kind=1;
        cmd.x=p.x;
        cmd.y=p.y;
        cmd.revision=displayed.load();
        resolveKey(cmd);
        // Acknowledge an accepted physical keyboard tap before rendering. This
        // screen has no HapticAttachClick zones; remote taps remain silent.
        // The board pulse is timer-driven, so it never sleeps on the UI task.
        if(post(cmd)&&cmd.kind==4)HapticPulseIfEnabled();
    }
    paper::Status present() {
        paper::DisplayJob job;
        auto st=runtime->job(job);
        if(!st)return st;
        if(esp_lv_adapter_lock(2000)!=ESP_OK)return paper::Status::fail(paper::Error::Busy,"LVGL锁超时");
        if(selftest::Visible()||selftest::Busy()) {
            esp_lv_adapter_unlock();
            return paper::Status::fail(paper::Error::Busy,"设备维护页面正在使用显示");
        }
        auto* image=new(std::nothrow)ImageState;
        if(!image) {
            esp_lv_adapter_unlock();
            return paper::Status::fail(paper::Error::Unavailable,"显示内存不足");
        }
        image->bytes=(uint8_t*)heap_caps_malloc(48008,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!image->bytes) {
            delete image;
            esp_lv_adapter_unlock();
            return paper::Status::fail(paper::Error::Unavailable,"PSRAM显示缓冲不足");
        }
        const uint8_t palette[]= {
            0,0,0,255,255,255,255,255
        };
        memcpy(image->bytes,palette,8);
        memset(image->bytes+8,255,48000);
        auto&pixels=job.frame.bytes();
        for(size_t i=0;i<384000;++i) {
            auto v=(pixels[i/4]>>(6-2*(i%4)))&3;
            if(v<2)image->bytes[8+i/8]&=uint8_t(~(128>>(i&7)));
        }
        image->desc.header.magic=LV_IMAGE_HEADER_MAGIC;
        image->desc.header.cf=LV_COLOR_FORMAT_I1;
        image->desc.header.w=480;
        image->desc.header.h=800;
        image->desc.header.stride=60;
        image->desc.data=image->bytes;
        image->desc.data_size=48008;
        auto* old=lv_screen_active();
        auto*screen=lv_obj_create(nullptr);
        lv_obj_remove_style_all(screen);
        lv_obj_set_style_bg_color(screen,lv_color_white(),0);
        lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
        lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
        auto*obj=lv_image_create(screen);
        lv_image_set_src(obj,&image->desc);
        lv_obj_set_pos(obj,0,0);
        lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj,[](lv_event_t*e){delete static_cast<ImageState*>(lv_event_get_user_data(e));},LV_EVENT_DELETE,image);
        // Never leave users with unexplained empty text when no local MiSans pack exists.
        auto* state=cJSON_Parse(runtime->snapshot().c_str());
        auto*fontError=state?cJSON_GetObjectItem(state,"font_error"):nullptr;
        const bool rescue=(cJSON_IsString(fontError)&&fontError->valuestring[0])||paper_maintenance::UsbOwned();
        if(rescue) {
            // Same PAPER tokens, with an embedded MiSans subset; never reads SD.
            namespace token=paper::ui::token;
            lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
            auto*heading=lv_label_create(screen);
            lv_obj_set_style_text_font(heading,&paper_rescue_32,0);
            lv_obj_set_style_text_color(heading,lv_color_black(),0);
            lv_label_set_text(heading,"开发与维护");lv_obj_set_pos(heading,token::Margin,24);
            auto*label=lv_label_create(screen);
            lv_obj_set_style_text_font(label,&paper_rescue_18,0);
            lv_obj_set_style_text_color(label,lv_color_black(),0);
            lv_obj_set_style_bg_color(label,lv_color_white(),0);
            lv_obj_set_style_bg_opa(label,LV_OPA_COVER,0);
            lv_obj_set_size(label,token::ContentWidth,160);
            lv_obj_set_pos(label,token::Margin,80);
            std::string message=paper_maintenance::Text()+"\n\n"+(paper_maintenance::UsbOwned()?"请在电脑上安全弹出磁盘。\n设备将自动接回存储卡并恢复调试。":"存储卡字体资源尚未就绪。\n请通过 USB 导入字体，再检查资源。");
            lv_label_set_text(label,message.c_str());
            const char*names[]={"USB 磁盘 / 导入文件","检查字体与存储资源","检查固件更新包","安装已检查的固件","开发保持唤醒 / 切换","返回首页"};
            const char*actions[]={"maintenance-usb","maintenance-resources","maintenance-check","maintenance-install","maintenance-awake","home"};
            for(int k=0;k<6;++k){auto*b=lv_button_create(screen);lv_obj_set_pos(b,token::Margin,248+k*76);lv_obj_set_size(b,token::ContentWidth,token::ButtonHeight);lv_obj_set_style_radius(b,token::ButtonRadius,0);lv_obj_set_style_bg_color(b,lv_color_white(),0);lv_obj_set_style_border_color(b,lv_color_black(),0);lv_obj_set_style_border_width(b,2,0);lv_obj_set_style_shadow_width(b,0,0);auto*t=lv_label_create(b);lv_obj_set_style_text_font(t,&paper_rescue_22,0);lv_obj_set_style_text_color(t,lv_color_black(),0);lv_label_set_text(t,names[k]);lv_obj_center(t);lv_obj_add_event_cb(b,[](lv_event_t*e){::Input i;strncpy(i.action,(const char*)lv_event_get_user_data(e),sizeof i.action-1);post(i);},LV_EVENT_CLICKED,(void*)actions[k]);}
        }
        cJSON_Delete(state);
        lv_obj_add_flag(screen,LV_OBJ_FLAG_CLICKABLE);
        if(!rescue)lv_obj_add_event_cb(screen,click,LV_EVENT_CLICKED,nullptr);
        lv_screen_load(screen);
        if(old!=screen)lv_obj_delete(old);
        auto*d=LVAdapterDisplay::Instance();
        displayReport="{\"submitted\":false}";
        auto error=d?(rescue?d->RefreshDiagnostic():d->RefreshPaper(job.frame.bytes().data(),job.frame.bytes().size(),job.full,job.gray,&displayReport)):ESP_ERR_INVALID_STATE;
        if(lv_screen_active()!=screen)error=ESP_ERR_INVALID_STATE;
        esp_lv_adapter_unlock();
        st=error==ESP_OK?paper::Status {
        }
        :paper::Status::fail(paper::Error::BackendFailure,"墨水屏提交失败");
        runtime->complete(job.revision,st);
        if(st){displayed=job.revision;std::lock_guard<std::mutex> lock(keyMapMutex);keyEpoch=job.inputEpoch;keyHits=job.hits;}
        return st;
    }
    void worker(void*) {
        selftest::Hide();
        runtime=new(std::nothrow)paper::Runtime("/sdcard",hardware);
        if(!runtime) {
            vTaskDelete(nullptr);
            return;
        }
        paper_maintenance::Init(runtime);
        hardware.bindAction(paper_maintenance::Action);
        hardware.bind("maintenance_status",{[](std::string&v){v=paper_maintenance::Text();return paper::Status{};},{}});
        runtime->resources().setActivityObserver([](bool awake){static bool held=false;if(awake&&!held){PowerPolicy::GetInstance().Acquire(PowerNeed::StandbyInhibit);held=true;}else if(!awake&&held){PowerPolicy::GetInstance().Release(PowerNeed::StandbyInhibit);held=false;}});
        runtime->initialize();
        runtime->setFontProgressObserver(fontProgress);
        runtime->setDocumentProgressObserver(workProgress);
        active=true;
        if(present())paper_maintenance::AcceptBoot();
        publish();
        Input cmd;
        for(;;) {
            if(paper_maintenance::Tick()){present();publish();}
            if(xQueueReceive(queue,&cmd,pdMS_TO_TICKS(250))!=pdTRUE){
                if(active&&!selftest::Visible()&&!selftest::Busy()&&runtime->pollNetwork()){present();publish();}
                if(active&&!selftest::Visible()&&!selftest::Busy()&&!paper_maintenance::UsbOwned()){
                    const auto until=esp_timer_get_time()+8000;
                    if(runtime->prepareIdle([&](){return uxQueueMessagesWaiting(queue)>0||esp_timer_get_time()>=until;}))publish();
                }
                continue;
            }
            auto started=esp_timer_get_time();queueUs=started-cmd.queuedUs;
            if(cmd.kind==2) {
                auto suspended=runtime->suspend();
                if(!suspended){publish();continue;}
                if(esp_lv_adapter_lock(2000)==ESP_OK) {
                    if(auto*d=LVAdapterDisplay::Instance())d->RecoverPaper();
                    esp_lv_adapter_unlock();
                }
                active=false;
                publish();
                continue;
            }
            PowerNeedHold hold(PowerNeed::StandbyInhibit);
            PowerPolicy::GetInstance().NotifyUserActivity();
            loadingCancel=false;loadingDone=0;loadingTotal=0;loadingFrames=0;loadingDisplayUs=0;
            loadingStarted=esp_timer_get_time();lastLoadingFrame=0;observeLoading=true;
            if(cmd.kind==3) {
                selftest::Hide();
                runtime->resume();
                active=true;
            }
            else if(!active){observeLoading=false;continue;}
            else if(cmd.kind==4){
                std::vector<std::pair<std::string,std::string>> keys{{cmd.action,cmd.value}};
                ::Input next;
                while(keys.size()<32&&xQueuePeek(queue,&next,0)==pdTRUE&&next.kind==4&&next.revision==cmd.revision){
                    if(xQueueReceive(queue,&next,0)!=pdTRUE)break;
                    keys.emplace_back(next.action,next.value);
                }
                auto result=runtime->inputBatch(cmd.revision,keys);
                std::lock_guard<std::mutex> l(snapshotMutex);lastAction="keyboard-batch";lastActionError=result?"":result.message;
            }
            else if(cmd.kind==1){auto result=runtime->tap(cmd.x,cmd.y,cmd.revision);std::lock_guard<std::mutex>l(snapshotMutex);lastAction="tap";lastActionError=result?"":result.message;}
            else {
                auto result=runtime->action(cmd.action,cmd.value);
                {std::lock_guard<std::mutex>l(snapshotMutex);lastAction=cmd.action;lastActionError=result?"":result.message;}
            }
            observeLoading=false;
            {std::lock_guard<std::mutex>l(snapshotMutex);lastToken=cmd.token;}
            if(loadingCancel)runtime->action("home");
            loadingBusy=false;
            actionUs=esp_timer_get_time()-started;
            started=esp_timer_get_time();present();presentUs=esp_timer_get_time()-started;
            ++completedInputs;
            publish();
        }
    }
    bool number(cJSON*obj,const char*key,int&value,int min,int max) {
        auto*x=cJSON_GetObjectItem(obj,key);
        if(!cJSON_IsNumber(x)||!std::isfinite(x->valuedouble)||x->valuedouble<x->valueint||x->valuedouble!=x->valueint||x->valueint<min||x->valueint>max)return false;
        value=x->valueint;
        return true;
    }
    void error(cJSON*reply,const char*m) {
        cJSON_AddStringToObject(reply,"error",m);
    }
}
namespace inkdesk_app {
    bool KeyboardTouchDown(int x,int y) {
        if(!active)return false;
        ::Input cmd;cmd.kind=1;cmd.x=x;cmd.y=y;
        resolveKey(cmd);
        if(cmd.kind!=4)return false;
        if(post(cmd))HapticPulseIfEnabled();
        return true;
    }
    void Start() {
        if(queue)return;
        queue=xQueueCreate(32,sizeof(::Input));
        if(queue)xTaskCreate(worker,"paper_core",16384,nullptr,3,nullptr);
    }
    void Open() {
        ::Input i;
        i.kind=3;
        post(i);
    }
    void Suspend() {
        ::Input i;
        i.kind=2;
        post(i);
    }
    void Input(const char*name,bool pressed) {
        if(!pressed||!active)return;
        ::Input i;
        if(!strcmp(name,"vk_next")||!strcmp(name,"volume_down"))strcpy(i.action,"next");
        else if(!strcmp(name,"vk_prev")||!strcmp(name,"volume_up"))strcpy(i.action,"prev");
        else if(!strcmp(name,"vk_home"))strcpy(i.action,"home");
        else if(!strcmp(name,"boot"))strcpy(i.action,"settings");
        else return;
        post(i);
    }
    bool Handle(const char*cmd,cJSON*req,cJSON*reply) {
        if(!strcmp(cmd,"paper.bluetooth")){
            auto state=hardware.bluetooth();auto*bt=cJSON_AddObjectToObject(reply,"bluetooth");
            cJSON_AddBoolToObject(bt,"available",state.available);cJSON_AddBoolToObject(bt,"busy",state.busy);
            cJSON_AddBoolToObject(bt,"connected",state.connected);cJSON_AddNumberToObject(bt,"mode",state.mode);
            cJSON_AddStringToObject(bt,"message",state.message.c_str());cJSON_AddStringToObject(bt,"address",state.address.c_str());
            cJSON_AddBoolToObject(bt,"connection_known",state.connectionKnown);
            cJSON_AddBoolToObject(bt,"rail_held",state.railHeld);cJSON_AddBoolToObject(bt,"rail_on",state.railOn);
            cJSON_AddNumberToObject(bt,"rx_bytes",state.rxBytes);cJSON_AddNumberToObject(bt,"last_rx_ms",state.lastRxMs);
            auto*raw=cJSON_AddArrayToObject(bt,"recent_replies");for(const auto&line:state.recentReplies)cJSON_AddItemToArray(raw,cJSON_CreateString(line.c_str()));
            auto*devices=cJSON_AddArrayToObject(bt,"devices");
            for(auto&d:state.devices){auto*item=cJSON_CreateObject();cJSON_AddStringToObject(item,"address",d.address.c_str());cJSON_AddStringToObject(item,"name",d.name.c_str());cJSON_AddItemToArray(devices,item);}
            return true;
        }
        if(!strcmp(cmd,"paper.network")){
            auto state=paper_network::Snapshot();auto*net=cJSON_AddObjectToObject(reply,"network");
            auto saved=paper_network::SavedSsid();cJSON_AddStringToObject(net,"saved_ssid",saved.c_str());
            cJSON_AddStringToObject(net,"connected_ssid",state.ssid.c_str());
            cJSON_AddBoolToObject(net,"saved_visible",!saved.empty()&&std::any_of(state.aps.begin(),state.aps.end(),[&](const auto&a){return a.ssid==saved;}));
            auto*aps=cJSON_AddArrayToObject(net,"networks");
            for(size_t n=0;n<state.aps.size()&&n<12;++n){auto*ap=cJSON_CreateObject();cJSON_AddStringToObject(ap,"ssid",state.aps[n].ssid.c_str());cJSON_AddNumberToObject(ap,"signal",state.aps[n].signal);cJSON_AddBoolToObject(ap,"secured",state.aps[n].secured);cJSON_AddItemToArray(aps,ap);}
            cJSON_AddBoolToObject(net,"busy",state.busy);cJSON_AddBoolToObject(net,"connected",state.connected);
            cJSON_AddStringToObject(net,"message",state.message.c_str());cJSON_AddStringToObject(net,"ip",state.ip.c_str());
            cJSON_AddNumberToObject(net,"access_points",state.aps.size());return true;
        }
        if((selftest::Busy()&&(!strcmp(cmd,"storage.usb")||!strcmp(cmd,"paper.command")||!strcmp(cmd,"paper.transfer")))||
           (paper_maintenance::UsbOwned()&&(!strncmp(cmd,"sd.",3)||!strncmp(cmd,"selftest.",9)))){error(reply,"storage_or_selftest_busy");return true;}
        if(!strcmp(cmd,"storage.usb")){
            auto*enabled=cJSON_GetObjectItem(req,"enabled");
            if(!cJSON_IsBool(enabled)){error(reply,"invalid_enabled");return true;}
            ::Input i;strcpy(i.action,cJSON_IsTrue(enabled)?"maintenance-usb":"maintenance-usb-exit");
            if(!post(i))error(reply,"queue_full");else cJSON_AddStringToObject(reply,"state","queued");return true;
        }
        if(!strcmp(cmd,"paper.maintenance")){
            auto*s=cJSON_Parse(paper_maintenance::Json().c_str());if(s){
                auto&disk=UsbVirtualDisk::GetInstance();
                cJSON_AddNumberToObject(s,"usb_stage",disk.SwitchStage());
                cJSON_AddNumberToObject(s,"usb_previous_boot_stage",disk.PreviousBootStage());
                cJSON_AddBoolToObject(s,"usb_busy",disk.IsBusy());
                cJSON_AddItemToObject(reply,"maintenance",s);
            }return true;
        }
        if(!strcmp(cmd,"paper.file")){
            if(!runtime){error(reply,"starting");return true;}
            auto*path=cJSON_GetObjectItem(req,"path"),*op=cJSON_GetObjectItem(req,"op");
            if(!cJSON_IsString(path)||!cJSON_IsString(op)){error(reply,"invalid_file_request");return true;}
            std::string relative=path->valuestring;
            if(relative!="books"&&relative!="paper"&&relative!="exports"&&relative.rfind("books/",0)!=0&&relative.rfind("paper/",0)!=0&&relative.rfind("exports/",0)!=0){error(reply,"file_path_not_allowed");return true;}
            paper::Status st;
            if(!strcmp(op->valuestring,"hash")){std::string sha;st=runtime->store().hash(relative,sha);if(st)cJSON_AddStringToObject(reply,"sha256",sha.c_str());}
            else if(!strcmp(op->valuestring,"list")){
                std::vector<paper::FileEntry> entries;st=runtime->store().list(relative,entries,1024);
                if(st){int offset=0;if(cJSON_GetObjectItem(req,"offset")&&!number(req,"offset",offset,0,1024)){error(reply,"invalid_offset");return true;}auto*a=cJSON_AddArrayToObject(reply,"files");for(size_t i=offset;i<entries.size()&&i<size_t(offset+12);++i){auto*e=cJSON_CreateObject();cJSON_AddStringToObject(e,"name",entries[i].name.c_str());cJSON_AddNumberToObject(e,"size",entries[i].size);cJSON_AddBoolToObject(e,"directory",entries[i].directory);cJSON_AddItemToArray(a,e);}cJSON_AddNumberToObject(reply,"total",entries.size());}
            }else st=paper::Status::fail(paper::Error::Unsupported,"unsupported_file_operation");
            if(!st)error(reply,paper::errorName(st.code));
            return true;
        }
        if(!strncmp(cmd,"book.",5)) {
            error(reply,"legacy_transfer_disabled_use_paper_transfer");
            return true;
        }
        if(strncmp(cmd,"paper.",6)&&strncmp(cmd,"inkdesk.",8))return false;
        if(!strcmp(cmd,"paper.status")||!strcmp(cmd,"inkdesk.status")) {
            std::lock_guard<std::mutex>l(snapshotMutex);
            auto*s=cJSON_Parse(statusJson.c_str());
            if(s) {
                cJSON_DeleteItemFromObject(s,"hits");
                cJSON_DeleteItemFromObject(s,"input");
                // Discovery can contain 75+ font faces. Keep health receipts
                // bounded independently of how many SD fonts are installed.
                if(auto*r=cJSON_GetObjectItem(s,"reader")){
                    auto*f=cJSON_GetObjectItem(r,"families");
                    cJSON_AddNumberToObject(r,"family_count",cJSON_GetArraySize(f));
                    cJSON_DeleteItemFromObject(r,"families");
                }
                cJSON_AddItemToObject(reply,"app",s);
                cJSON_AddStringToObject(reply,"last_action",publishedAction.c_str());
                cJSON_AddNumberToObject(reply,"receipt_token_version",1);
                cJSON_AddStringToObject(reply,"last_request_token",publishedToken.c_str());
                cJSON_AddStringToObject(reply,"last_action_error",publishedError.c_str());
                auto*p=cJSON_AddObjectToObject(reply,"performance");
                cJSON_AddNumberToObject(p,"completed",publishedCompleted);
                cJSON_AddNumberToObject(p,"rejected_inputs",rejectedInputs.load());
                cJSON_AddNumberToObject(p,"queue_us",publishedQueueUs);
                cJSON_AddNumberToObject(p,"action_us",publishedActionUs);
                cJSON_AddNumberToObject(p,"present_us",publishedPresentUs);
                if(auto*display=cJSON_Parse(publishedDisplayReport.c_str()))cJSON_AddItemToObject(reply,"display",display);
                auto*loading=cJSON_AddObjectToObject(reply,"loading");
                cJSON_AddBoolToObject(loading,"active",loadingBusy.load());
                cJSON_AddBoolToObject(loading,"cancel_requested",loadingCancel.load());
                cJSON_AddStringToObject(loading,"stage",loadingStage.load());
                cJSON_AddNumberToObject(loading,"bytes_done",loadingDone.load());
                cJSON_AddNumberToObject(loading,"bytes_total",loadingTotal.load());
                cJSON_AddNumberToObject(loading,"display_us",loadingDisplayUs.load());
                cJSON_AddNumberToObject(loading,"frames",loadingFrames.load());
            }
            else error(reply,"starting");
            return true;
        }
        if(!strcmp(cmd,"inkdesk.open")||!strcmp(cmd,"inkdesk.ui.open")||!strcmp(cmd,"paper.open")) {
            Open();
            return true;
        }
        if(!strcmp(cmd,"inkdesk.tap")||!strcmp(cmd,"paper.tap")) {
            ::Input i;
            i.kind=1;
            i.revision=displayed.load();
            if(!number(req,"x",i.x,0,479)||!number(req,"y",i.y,0,799)){error(reply,"invalid_or_busy_tap");return true;}
            resolveKey(i);
            if(!post(i))error(reply,"invalid_or_busy_tap");
            return true;
        }
        if(!strcmp(cmd,"paper.command")) {
            auto*a=cJSON_GetObjectItem(req,"action"),*v=cJSON_GetObjectItem(req,"value");
            ::Input i;
            if(!cJSON_IsString(a)||strlen(a->valuestring)>=sizeof i.action||(v&&(!cJSON_IsString(v)||strlen(v->valuestring)>=sizeof i.value))) {
                error(reply,"invalid_command");
                return true;
            }
            strcpy(i.action,a->valuestring);
            if(v)strcpy(i.value,v->valuestring);
            auto*token=cJSON_GetObjectItem(req,"request_token");
            if(token){
                if(!cJSON_IsString(token)||strlen(token->valuestring)!=32||strspn(token->valuestring,"0123456789abcdef")!=32){error(reply,"invalid_request_token");return true;}
                strcpy(i.token,token->valuestring);
            }
            if(!post(i))error(reply,"queue_full");
            else cJSON_AddStringToObject(reply,"state","queued");
            return true;
        }
        if(!strcmp(cmd,"paper.transfer")) {
            if(!runtime) {
                error(reply,"starting");
                return true;
            }
            auto*op=cJSON_GetObjectItem(req,"op");
            if(!cJSON_IsString(op)) {
                error(reply,"invalid_operation");
                return true;
            }
            paper::Status st;
            int id=0;
            if(!strcmp(op->valuestring,"begin")) {
                auto*p=cJSON_GetObjectItem(req,"path"),*sha=cJSON_GetObjectItem(req,"sha256");
                int len=0;
                uint64_t tid=0;
                if(!cJSON_IsString(p)||!cJSON_IsString(sha)||!number(req,"size",len,1,128*1024*1024)) {
                    error(reply,"invalid_transfer");
                    return true;
                }
                st=runtime->transfers().begin(p->valuestring,len,sha->valuestring,tid);
                if(st)cJSON_AddNumberToObject(reply,"transfer_id",tid);
            }
            else if(!number(req,"transfer_id",id,1,INT32_MAX)) {
                error(reply,"invalid_transfer_id");
                return true;
            }
            else if(!strcmp(op->valuestring,"chunk")) {
                auto*h=cJSON_GetObjectItem(req,"hex");
                int off=0;
                if(!cJSON_IsString(h)||!number(req,"offset",off,0,128*1024*1024)) {
                    error(reply,"invalid_chunk");
                    return true;
                }
                size_t n=strlen(h->valuestring);
                if(!n||n>1024||n%2) {
                    error(reply,"invalid_chunk_size");
                    return true;
                }
                uint8_t bytes[512];
                auto digit=[](char c) {
                    return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;
                };
                for(size_t k=0;k<n/2;++k) {
                    int a=digit(h->valuestring[k*2]),b=digit(h->valuestring[k*2+1]);
                    if(a<0||b<0) {
                        error(reply,"invalid_hex");
                        return true;
                    }
                    bytes[k]=uint8_t(a*16+b);
                }
                st=runtime->transfers().append(id,off,bytes,n/2);
            }
            else if(!strcmp(op->valuestring,"commit")) {
                paper::TransferInfo receipt;
                st=runtime->transfers().commit(id,receipt);
                if(st){cJSON_AddStringToObject(reply,"sha256",receipt.sha.c_str());cJSON_AddStringToObject(reply,"path",receipt.path.c_str());cJSON_AddNumberToObject(reply,"size",receipt.received);}
            }
            else if(!strcmp(op->valuestring,"cancel"))st=runtime->transfers().cancel(id);
            else st=paper::Status::fail(paper::Error::Invalid,"unknown transfer op");
            if(!st)error(reply,paper::errorName(st.code));
            return true;
        }
        error(reply,"legacy_lab_removed_or_unknown_command");
        return true;
    }
}
