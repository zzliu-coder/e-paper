// Generated, dedicated font assets. Never silently rescale.
#pragma once
#include "lvgl.h"
extern "C" {
extern const lv_font_t lab_p0_16;
extern const lv_font_t lab_p0_18;
extern const lv_font_t lab_p0_20;
extern const lv_font_t lab_p0_22;
extern const lv_font_t lab_p0_24;
extern const lv_font_t lab_p0_28;
extern const lv_font_t lab_p0_32;
extern const lv_font_t lab_p0_40;
extern const lv_font_t lab_p1_16;
extern const lv_font_t lab_p1_18;
extern const lv_font_t lab_p1_20;
extern const lv_font_t lab_p1_22;
extern const lv_font_t lab_p1_24;
extern const lv_font_t lab_p1_28;
extern const lv_font_t lab_p1_32;
extern const lv_font_t lab_p1_40;
extern const lv_font_t lab_p2_16;
extern const lv_font_t lab_p2_18;
extern const lv_font_t lab_p2_20;
extern const lv_font_t lab_p2_22;
extern const lv_font_t lab_p2_24;
extern const lv_font_t lab_p2_28;
extern const lv_font_t lab_p2_32;
extern const lv_font_t lab_p2_40;
extern const lv_font_t lab_p3_16;
extern const lv_font_t lab_p3_18;
extern const lv_font_t lab_p3_20;
extern const lv_font_t lab_p3_22;
extern const lv_font_t lab_p3_24;
extern const lv_font_t lab_p3_28;
extern const lv_font_t lab_p3_32;
extern const lv_font_t lab_p3_40;
}
inline const lv_font_t* LabFont(int profile,int size){
 static const lv_font_t* fonts[4][8]={{&lab_p0_16,&lab_p0_18,&lab_p0_20,&lab_p0_22,&lab_p0_24,&lab_p0_28,&lab_p0_32,&lab_p0_40},{&lab_p1_16,&lab_p1_18,&lab_p1_20,&lab_p1_22,&lab_p1_24,&lab_p1_28,&lab_p1_32,&lab_p1_40},{&lab_p2_16,&lab_p2_18,&lab_p2_20,&lab_p2_22,&lab_p2_24,&lab_p2_28,&lab_p2_32,&lab_p2_40},{&lab_p3_16,&lab_p3_18,&lab_p3_20,&lab_p3_22,&lab_p3_24,&lab_p3_28,&lab_p3_32,&lab_p3_40}};
 static const int sizes[]={16,18,20,22,24,28,32,40};
 if(profile<0||profile>3)return nullptr;
 for(int i=0;i<8;++i)if(sizes[i]==size)return fonts[profile][i];
 return nullptr;
}
