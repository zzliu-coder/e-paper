#pragma once
#include <stdint.h>
/* Coverage, not luminance of a premature 0/1 result, decides the final pixel.
 * The 255 denominator has no half-integer tie, so complement symmetry holds.
 * Both the target LVGL hook and host regression tests use this function. */
static inline uint8_t paper_mono_mix(uint8_t source, uint8_t dest, uint8_t mask, uint8_t opacity) {
    const unsigned alpha=((unsigned)mask*opacity+127u)/255u;
    return (uint8_t)((source*alpha+dest*(255u-alpha))>=128u);
}
static inline void paper_mono_fill(uint8_t* dest,int width,int height,int stride,int bit_offset,
                                   uint8_t source,const uint8_t* mask,int mask_stride,uint8_t opacity) {
    for(int y=0;y<height;++y){
        for(int x=0;x<width;++x){
            const int bit=x+bit_offset;const uint8_t flag=(uint8_t)(0x80u>>(bit&7));
            const uint8_t old=(dest[bit/8]&flag)?1:0;
            const uint8_t value=paper_mono_mix(source,old,mask?mask[x]:255,opacity);
            if(value)dest[bit/8]|=flag;else dest[bit/8]&=(uint8_t)~flag;
        }
        dest+=stride;if(mask)mask+=mask_stride;
    }
}
