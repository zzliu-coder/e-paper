#include "paper/runtime.hpp"
#include <cassert>
#include <iostream>
int main(int argc,char**argv){
    using namespace paper;
    // Every 2x2 tile has exact 0/25/75/100 percent ink coverage.
    for(int a=0;a<4;++a){
        Glyph g;g.width=g.height=4;g.top=4;g.coverage2.assign(4,a*85);
        Canvas c(4,4),inverse(4,4),raw(4,4);c.textDots(true);inverse.textDots(true);inverse.clear(0);
        c.glyph(g,0,4,{0,0,4,4});inverse.glyph(g,0,4,{0,0,4,4},false,true);raw.glyph(g,0,4,{0,0,4,4});
        int ink=0;for(int y=0;y<4;++y)for(int x=0;x<4;++x){auto p=c.pixel(x,y);assert(p==0||p==3);assert(p+inverse.pixel(x,y)==3);ink+=p==0;assert(raw.pixel(x,y)==(a>=2?0:3));}
        const int expected[]={0,4,12,16};assert(ink==expected[a]);
        Canvas clipped(4,4);clipped.textDots(true);clipped.glyph(g,0,4,{1,1,2,2});assert(clipped.pixel(0,0)==3);assert(clipped.pixel(3,3)==3);
    }
    assert(argc==2);HostHardware hw;Runtime r(argv[1],hw);assert(r.initialize());
    assert(!r.action("gray-test","mono"));assert(!r.action("gray-test-next"));
    DisplayJob j;uint32_t crc=0;
    for(auto mode:{"mono","dots","mono"}){
        assert(r.action("text-render",mode));assert(r.job(j));assert(!j.gray);
        if(std::string(mode)=="dots")assert(j.frame.checksum()!=crc);else if(crc)assert(j.frame.checksum()==crc);else crc=j.frame.checksum();
        assert(r.complete(j.revision,{}));
    }
    assert(!r.action("text-render","gray4"));
    assert(r.action("text-render","dots"));
    assert(r.action("library"));assert(r.action("open","0"));
    assert(r.job(j));const auto pageCrc=j.frame.checksum();assert(r.complete(j.revision,{}));
    assert(r.action("settings-category","display"));assert(r.action("continue"));
    assert(r.job(j));assert(j.frame.checksum()==pageCrc);assert(r.complete(j.revision,{}));
    for(auto mode:{"mono","dots","mono"}){
        assert(r.action("text-render",mode));assert(r.job(j));assert(!j.gray);
        assert(r.complete(j.revision,{}));
    }
    assert(r.action("text-render","dots"));
    Runtime reopened(argv[1],hw);assert(reopened.initialize());assert(reopened.snapshot().find("\"text_render\":\"dots\"")!=std::string::npos);
    std::cout<<"PASS coverage, inverse, clipping, determinism, binary panel, removed routes, settings persistence\n";
}
