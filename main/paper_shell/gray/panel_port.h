#pragma once
#include "driver.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t fb_panel_present(esp_lcd_panel_handle_t,const uint8_t* native_luma,const uint8_t* mono_frame,bool full,bool armed,fb_receipt*);
esp_err_t fb_panel_normal_guard(esp_lcd_panel_handle_t);
bool fb_panel_needs_restore(esp_lcd_panel_handle_t);
esp_err_t fb_panel_detach(esp_lcd_panel_handle_t);
esp_err_t fb_panel_recover(esp_lcd_panel_handle_t);
#ifdef __cplusplus
}
#endif
