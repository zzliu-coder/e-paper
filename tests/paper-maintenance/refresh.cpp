#include "driver.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <array>
#include <algorithm>
struct Bus {int writes=0,waits=0,failWrite=0,failWait=0;uint32_t time=0;int prepares=0;bool failPrepare=false;std::vector<int> sequences;int pingpong=0x40;bool board=false;};
static int write(void*p,int cmd,const uint8_t*d,size_t n){auto&b=*static_cast<Bus*>(p);if(cmd==0x37){assert(n==10);b.pingpong=d[5];}if(b.board&&(cmd==0x24||cmd==0x26||cmd==0x20))assert(b.pingpong==0);if(cmd==0x22&&n==1)b.sequences.push_back(*d);return ++b.writes==b.failWrite?-1:0;}
static int prepare(void*p){auto&b=*static_cast<Bus*>(p);++b.prepares;return b.failPrepare?FB_IO:FB_OK;}
static int wait(void*p,uint32_t ms){assert(ms==FB_GRAY_TIMEOUT_MS);auto&b=*static_cast<Bus*>(p);b.time+=5;return ++b.waits==b.failWait?-1:0;}
static uint32_t millis(void*p){return static_cast<Bus*>(p)->time;}
// Replay the actual RAM stream, not only the order of command numbers.
struct Replay {
 std::array<uint8_t,FB_FRAME_BYTES> bw{},red{};
 const uint8_t *base,*lo,*hi;size_t offset=0;int command=0,phase=0,entries=0;
};
static int enter(void*p){auto&r=*static_cast<Replay*>(p);++r.entries;r.phase=0;return FB_OK;}
static int replay(void*p,int cmd,const uint8_t*d,size_t n){
 auto&r=*static_cast<Replay*>(p);
 if(cmd>=0){r.command=cmd;r.offset=0;}
 if(cmd==-1){assert(r.command==0x24||r.command==0x26);assert(r.offset+n<=FB_FRAME_BYTES);
  auto&dst=r.command==0x24?r.bw:r.red;std::copy(d,d+n,dst.begin()+r.offset);r.offset+=n;}
 if(cmd==0x44){assert(n==4&&d[0]==0&&d[1]==0&&d[2]==0x1f&&d[3]==3);}
 if(cmd==0x45){assert(n==4&&d[0]==0xdf&&d[1]==1&&d[2]==0&&d[3]==0);}
 if(cmd==0x22){
  const int seq[]={0xFC,0xFC,0xCC,0x83};assert(r.phase<4&&*d==seq[r.phase]);
  for(size_t i=0;i<FB_FRAME_BYTES;++i){
   if(r.phase==0){assert(r.bw[i]==0&&r.red[i]==255);}
   if(r.phase==1){assert(r.bw[i]==r.base[i]&&r.red[i]==0);}
   if(r.phase==2){assert(r.bw[i]==r.lo[i]&&r.red[i]==r.hi[i]);}
  }++r.phase;
 }
 assert(cmd!=0x3F);return FB_OK;
}
static int replayWait(void*,uint32_t ms){assert(ms==FB_GRAY_TIMEOUT_MS);return FB_OK;}
static uint32_t replayTime(void*){return 0;}
static int isolatedEnter(void*p){auto&b=*static_cast<Bus*>(p);b.pingpong=0;++b.prepares;return b.failPrepare?FB_IO:FB_OK;}
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
    bus.prepare_bw=prepare;fb_gray_init(&s);b={};b.board=true;
    assert(present(true,false)==FB_OK);assert(b.prepares==1);
    assert((b.sequences==std::vector<int>{0xC4,0xCC,0x83}));
    assert(present(true,false)==FB_OK);assert(b.prepares==2&&s.last.cleaned);
    b.sequences.clear();assert(fb_gray_restore(&s,&bus,base.data())==FB_OK);
    assert((b.sequences==std::vector<int>{0xC4,0x83}));
    fb_gray_init(&s);b={};b.board=true;assert(present(true,true)==FB_OK);const int boardWaits=b.waits,boardWrites=b.writes;
    for(int i=1;i<=boardWaits;++i){fb_gray_init(&s);b={};b.board=true;b.failWait=i;assert(present(true,true)==FB_TIMEOUT);assert(s.fault&&!s.last.success);int previous=b.writes;assert(present(false,false)==FB_FAULT&&b.writes==previous);}
    for(int i:{1,20,400,800,boardWrites}){fb_gray_init(&s);b={};b.board=true;b.failWrite=i;assert(present(true,true)==FB_IO&&s.fault);}
    fb_gray_init(&s);b={};b.board=true;b.failPrepare=true;assert(present(true,true)==FB_IO&&s.fault&&!s.last.success);
    bus.begin_gray=isolatedEnter;fb_gray_init(&s);b={};b.board=true;
    assert(present(true,true)==FB_OK&&b.prepares==1);
    assert((b.sequences==std::vector<int>{0xFC,0xFC,0xCC,0x83}));
    const int isolatedWaits=b.waits,isolatedWrites=b.writes;
    for(int i=1;i<=isolatedWaits;++i){fb_gray_init(&s);b={};b.board=true;b.failWait=i;assert(present(true,true)==FB_TIMEOUT);int previous=b.writes,entries=b.prepares;assert(present(true,true)==FB_FAULT&&b.writes==previous&&b.prepares==entries);if(i==1)assert(entries==0);}
    for(int i:{1,20,400,800,isolatedWrites}){fb_gray_init(&s);b={};b.board=true;b.failWrite=i;assert(present(true,true)==FB_IO&&s.fault);}
    fb_gray_init(&s);b={};b.board=true;b.failPrepare=true;assert(present(true,true)==FB_IO&&s.fault&&!s.last.success);
    Replay r;r.base=base.data();r.lo=lo.data();r.hi=hi.data();fb_bus rb{&r,replay,replayWait,replayTime,nullptr,enter};fb_gray_init(&s);
    for(int i=0;i<3;++i)assert(fb_gray_present(&s,&rb,base.data(),lo.data(),hi.data(),true,false)==FB_OK&&r.phase==4);
    assert(r.entries==3);
    std::cout<<"PASS: gray encoding, valid BW baseline reuse, periodic cleaning, gray-to-BW cleanup, restore, all BUSY failure phases, SPI failures; physical quality NOT_PROVEN\n";
}
