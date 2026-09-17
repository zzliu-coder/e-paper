#pragma once
/* Forced include on LVGL's I1 blender only. Managed dependency bytes stay intact.
 * LVGL 9.5.0 fill hook covers text masks, rounded borders, opacity and fills. */
#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"
#include "mono_blend.h"
static inline lv_result_t paper_i1_fill(lv_draw_sw_blend_fill_dsc_t* d) {
    paper_mono_fill((uint8_t*)d->dest_buf,d->dest_w,d->dest_h,d->dest_stride,
                   d->relative_area.x1%8,lv_color_luminance(d->color)>LV_DRAW_SW_I1_LUM_THRESHOLD,
                   d->mask_buf,d->mask_stride,d->opa>=LV_OPA_MAX?255:d->opa);
    return LV_RESULT_OK;
}
#define LV_DRAW_SW_COLOR_BLEND_TO_I1(d) paper_i1_fill(d)
#define LV_DRAW_SW_COLOR_BLEND_TO_I1_WITH_OPA(d) paper_i1_fill(d)
#define LV_DRAW_SW_COLOR_BLEND_TO_I1_WITH_MASK(d) paper_i1_fill(d)
#define LV_DRAW_SW_COLOR_BLEND_TO_I1_MIX_MASK_OPA(d) paper_i1_fill(d)
