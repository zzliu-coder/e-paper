#include "driver.h"
#include <cassert>
#include <iostream>
#include <vector>
struct Bus {int writes=0,waits=0,failWrite=0,failWait=0;uint32_t time=0;};
static int write(void*p,int,const uint8_t*,size_t){auto&b=*static_cast<Bus*>(p);return ++b.writes==b.failWrite?-1:0;}
static int wait(void*p,uint32_t ms){assert(ms==FB_GRAY_TIMEOUT_MS);auto&b=*static_cast<Bus*>(p);b.time+=5;return ++b.waits==b.failWait?-1:0;}
static uint32_t millis(void*p){return static_cast<Bus*>(p)->time;}
int main(){
    std::vector<uint8_t> native(FB_FRAME_BYTES*8,255),base(FB_FRAME_BYTES),lo(FB_FRAME_BYTES),hi(FB_FRAME_BYTES);
    native[0]=0;native[1]=85;native[2]=170;native[3]=255;
    assert(fb_encode_planes(native.data(),native.size(),base.data(),lo.data(),hi.data())==FB_OK);
    assert(base[0]==0x1f&&lo[0]==0x40&&hi[0]==0x60);
    auto unchanged=base;native[0]=42;assert(fb_encode_planes(native.data(),native.size(),base.data(),lo.data(),hi.data())==FB_ARGUMENT);assert(base==unchanged);
    Bus b;fb_bus bus{&b,write,wait,millis};fb_gray_state s;fb_gray_init(&s);
    auto present=[&](bool gray,bool full){return fb_gray_present(&s,&bus,base.data(),lo.data(),hi.data(),gray,full);};
    assert(present(false,false)==FB_OK&&s.last.cleaned&&s.baseline_synced);
    for(unsigned n=0;n<FB_GRAY_MAX_FAST;++n)assert(present(false,false)==FB_OK&&!s.last.cleaned);
    assert(present(false,false)==FB_OK&&s.last.cleaned);
    assert(present(true,false)==FB_OK&&s.gray_residue&&!s.last.physical_quality_verified);
    assert(present(false,false)==FB_OK&&s.last.cleaned&&!s.gray_residue);
    assert(present(true,true)==FB_OK);
    assert(fb_gray_restore(&s,&bus,base.data())==FB_OK&&!s.active&&!s.gray_residue&&!s.baseline_synced);
    assert(present(false,false)==FB_OK&&s.last.cleaned);
    // Every BUSY boundary of a gray transaction is fault-injected separately.
    fb_gray_init(&s);b={};assert(present(true,true)==FB_OK);const int waits=b.waits,writes=b.writes;
    for(int i=1;i<=waits;++i){fb_gray_init(&s);b={};b.failWait=i;assert(present(true,true)==FB_TIMEOUT);assert(s.fault&&!s.baseline_synced&&!s.last.success);assert(present(false,false)==FB_FAULT);}
    for(int i:{1,20,400,800,writes}){fb_gray_init(&s);b={};b.failWrite=i;assert(present(true,true)==FB_IO);assert(s.fault&&!s.baseline_synced&&!s.last.success);}
    fb_gray_init(&s);b={};assert(present(false,true)==FB_OK);
    std::cout<<"PASS: gray encoding, valid BW baseline reuse, periodic cleaning, gray-to-BW cleanup, restore, all BUSY failure phases, SPI failures; physical quality NOT_PROVEN\n";
}
