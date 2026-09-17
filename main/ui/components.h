#pragma once
#include "tokens.h"
#include "../inkdesk_r2/core/types.h"
#include <cstdio>
namespace paper {
using inkdesk::Frame;using inkdesk::Rect;using inkdesk::DrawKind;using inkdesk::Action;
inline void Hit(Frame& f,Rect r,int action,int value=0){
    if(!action)return;
    if(f.hitCount==f.hits.size()){f.overflow=true;return;}
    f.hits[f.hitCount++]={r,{static_cast<Action>(action),value,f.epoch,0}};
}
inline void Text(Frame& f,int x,int y,int w,const char* s,int size=token::Body,bool black=true){
    f.textIn({x,y,w,size+token::TextExtra},s,size,black);
}
inline void Panel(Frame& f,Rect r,bool dark=false,int radius=token::Radius,int stroke=token::Stroke){
    if(f.draw(dark?DrawKind::PanelFill:DrawKind::Panel,r,"",radius)){
        auto& d=f.draws[f.drawCount-1];d.radius=radius;d.stroke=stroke;
    }
}
inline void Dashed(Frame& f,Rect r,int stroke=1,int dash=6,int gap=4){
    if(f.draw(DrawKind::Dashed,r)){
        auto& d=f.draws[f.drawCount-1];d.stroke=stroke;d.dash=dash;d.gap=gap;
    }
}
inline void Texture(Frame& f,Rect r,Tone tone){
    if(f.draw(DrawKind::Texture,r))f.draws[f.drawCount-1].density=Density(tone);
}
inline void Button(Frame& f,Rect r,const char* s,int action,int value=0,bool primary=false,int size=token::Body,State state=State::Normal){
    const bool selected=primary||state==State::Selected;
    Panel(f,r,selected);
    Text(f,r.x+12,r.y+(r.h-size)/2-2,r.w-24,s,size,!selected);
    if(state==State::Disabled||state==State::Busy)Dashed(f,{r.x+12,r.y+r.h-7,r.w-24,1});
    else Hit(f,r,action,value);
}
inline void Header(Frame& f,const char* title,int backAction=0){
    Text(f,token::Margin,16,310,title,token::Heading);
    Text(f,364,22,96,"纸间 UI",18);
    Hit(f,{0,0,332,58},backAction);
}
// Unknown hardware values stay explicit; no fabricated percent/network status.
inline void StatusBar(Frame& f,Rect r,const char* connection,int battery=-1){
    Text(f,r.x,r.y,r.w-106,connection,token::Caption);
    char b[24];if(battery<0)std::snprintf(b,sizeof(b),"电量未知");else std::snprintf(b,sizeof(b),"%d%%",std::clamp(battery,0,100));
    Text(f,r.x+r.w-104,r.y,104,b,token::Caption);
}
inline void SettingRow(Frame& f,Rect r,const char* label,const char* value,int action,int arg=0,State state=State::Normal){
    Panel(f,r,state==State::Selected);
    const bool black=state!=State::Selected;
    Text(f,r.x+16,r.y+12,r.w-32,label,token::Body,black);
    Text(f,r.x+16,r.y+48,r.w-32,value,token::Caption,black);
    if(state!=State::Disabled&&state!=State::Busy)Hit(f,r,action,arg);
}
inline void Message(Frame& f,Rect r,const char* title,const char* detail){
    Panel(f,r);Text(f,r.x+16,r.y+16,r.w-32,title,token::Heading);
    Dashed(f,{r.x+16,r.y+60,r.w-32,1});Text(f,r.x+16,r.y+76,r.w-32,detail,token::Caption);
}
inline void Progress(Frame& f,Rect r,int value,bool black=true){
    if(value<0){Dashed(f,r);return;}
    f.rect(r,false,black);const int filled=(std::clamp(value,0,100)+2)/5,step=(r.w-4)/20;
    if(step<3||r.h<8)return;
    for(int i=0;i<20;++i)f.rect({r.x+2+i*step,r.y+3,step-2,r.h-6},i<filled,black);
}
inline void Dots(Frame& f,int x,int y,const char* s,int cell,bool black=true){
    f.draw(DrawKind::Dots,{x,y,int(std::strlen(s))*6*cell,7*cell},s,cell,black);
}
// Deterministic, spatially anchored 1-bit patterns. No temporal dithering/LUT.
inline bool PatternBlack(const inkdesk::Draw& d,int x,int y){
    if(d.kind==DrawKind::Texture){
        constexpr int matrix[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
        return matrix[(y+d.box.y)&3][(x+d.box.x)&3]*100<d.density*16;
    }
    const int w=d.box.w,h=d.box.h,s=d.stroke,period=std::max(1,int(d.dash+d.gap));
    if(y<s||y>=h-s)return x%period<d.dash;
    if(x<s||x>=w-s)return y%period<d.dash;
    return false;
}
}
