// Included once at the END of lv_adapter_display.cc. Uses the real EpdFlushCtx.
#include "panel_port.h"
#include <memory>
#include <sstream>

esp_err_t LVAdapterDisplay::RefreshPaper(const uint8_t* portrait2,size_t n,bool full,bool gray,std::string* report) {
 const auto started=esp_timer_get_time();
 if(report)*report="{\"submitted\":false}";
 auto* ctx=epd_flush_ctx_;
 if(!portrait2||n!=96000||!ctx||ctx->paper_presenting||ctx->fb_size!=48000||ctx->panel_w!=800||ctx->panel_h!=480||ctx->freeze_updates||ctx->defer_boot_paint)return ESP_ERR_INVALID_STATE;
 if(ctx->refresh_inflight){auto e=WaitRefreshDone(ctx,"paper drain");if(e!=ESP_OK)return e;ctx->refresh_inflight=false;ctx->suppress_lvgl_notify=false;}
 if(!gray){
   const bool restored=fb_panel_needs_restore(ctx->panel);
   if(restored){auto e=RecoverPaper();if(e!=ESP_OK)return e;full=true;}
   auto e=RefreshDiagnostic(full,true);
   if(report){std::ostringstream o;o<<"{\"mode\":\"mono\",\"success\":"<<(e==ESP_OK?"true":"false")<<",\"restore_from_gray\":"<<(restored?"true":"false")<<",\"requested_full\":"<<(full?"true":"false")<<",\"elapsed_us\":"<<esp_timer_get_time()-started<<",\"physical_flash_count\":null}";*report=o.str();}
   return e;
 }
#if !defined(CONFIG_PAPER_GRAY_VERIFIED) && !defined(CONFIG_PAPER_GRAY_EXPERIMENT)
 return ESP_ERR_NOT_SUPPORTED;
#else
 auto* memory=static_cast<uint8_t*>(heap_caps_malloc(384000+48000,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!memory)return ESP_ERR_NO_MEM;
 std::unique_ptr<uint8_t,decltype(&heap_caps_free)>bytes(memory,heap_caps_free);auto*mono=memory+384000;memset(mono,255,48000);
 // Portrait x,y -> native x=y,y=479-x. The same transform is host-tested.
 for(int y=0;y<800;++y)for(int x=0;x<480;++x){size_t i=size_t(y)*480+x,at=size_t(479-x)*800+y;uint8_t value=(portrait2[i/4]>>(6-2*(i%4)))&3;memory[at]=value*85;if(value<2)mono[at/8]&=uint8_t(~(128u>>(at&7)));}
 bool deferred=ctx->defer_standby_paint;ctx->defer_standby_paint=true;lv_obj_invalidate(lv_screen_active());lv_refr_now(ctx->disp);ctx->defer_standby_paint=deferred;
 fb_receipt receipt{};ctx->paper_presenting=true;ctx->suppress_lvgl_notify=true;auto e=fb_panel_present(ctx->panel,memory,mono,full,true,&receipt);ctx->suppress_lvgl_notify=false;ctx->paper_presenting=false;
 if(report){std::ostringstream o;o<<"{\"mode\":\"gray4-experimental\",\"success\":"<<(e==ESP_OK?"true":"false")<<",\"cleaned\":"<<(receipt.cleaned?"true":"false")<<",\"elapsed_us\":"<<esp_timer_get_time()-started<<",\"driver_ms\":"<<receipt.total_ms<<",\"physical_flash_count\":null,\"phases\":[";for(unsigned i=0;i<receipt.phases;++i){if(i)o<<',';o<<"{\"sequence\":"<<unsigned(receipt.phase[i].sequence)<<",\"ms\":"<<receipt.phase[i].elapsed_ms<<",\"complete\":"<<(receipt.phase[i].complete?"true":"false")<<'}';}o<<"]}";*report=o.str();}
 if(e==ESP_OK){memcpy(ctx->last_fb,mono,48000);ctx->has_last=true;ctx->partial_refresh_count=0;}else{ctx->has_last=false;ctx->force_next_full=true;}
 return e;
#endif
}
esp_err_t LVAdapterDisplay::RecoverPaper(){auto*ctx=epd_flush_ctx_;if(!ctx||ctx->paper_presenting)return ESP_ERR_INVALID_STATE;if(ctx->refresh_inflight){auto e=WaitRefreshDone(ctx,"paper recover");if(e!=ESP_OK)return e;ctx->refresh_inflight=false;ctx->suppress_lvgl_notify=false;}auto e=fb_panel_recover(ctx->panel);ctx->force_next_full=true;if(e!=ESP_OK)ctx->has_last=false;return e;}
