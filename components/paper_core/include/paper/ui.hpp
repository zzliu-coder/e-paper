#pragma once
#include "text.hpp"
#include "generated/ui_tokens.hpp"
namespace paper { namespace ui {
enum class Align { Left, Center, Right };
enum class Control { Action, Compact, Choice, Key, Quiet };
enum class Icon { Back, Next, Book, Folder, Settings, Transfer, Wifi, Usb, Check, Lock, Refresh, Keyboard, ArrowUp, File, Search };
struct Node {
    std::string kind;
    Rect box;
    int radius=0,px=0;
    bool enabled=true,interactive=false,elided=false;
};
// Logical rectangles are also the native hit rectangles. No CSS or screenshot assets.
class Painter {
    Canvas& c_; FontProvider& f_; std::string& error_; std::vector<Node>* trace_;
    void record(const char*,Rect,int radius=0,int px=0,bool enabled=true,bool interactive=false,bool elided=false);
public:
    Painter(Canvas&c,FontProvider&f,std::string&e,std::vector<Node>*t=nullptr):c_(c),f_(f),error_(e),trace_(t){}
    void label(Rect,const std::string&,int px=token::BodyPx,int weight=500,Align=Align::Left,bool inverse=false);
    void paragraph(Rect,const std::string&,int px=token::BodyPx,int weight=500,int line=32,bool inverse=false);
    void control(Rect,const std::string&,Control=Control::Action,bool selected=false,bool enabled=true,int px=token::BodyPx);
    void row(Rect,const std::string&,const std::string& detail="",const std::string& value="",bool selected=false,bool available=true,bool chevron=true);
    void icon(Icon,Rect,bool inverse=false,int stroke=2);
    void section(int y,const std::string&,const std::string& code="");
    void header(const std::string&,const std::string&,bool back=true);
    void footer(const std::string&);
    void empty(Icon,const std::string&,const std::string&,int y=260);
    void segments(Rect,int completed,int total=28);
};
namespace layout {
inline Rect row(int index,int top=144,int height=token::ListRowHeight){return {token::Margin,top+index*height,token::ContentWidth,height};}
inline Rect grid(int index,int top=188,int cols=5,int h=56,int gapY=8){int gapX=cols<=3?token::Gutter:8;int w=(token::ContentWidth-(cols-1)*gapX)/cols;return {token::Margin+(index%cols)*(w+gapX),top+(index/cols)*(h+gapY),w,h};}
inline Rect pair(int index,int y,int h=64){return {token::Margin+index*(token::Column+token::Gutter),y,token::Column,h};}
inline Rect body(int margin=token::Margin){return {margin,token::ReaderBodyTop,token::Width-2*margin,token::ReaderBodyHeight};}
}
}}
