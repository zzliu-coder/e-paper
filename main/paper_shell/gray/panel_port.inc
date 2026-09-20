#if !defined(CONFIG_PAPER_GRAY_EXPERIMENT) && !defined(CONFIG_PAPER_GRAY_VERIFIED)
// Product is binary-only. No experimental panel state or waveform is linked.
esp_err_t fb_panel_normal_guard(esp_lcd_panel_handle_t p){(void)p;return ESP_OK;}
bool fb_panel_needs_restore(esp_lcd_panel_handle_t p){(void)p;return false;}
esp_err_t fb_panel_detach(esp_lcd_panel_handle_t p){(void)p;return ESP_OK;}
esp_err_t fb_panel_recover(esp_lcd_panel_handle_t p){(void)p;return ESP_OK;}
#else
/* Included at the END of the original SSD1677 driver translation unit.
 * Accesses that driver's actual private state; never redeclares its layout.
 * All callers hold the same LVGL/display lock. No interrupt or timer writes.
 */
#include "paper_shell/gray/panel_port.h"
#include "esp_timer.h"
typedef struct {
 epaper_panel_t* owner;
 fb_gray_state gray;
 bool restoring;
 uint8_t* storage; /* base, LSB, MSB, last committed monotone frame */
 uint8_t tx_scratch[128] __attribute__((aligned(4)));
} fb_panel_context;
static fb_panel_context fbpc;
static uint32_t fbp_now(void* p){(void)p;return (uint32_t)(esp_timer_get_time()/1000);}
static int fbp_idle(void* p,uint32_t ms){
 epaper_panel_t* panel=(epaper_panel_t*)p;const int64_t deadline=esp_timer_get_time()+(int64_t)ms*1000;
 /* SSD1677 BUSY assertion can follow the activation write; allow it to rise. */
 esp_rom_delay_us(1000);
 while(gpio_get_level(panel->busy_gpio_num)){
  if(esp_timer_get_time()>=deadline)return FB_TIMEOUT;
  vTaskDelay(1);
 }
 esp_rom_delay_us(100);return FB_OK;
}
static int fbp_write(void* p,int cmd,const uint8_t* data,size_t n){
 epaper_panel_t* panel=(epaper_panel_t*)p;
 // tx_param is synchronous and drains previous queued color DMA. -1 omits the
 // command byte. The portable layer sends at most 128 bytes per data chunk.
 if(n>sizeof(fbpc.tx_scratch)||(!data&&n))return FB_ARGUMENT;
 if(n)memcpy(fbpc.tx_scratch,data,n);
 return esp_lcd_panel_io_tx_param(panel->io,cmd,n?fbpc.tx_scratch:NULL,n)==ESP_OK?FB_OK:FB_IO;
}
static int fbp_prepare_bw(void* p){
 // Reuse the actual board's proven full B/W LUT, not an assumed OTP waveform.
 // C4 leaves analog power on for the subsequent short gray overlay.
 epaper_panel_t* panel=(epaper_panel_t*)p;
 return epaper_panel_load_fast_lut(&panel->base)==ESP_OK?FB_OK:FB_IO;
}
static int fbp_begin_gray(void* p){
 epaper_panel_t* panel=(epaper_panel_t*)p;
 // A deliberate mode entry, never recovery from stuck BUSY. Invalidate all
 // volatile B/W registers before adopting FreeInk's complete Metalio path.
 if(fbp_idle(p,FB_GRAY_TIMEOUT_MS)!=FB_OK)return FB_TIMEOUT;
 esp_err_t e=epaper_panel_reset(&panel->base);
 if(e==ESP_OK)e=epaper_panel_init(&panel->base);
 gpio_intr_disable(panel->busy_gpio_num);
 if(e!=ESP_OK)return FB_IO;
 const uint8_t options[10]={0};
 return fbp_write(p,0x37,options,sizeof(options));
}
static fb_bus fbp_bus(epaper_panel_t* p){fb_bus b={p,fbp_write,fbp_idle,fbp_now,fbp_prepare_bw,fbp_begin_gray};return b;}
static esp_err_t fbp_error(int e){return e==FB_OK?ESP_OK:e==FB_TIMEOUT?ESP_ERR_TIMEOUT:e==FB_ARGUMENT?ESP_ERR_INVALID_ARG:e==FB_LOCKED?ESP_ERR_NOT_SUPPORTED:ESP_FAIL;}
static esp_err_t fbp_attach(esp_lcd_panel_handle_t panel){
 if(!panel||SSD1677_PANEL_WIDTH!=800||SSD1677_PANEL_HEIGHT!=480)return ESP_ERR_INVALID_ARG;
 epaper_panel_t* p=__containerof(panel,epaper_panel_t,base);
 // Audited board sets non_copy_mode=true and invert_color=true. In non-copy
 // mode the original draw path transmits bytes verbatim; invert/mirror-X flags
 // are deliberately ignored there. Require that exact raw-frame contract.
 if(!p->_non_copy_mode||p->_swap_xy||p->_mirror_y||p->gap_x||p->gap_y)return ESP_ERR_NOT_SUPPORTED;
 if(fbpc.owner&&fbpc.owner!=p)return ESP_ERR_INVALID_STATE;
 if(!fbpc.storage){
  fbpc.storage=(uint8_t*)heap_caps_malloc(4*FB_FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!fbpc.storage)return ESP_ERR_NO_MEM;
  memset(fbpc.storage,255,4*FB_FRAME_BYTES);
 }
 fbpc.owner=p;return ESP_OK;
}
esp_err_t fb_panel_present(esp_lcd_panel_handle_t panel,const uint8_t* luma,const uint8_t* mono,bool full,bool armed,fb_receipt* receipt){
 if(!armed)return ESP_ERR_NOT_SUPPORTED;
 if(!luma||!mono||!receipt)return ESP_ERR_INVALID_ARG;
 esp_err_t e=fbp_attach(panel);if(e!=ESP_OK)return e;
 epaper_panel_t* p=fbpc.owner;fb_bus b=fbp_bus(p);
 uint8_t* base=fbpc.storage;uint8_t* lo=base+FB_FRAME_BYTES;uint8_t* hi=lo+FB_FRAME_BYTES;
 int result=fb_encode_planes(luma,FB_FRAME_BYTES*8,base,lo,hi);
 if(result!=FB_OK)return fbp_error(result);
 gpio_intr_disable(p->busy_gpio_num);
 result=fb_gray_present(&fbpc.gray,&b,base,lo,hi,true,full);
 *receipt=fbpc.gray.last;
 // OTP/custom-gray use changed driver registers. The old driver's LUT cache must
 // be invalidated even on a failed experimental command.
 p->loaded_lut=EPD_LOADED_LUT_NONE;
 if(result==FB_OK)memcpy(hi+FB_FRAME_BYTES,mono,FB_FRAME_BYTES);
 return fbp_error(result);
}
esp_err_t fb_panel_normal_guard(esp_lcd_panel_handle_t panel){
 if(!fbpc.owner||fbpc.restoring)return ESP_OK;
 if(&fbpc.owner->base!=panel)return ESP_ERR_INVALID_STATE;
 if(fbpc.gray.fault)return ESP_ERR_INVALID_STATE;
 if(!fbpc.gray.active)return ESP_OK;
 fbpc.restoring=true;fb_bus b=fbp_bus(fbpc.owner);
 int r=fb_gray_restore(&fbpc.gray,&b,fbpc.storage+3*FB_FRAME_BYTES);
 esp_err_t e=fbp_error(r);
 if(e==ESP_OK)e=epaper_panel_load_fast_lut(panel);
 // External gray writes CTRL1=00 00. Explicitly restore the original normal setting.
 if(e==ESP_OK){const uint8_t ctrl[]={0,0};e=esp_lcd_panel_io_tx_param(fbpc.owner->io,0x21,ctrl,2);}
 // Restore the exact board B/W initialization after the fixed-plane gray path.
 if(e==ESP_OK){const uint8_t options[]={0,0,0,0,0,0x40,0,0,0,0};e=esp_lcd_panel_io_tx_param(fbpc.owner->io,0x37,options,sizeof(options));}
 if(e!=ESP_OK){fbpc.gray.fault=true;fbpc.gray.baseline_synced=false;}
 fbpc.restoring=false;return e;
}
esp_err_t fb_panel_recover(esp_lcd_panel_handle_t panel){
 if(!fbpc.owner)return ESP_OK;
 if(&fbpc.owner->base!=panel)return ESP_ERR_INVALID_STATE;
 // A stuck BUSY must never be followed by reset, power-off or additional RAM writes.
 if(fbp_idle(fbpc.owner,FB_GRAY_TIMEOUT_MS)!=FB_OK)return ESP_ERR_TIMEOUT;
 if(!fbpc.gray.fault)return fb_panel_normal_guard(panel);
 fbpc.restoring=true;gpio_intr_disable(fbpc.owner->busy_gpio_num);
 esp_err_t e=epaper_panel_reset(panel);
 if(e==ESP_OK)e=epaper_panel_init(panel); // original board initialization, bounded BUSY helper
 if(e==ESP_OK){fb_gray_init(&fbpc.gray);fbpc.gray.active=true;fbpc.gray.gray_residue=true;}
 fbpc.restoring=false;
 if(e==ESP_OK)e=fb_panel_normal_guard(panel);
 if(e!=ESP_OK)fbpc.gray.fault=true;
 return e;
}

bool fb_panel_needs_restore(esp_lcd_panel_handle_t p){return fbpc.owner && &fbpc.owner->base==p&&(fbpc.gray.active||fbpc.gray.fault);}
esp_err_t fb_panel_detach(esp_lcd_panel_handle_t p){
 if(!fbpc.owner||&fbpc.owner->base!=p)return ESP_OK;
 esp_err_t e=fb_panel_normal_guard(p);if(e!=ESP_OK)return e;
 heap_caps_free(fbpc.storage);memset(&fbpc,0,sizeof(fbpc));return ESP_OK;
}
#endif
