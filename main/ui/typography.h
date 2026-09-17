#pragma once
#include "components.h"
#include "font_metrics.h"
namespace paper {
inline bool Closing(uint32_t c){return c==0x3002||c==0xff0c||c==0xff01||c==0xff1f||c==0xff1b||c==0xff1a||c==0x201d||c==0x300b||c==0xff09;}
inline bool Opening(uint32_t c){return c==0x201c||c==0x300a||c==0xff08;}
inline size_t LineEnd(const char* s,size_t from,int size,int theme,int width){
    size_t p=from,prev=from;int used=0;uint32_t previous=0;
    while(s[p]){
        uint32_t cp=0;size_t n=inkdesk::utf8Next(s+p,std::strlen(s+p),cp);if(!n)break;
        if(cp=='\n')return p+n;
        int advance=Advance(cp,size,theme);
        if(used+advance>width||p+n-from>90){
            if(p==from)return p+n;
            if(prev>from&&(Closing(cp)||Opening(previous)))return prev;
            return p;
        }
        used+=advance;prev=p;previous=cp;p+=n;
    }
    return p;
}
inline size_t BodyPage(const char* s,size_t from,int size,int theme,Frame* f=nullptr){
    for(int y=token::ReaderTop;s[from]&&y+size+token::TextExtra<=token::ReaderBottom;y+=size+token::ReaderGap){
        size_t end=LineEnd(s,from,size,theme,token::ReaderWidth);if(end<=from)break;
        inkdesk::Text<96> line;size_t n=end-from;if(n&&s[end-1]=='\n')--n;
        line.assign(s+from,n);if(f)Text(*f,24,y,token::ReaderWidth,line.c_str(),size);from=end;
    }
    return from;
}
struct Pagination {
    std::array<size_t,65> offsets{};int count=0;
    Pagination(const char* text,int size,int theme){
        size_t p=0;while(text[p]&&count<64){offsets[count++]=p;size_t n=BodyPage(text,p,size,theme);if(n<=p)break;p=n;}
        offsets[count]=p;if(!count)count=1;
    }
    int Containing(size_t anchor)const{int i=0;while(i+1<count&&offsets[i+1]<=anchor)++i;return i+1;}
};
}
