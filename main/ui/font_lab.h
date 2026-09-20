#pragma once
#include "components.h"
#include <cstdio>
namespace paper {
inline constexpr int LabSizes[]={16,18,20,22,24,28,32,40};
inline constexpr int LabPageCount=6;
// 0..3 are the bounded, statically generated comparison fonts. 4 and 5 are
// device-only selectors for the existing flash fontpack (2bpp / 4bpp mask).
// The panel still receives a 1-bit frame; these selectors compare coverage
// before the I1 quantisation and do not claim true panel grayscale.
inline constexpr int LabFontpack2=4;
inline constexpr int LabFontpack4=5;
inline constexpr const char* LabProfiles[]={
 "A / 思源黑体 · 现有基线",
 "B / 思源黑体 · MONO",
 "C / 霞鹜新晰黑 · 屏幕版",
 "D / 文泉驿微米黑 · 嵌入式"
};
inline void LabText(inkdesk::Frame& f,int x,int y,int w,const char* s,int size,int profile,bool black=true){
 f.textIn({x,y,w,size+12},s,size,black);
 if(f.drawCount)f.draws[f.drawCount-1].fontProfile=profile;
}
inline void LabFontpackText(inkdesk::Frame& f,int x,int y,int w,const char* s,int size,int bpp,bool black=true){
 f.textIn({x,y,w,size+12},s,size,black);
 if(f.drawCount)f.draws[f.drawCount-1].fontProfile=bpp>=4?LabFontpack4:LabFontpack2;
}
inline void FontLab(inkdesk::Frame& f,int page,int profile,int size,int saved,int nav,int algorithm,int resize,int vote,int refresh){
 const char* tabs[]={"字号","算法","反白","混排","实景","位深"};
 for(int i=0;i<LabPageCount;++i)Button(f,{20+i*74,66,70,48},tabs[i],nav,i,i==page,20);
 Text(f,24,124,432,LabProfiles[profile],18);
 Button(f,{20,158,104,46},"A 基线",algorithm,0,profile==0,18);
 Button(f,{130,158,104,46},"B 微调",algorithm,1,profile==1,18);
 Button(f,{240,158,104,46},"C 屏幕",algorithm,2,profile==2,18);
 Button(f,{350,158,104,46},"D 嵌入",algorithm,3,profile==3,18);
 char label[48];std::snprintf(label,sizeof(label),"%d px / %d",LabSizes[size],profile);
 Button(f,{20,212,64,46},"-",resize,-1,false,20);Text(f,104,222,208,label,20);
 Button(f,{322,212,64,46},"+",resize,1,false,20);
 Button(f,{396,212,64,46},"全刷",refresh,0,false,18);
 if(page==0){
  for(int i=0;i<8;++i){char n[8];std::snprintf(n,sizeof(n),"%d",LabSizes[i]);Text(f,24,274+i*45,48,n,18);LabText(f,82,270+i*45,374,"阅读美晨 Aa82",LabSizes[i],profile);}
 }else if(page==1){
  for(int i=0;i<4;++i){Text(f,24,268+i*96,430,LabProfiles[i],18);LabText(f,24,300+i*96,432,"阅读美晨 Aa82",LabSizes[size],i);Dashed(f,{24,350+i*96,432,1});}
 }else if(page==2){
  Panel(f,{20,276,440,148});Panel(f,{20,440,440,148},true);
  LabText(f,36,294,408,"阅读美晨 Aa82",LabSizes[size],profile);
  LabText(f,36,346,408,"Wi-Fi 82% 09:41",LabSizes[size],profile);
  LabText(f,36,458,408,"阅读美晨 Aa82",LabSizes[size],profile,false);
  LabText(f,36,510,408,"Wi-Fi 82% 09:41",LabSizes[size],profile,false);
 }else if(page==3){
  const char* samples[]={"设置 Wi-Fi 82%","阅读 012 / 128","清晨，窗边的光。","Aa Il1 O0 8B rn m","（中文）: !? ,.;"};
  for(int i=0;i<5;++i){LabText(f,24,280+i*64,432,samples[i],LabSizes[size],profile);Dashed(f,{24,334+i*64,432,1});}
 }else if(page==4){
  Panel(f,{20,274,440,146});LabText(f,36,286,408,"正在阅读 / 示例",LabSizes[size],profile);
  LabText(f,36,342,408,"系统之美 82%",LabSizes[size],profile);
  Panel(f,{20,438,440,78},true);LabText(f,36,454,408,"继续阅读",LabSizes[size],profile,false);
  LabText(f,24,538,432,"清晨，窗边的光。",LabSizes[size],profile);
  LabText(f,24,596,432,"设置 Wi-Fi 82%",LabSizes[size],profile);
 }else{
  // Historical fontpack comparison: exact 30px, both polarities, no visual votes.
  for(int j=0;j<4;++j){
   const int x=20+(j%2)*224,y=276+(j/2)*164;
   const bool inverse=j>=2;const int bpp=(j%2)?4:2;
   Panel(f,{x,y,216,148},inverse);
   Text(f,x+8,y+10,200,bpp==2?"MiSans 30 / 2bpp":"MiSans 30 / 4bpp",18,!inverse);
   LabFontpackText(f,x+8,y+64,200,"阅读美晨",30,bpp,!inverse);
  }
  Text(f,24,616,432,"现有面板仍为 1-bit · 这里比较覆盖率",18);
 }
 Text(f,24,644,432,LabProfiles[profile],18);
 const char* votes[]={"清楚","太细","太粗","粘连"};
 if(page!=5)for(int i=0;i<4;++i)Button(f,{20+i*112,678,104,50},votes[i],vote,i,false,20);
 Text(f,24,744,432,saved==1?"已存 SD · 可继续比较":saved<0?"保存失败 · 请连接电脑查看":"点击评价，记录当前方案与字号",18);
}
}
