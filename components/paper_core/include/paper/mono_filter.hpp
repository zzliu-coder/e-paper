#pragma once
#include "paper/text.hpp"
namespace paper {
// Coverage conversion only: mode 0 threshold, 2 ordered dots.
inline void monoFilter(Canvas& c,Rect box,int mode,bool inverse=false){
    Canvas source=c;
    const int order[2][2]={{0,2},{3,1}};
    for(int y=box.y;y<box.y+box.h;++y)for(int x=box.x;x<box.x+box.w;++x){
        int p=source.pixel(x,y);if(inverse)p=3-p;
        bool white=p>=2;
        if(mode==2){const int coverage[4]={0,1,3,4};white=order[y&1][x&1]<coverage[p];}
        c.pixel(x,y,(white!=inverse)?3:0);
    }
}
}
