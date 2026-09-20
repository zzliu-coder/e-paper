#include "paper/ui.hpp"
#include <climits>
namespace paper { namespace ui {
void Painter::record(const char*k,Rect b,int radius,int px,bool enabled,bool interactive,bool elided){
#ifdef PAPER_UI_AUDIT
    if(trace_)trace_->push_back({k,b,radius,px,enabled,interactive,elided});
#else
    (void)k;(void)b;(void)radius;(void)px;(void)enabled;(void)interactive;(void)elided;
#endif
}
void Painter::label(Rect b,const std::string&s,int px,int weight,Align align,bool inv){
    if(s.empty())return;
    FontReadBatch batch(f_);
    if(!batch.status()){if(error_.empty())error_=batch.status().message;return;}
    FontSpec spec{px,weight,false};
    spec.uiOnly=true;
    std::vector<Rune> runes; auto st=decodeUtf8(s,runes);
    if(!st){if(error_.empty())error_=st.message;return;}
    int advance=0,top=0,bottom=0;bool elided=false;
    std::vector<Glyph> prepared;
    Glyph dot;int ell=0;auto ellst=f_.glyph(0x2026,spec,dot);if(ellst)ell=dot.advance64;
    std::vector<int> widths;size_t fit=0;int total=0;
    for(const auto&r:runes){
        if(r.cp=='\r'||r.cp=='\n'){elided=true;break;}
        int v=0;st=f_.advance(r.cp,spec,v);if(!st){if(error_.empty())error_=st.message;return;}
        widths.push_back(v);total+=v;
    }
    elided=elided||total>b.w*64;
    for(size_t i=0;i<widths.size();++i){
        if(advance+widths[i]+(elided?ell:0)>b.w*64)break;
        advance+=widths[i];fit=i+1;
    }
    for(size_t i=0;i<fit;++i){Glyph g;st=f_.glyph(runes[i].cp=='\t'?' ':runes[i].cp,spec,g);if(!st){if(error_.empty())error_=st.message;return;}top=std::max(top,g.top);bottom=std::max(bottom,g.height-g.top);prepared.push_back(std::move(g));}
    if(elided&&ellst&&advance+ell<=b.w*64){advance+=ell;top=std::max(top,dot.top);bottom=std::max(bottom,dot.height-dot.top);prepared.push_back(std::move(dot));}
    if(top+bottom>b.h){if(error_.empty())error_="UI text height overflow";}
    int width=(advance+63)/64,x=b.x;
    if(align==Align::Center)x+=(b.w-width)/2;else if(align==Align::Right)x+=b.w-width;
    int baseline=b.y+(b.h-top-bottom)/2+top;
    int pen=x*64;for(const auto&g:prepared){c_.glyph(g,pen,baseline,b,false,inv);pen+=g.advance64;}
    record("label",b,0,px,true,false,elided);
}
void Painter::paragraph(Rect b,const std::string&s,int px,int weight,int line,bool inv){
    if(s.empty())return;
    FontReadBatch batch(f_);
    if(!batch.status()){if(error_.empty())error_=batch.status().message;return;}
    TextLayout l;FontSpec spec{px,weight,false};
    spec.uiOnly=true;
    auto st=layoutText(f_,spec,s,0,b.w,std::max(1,b.h/line),l);if(!st){if(error_.empty())error_=st.message;return;}
    for(size_t i=0;i<l.lines.size();++i){auto x=l.lines[i];std::string t=s.substr(x.begin,x.end-x.begin);if(i+1==l.lines.size()&&!l.eof)t+="…";label({b.x,b.y+int(i)*line,b.w,line},t,px,weight,Align::Left,inv);}
    record("paragraph",b,0,px,true,false,!l.eof);
}
void Painter::control(Rect b,const std::string&s,Control type,bool selected,bool enabled,int px){
    int r=type==Control::Choice?token::ChoiceRadius:type==Control::Key?token::KeyRadius:type==Control::Compact?token::CompactRadius:token::ButtonRadius;
    bool quiet=type==Control::Quiet||!enabled;
    if(selected&&enabled)c_.rect(b,0,true,token::Outline,r);
    else if(!quiet)c_.rect(b,0,false,type==Control::Key?1:token::Outline,r);
    const char*k=type==Control::Choice?"choice":type==Control::Key?"key":type==Control::Quiet?"quiet":"button";
    record(k,b,r,px,enabled,true);
    // Disabled controls retain readable text, lose their active frame, and reject taps.
    // Never add an underline, strike, pattern fill, or a second state border.
    label({b.x+4,b.y+4,b.w-8,b.h-8},s,px,enabled?500:400,Align::Center,selected&&enabled);
}
void Painter::row(Rect b,const std::string&title,const std::string&detail,const std::string&value,bool selected,bool available,bool chevron){
    c_.line(b.x,b.y+b.h-1,b.x+b.w-1,b.y+b.h-1,0,token::Divider);
    const int inset=selected?16:0;
    if(selected)c_.rect({b.x,b.y+18,4,b.h-36},0,true,1,2);
    int right=chevron?34:8;
    int vw=value.empty()?0:std::min(164,b.w/2);
    label({b.x+inset,b.y+(detail.empty()?8:6),b.w-right-vw-inset,detail.empty()?b.h-16:34},title,token::BodyPx,500);
    if(!detail.empty())label({b.x+inset,b.y+42,b.w-right-inset,26},detail,token::MetaPx,400);
    if(!value.empty())label({b.x+b.w-right-vw,b.y+8,vw,detail.empty()?b.h-16:36},value,token::CaptionPx,available?500:400,Align::Right);
    if(chevron)icon(available?Icon::Next:Icon::Lock,{b.x+b.w-22,b.y+(b.h-18)/2,18,18});
    record("row",b,0,token::BodyPx,available,true);
}
void Painter::icon(Icon kind,Rect b,bool inv,int stroke){
    int col=inv?3:0;
    auto X=[&](double x){return b.x+int(x*b.w/24.0);};auto Y=[&](double y){return b.y+int(y*b.h/24.0);};
    auto L=[&](int a,int z,int x,int y){c_.line(X(a),Y(z),X(x),Y(y),col,stroke);};
    auto R=[&](int x,int y,int w,int h,int r=1){c_.rect({X(x),Y(y),X(x+w)-X(x),Y(y+h)-Y(y)},col,false,stroke,r);};
    switch(kind){
        case Icon::Back:L(18,12,5,12);L(5,12,11,6);L(5,12,11,18);break;
        case Icon::Next:L(8,6,14,12);L(14,12,8,18);break;
        case Icon::Check:L(5,12,10,17);L(10,17,20,6);break;
        case Icon::Book:R(3,3,18,19,2);L(7,3,7,22);L(10,8,17,8);L(10,12,17,12);break;
        case Icon::Folder:L(2,6,9,6);L(9,6,12,9);L(12,9,22,9);L(22,9,22,21);L(22,21,2,21);L(2,21,2,6);break;
        case Icon::File:R(5,2,14,20,1);L(9,8,15,8);L(9,12,15,12);L(9,16,13,16);break;
        case Icon::Settings:c_.circle(X(12),Y(12),b.w*7/24,col,false,stroke);c_.circle(X(12),Y(12),b.w*2/24,col,false,stroke);L(12,2,12,5);L(12,19,12,22);L(2,12,5,12);L(19,12,22,12);break;
        case Icon::Transfer:L(5,8,20,8);L(20,8,16,4);L(20,8,16,12);L(19,17,4,17);L(4,17,8,13);L(4,17,8,21);break;
        case Icon::Usb:L(12,20,12,4);L(12,4,9,8);L(12,4,15,8);L(12,16,6,12);L(6,12,6,9);L(12,12,18,9);L(18,9,18,6);break;
        case Icon::Wifi:c_.arc(X(12),Y(20),b.w*18/24,228,312,col,stroke);c_.arc(X(12),Y(20),b.w*12/24,228,312,col,stroke);c_.arc(X(12),Y(20),b.w*6/24,228,312,col,stroke);c_.circle(X(12),Y(20),2,col,true);break;
        case Icon::Lock:R(5,10,14,11,2);c_.arc(X(12),Y(10),b.w*5/24,180,360,col,stroke);L(12,14,12,17);break;
        case Icon::Refresh:c_.arc(X(12),Y(12),b.w*8/24,35,300,col,stroke);L(17,3,18,9);L(18,9,12,8);break;
        case Icon::Keyboard:R(2,5,20,14,2);for(int y=0;y<2;++y)for(int x=0;x<5;++x)c_.rect({X(5+x*3),Y(9+y*4),2,2},col);L(8,17,16,17);break;
        case Icon::Search:c_.circle(X(10),Y(10),b.w*6/24,col,false,stroke);L(14,14,21,21);break;
        case Icon::ArrowUp:L(12,20,12,4);L(12,4,5,11);L(12,4,19,11);break;
    }
    record("icon",b);
}
void Painter::header(const std::string&s,const std::string&code,bool back){
    if(back){icon(Icon::Back,{38,33,28,28});record("back",{24,20,56,56},0,0,true,true);}
    label({back?96:24,24,back?360:380,48},s,token::TitlePx,500);
    label({24,80,384,24},code,token::AnnotationPx,400);
    c_.rect({440,88,16,5},0,true,1,2);
}
void Painter::section(int y,const std::string&s,const std::string&code){label({24,y,280,28},s,token::CaptionPx,500);if(!code.empty())label({304,y,152,28},code,token::AnnotationPx,400,Align::Right);}
void Painter::footer(const std::string&s){label({24,token::StatusTop,432,24},s,token::AnnotationPx,400);}
void Painter::empty(Icon i,const std::string&s,const std::string&hint,int y){
    c_.circle(240,y+40,58,0,false,2);icon(i,{216,y+16,48,48},false,3);
    label({24,y+128,432,44},s,26,500,Align::Center);
    paragraph({48,y+192,384,96},hint,20,400,32);
}
void Painter::segments(Rect b,int completed,int total){
    total=std::max(1,std::min(total,64));completed=std::max(0,std::min(completed,total));int gap=4,w=(b.w-(total-1)*gap)/total;
    for(int i=0;i<total;++i)c_.rect({b.x+i*(w+gap),b.y,w,b.h},0,i<completed,1,std::min(2,b.h/2));
    record("segments",b);
}
}}
