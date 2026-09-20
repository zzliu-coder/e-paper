// Included at the end of lv_adapter_display.cc, where EpdFlushCtx and the
// original capture/refresh helpers have their real definitions.
#include "ui/fontbench/core.h"
#include "ui/fontbench/gray/panel_port.h"
#include <memory>

namespace {
void fb_pack_luma(const uint8_t* luma,uint8_t* out){
    for(size_t i=0;i<96000;++i)out[i]=uint8_t((luma[i*4]/85)<<6|(luma[i*4+1]/85)<<4|(luma[i*4+2]/85)<<2|luma[i*4+3]/85);
}
void fb_pack_mono(const uint8_t* mono,uint8_t* out){
    for(size_t i=0;i<96000;++i){uint8_t b=0;for(int k=0;k<4;++k){size_t bit=i*4+k;if(mono[bit/8]&(128u>>(bit&7)))b|=uint8_t(3u<<(6-k*2));}out[i]=b;}
}
std::string fb_receipt_json(const fb_receipt& r){
    auto* j=cJSON_CreateObject();if(!j)return "{}";
    cJSON_AddStringToObject(j,"profile",FB_GRAY_PROFILE);cJSON_AddBoolToObject(j,"success",r.success);
    cJSON_AddBoolToObject(j,"cleaned",r.cleaned);cJSON_AddBoolToObject(j,"baseline_synced",r.baseline_synced);
    cJSON_AddNumberToObject(j,"error",r.error);cJSON_AddNumberToObject(j,"total_ms",r.total_ms);
    cJSON_AddStringToObject(j,"physical_quality","NOT_PROVEN");
    auto* phases=cJSON_AddArrayToObject(j,"phases");
    for(unsigned i=0;i<r.phases&&i<FB_GRAY_PHASES;++i){auto* p=cJSON_CreateObject();cJSON_AddNumberToObject(p,"ctrl2",r.phase[i].sequence);cJSON_AddNumberToObject(p,"ms",r.phase[i].elapsed_ms);cJSON_AddBoolToObject(p,"complete",r.phase[i].complete);cJSON_AddItemToArray(phases,p);}
    char* text=cJSON_PrintUnformatted(j);std::string result=text?text:"{}";cJSON_free(text);cJSON_Delete(j);return result;
}
}

esp_err_t LVAdapterDisplay::RefreshFontBench(paper::fontbench::Session& lab,bool full,uint8_t* snapshot,size_t capacity){
    auto* ctx=epd_flush_ctx_;
    if(!snapshot||capacity!=96000||!ctx||ctx->fb_size!=48000||ctx->panel_w!=800||ctx->panel_h!=480||ctx->freeze_updates||ctx->defer_boot_paint)return ESP_ERR_INVALID_STATE;
    if(ctx->refresh_inflight){const auto e=WaitRefreshDone(ctx,"fontbench drain");if(e!=ESP_OK)return e;ctx->refresh_inflight=false;ctx->suppress_lvgl_notify=false;}
    if(lab.TakeRecovery()){
        const auto e=fb_panel_recover(ctx->panel);
        if(e!=ESP_OK){lab.RecordDisplay("recovery-failed","{\"success\":false}",false);return e;}
        ctx->force_next_full=true;full=true;
    }
    const bool gray=lab.WantsGray()&&lab.grayArmed();
    if(!gray){
        const bool restored=fb_panel_needs_restore(ctx->panel);
        const bool cleaned=restored||full||!ctx->has_last;
        const auto e=RefreshDiagnostic(full);
        if(e==ESP_OK){lab.CaptureFirmware(ctx->last_fb,ctx->fb_size);fb_pack_mono(ctx->last_fb,snapshot);}
        lab.RecordDisplay(lab.WantsGray()?"gray-request-blocked":"mono",(cleaned?"{\"backend\":\"original-I1\",\"cleaned\":true}":"{\"backend\":\"original-I1\",\"cleaned\":false}"),e==ESP_OK);
        return e;
    }
    // Allocate BEFORE modifying last_fb. An allocation failure preserves the old
    // reference; all big buffers explicitly live in PSRAM, outside the worker stack.
    auto* bytes=static_cast<uint8_t*>(heap_caps_malloc(384000,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!bytes){lab.RecordDisplay("allocation-failed","{\"success\":false}",false);return ESP_ERR_NO_MEM;}
    std::unique_ptr<uint8_t,decltype(&heap_caps_free)> luma(bytes,heap_caps_free);
    const bool deferred=ctx->defer_standby_paint;
    ctx->defer_standby_paint=true;lv_obj_invalidate(lv_screen_active());lv_refr_now(ctx->disp);ctx->defer_standby_paint=deferred;
    lab.CaptureFirmware(ctx->last_fb,ctx->fb_size);
    for(size_t i=0;i<384000;++i)bytes[i]=(ctx->last_fb[i/8]&(128u>>(i&7)))?255:0;
    lab.PutGrayRegions(bytes,384000);
    fb_receipt receipt{};ctx->suppress_lvgl_notify=true;
    const auto e=fb_panel_present(ctx->panel,bytes,ctx->last_fb,full,true,&receipt);
    ctx->suppress_lvgl_notify=false;
    if(e==ESP_OK){fb_pack_luma(bytes,snapshot);ctx->has_last=true;ctx->partial_refresh_count=0;}
    else {ctx->has_last=false;ctx->force_next_full=true;}
    auto json=fb_receipt_json(receipt);lab.RecordDisplay("gray4-experimental",json.c_str(),e==ESP_OK);return e;
}
