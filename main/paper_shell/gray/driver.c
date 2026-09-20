#include "driver.h"
#include <string.h>
/* MIT: Copyright (c) 2026 FreeInk. Byte-exact selector LUT from FreeInk SDK
 * 094976e1d47ad7120cf461fec5f6b737eaabf13f / Ssd1677Luts.h lut_grayscale.
 * See adjacent LICENSE.FreeInk. No ad-hoc voltage or timing tuning. */
static const uint8_t lut[112]={
 0,0,0,0,0,0,0,0,0,0,
 0x54,0x54,0x40,0,0,0,0,0,0,0,
 0xAA,0xA0,0xA8,0,0,0,0,0,0,0,
 0xA2,0x22,0x20,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,
 1,1,1,1,0, 1,1,1,1,0, 1,1,1,1,0,
 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0,
 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0,
 0x8F,0x8F,0x8F,0x8F,0x8F, 0x17,0x41,0xA8,0x32,0x30,0,0
};
const uint8_t* fb_gray_lut(size_t* n){if(n)*n=sizeof(lut);return lut;}
void fb_gray_init(fb_gray_state* s){if(s)memset(s,0,sizeof(*s));}
int fb_selector(uint8_t l,uint8_t* b,uint8_t* lo,uint8_t* hi){
 if(!b||!lo||!hi||(l!=0&&l!=85&&l!=170&&l!=255))return FB_ARGUMENT;
 *b=l==255;*lo=l==85;*hi=l==85||l==170;return FB_OK;
}
int fb_encode_planes(const uint8_t* p,size_t n,uint8_t* b,uint8_t* lo,uint8_t* hi){
 if(!p||!b||!lo||!hi||b==lo||b==hi||lo==hi||n!=FB_FRAME_BYTES*8)return FB_ARGUMENT;
 /* Validate before modifying any destination. */
 for(size_t i=0;i<n;++i)if(p[i]!=0&&p[i]!=85&&p[i]!=170&&p[i]!=255)return FB_ARGUMENT;
 memset(b,0,FB_FRAME_BYTES);memset(lo,0,FB_FRAME_BYTES);memset(hi,0,FB_FRAME_BYTES);
 for(size_t i=0;i<n;++i){uint8_t x,y,z;fb_selector(p[i],&x,&y,&z);const uint8_t bit=(uint8_t)(128u>>(i&7));
  if(x)b[i>>3]|=bit;
  if(y)lo[i>>3]|=bit;
  if(z)hi[i>>3]|=bit;
 }
 return FB_OK;
}
static int fail(fb_gray_state* s,int error){s->fault=true;s->baseline_synced=false;s->last.baseline_synced=false;s->last.success=false;s->last.error=error;return error;}
static int tx(fb_gray_state* s,const fb_bus* b,int cmd,const uint8_t* p,size_t n){if(s->fault)return FB_FAULT;if(b->write(b->context,cmd,p,n)!=0)return fail(s,FB_IO);return FB_OK;}
static int one(fb_gray_state* s,const fb_bus* b,int cmd,uint8_t value){return tx(s,b,cmd,&value,1);}
#define TRY(x) do {int e_=(x);if(e_!=FB_OK)return e_;}while(0)
static int idle(fb_gray_state* s,const fb_bus* b){if(s->fault)return FB_FAULT;return b->wait_idle(b->context,FB_GRAY_TIMEOUT_MS)==0?FB_OK:fail(s,FB_TIMEOUT);}
static int area(fb_gray_state* s,const fb_bus* b){
 const uint8_t xr[]={0,0,0x1f,0x03},yr[]={0xdf,1,0,0},xc[]={0,0},yc[]={0xdf,1};
 TRY(one(s,b,0x11,0x01));TRY(tx(s,b,0x44,xr,4));TRY(tx(s,b,0x45,yr,4));TRY(tx(s,b,0x4E,xc,2));return tx(s,b,0x4F,yc,2);
}
static int ram(fb_gray_state* s,const fb_bus* b,int command,const uint8_t* data,int fill){
 uint8_t chunk[128];if(!data)memset(chunk,fill,sizeof(chunk));
 TRY(area(s,b));TRY(tx(s,b,command,NULL,0));
 for(size_t offset=0;offset<FB_FRAME_BYTES;offset+=sizeof(chunk)){
  size_t n=FB_FRAME_BYTES-offset;if(n>sizeof(chunk))n=sizeof(chunk);
  TRY(tx(s,b,-1,data?data+offset:chunk,n));
 }
 return FB_OK;
}
static int activate(fb_gray_state* s,const fb_bus* b,uint8_t seq){
 if(s->last.phases>=FB_GRAY_PHASES)return fail(s,FB_ARGUMENT);
 fb_phase* p=&s->last.phase[s->last.phases++];p->sequence=seq;uint32_t start=b->millis(b->context);
 TRY(one(s,b,0x22,seq));s->powered=true;TRY(tx(s,b,0x20,NULL,0));
 const int e=idle(s,b);p->elapsed_ms=b->millis(b->context)-start;
 if(e!=FB_OK)return e;
 p->complete=true;s->powered=(seq&3)==0;return FB_OK;
}
static int bw(fb_gray_state* s,const fb_bus* b,const uint8_t* target,int target_fill,const uint8_t* prev,int prev_fill,uint8_t seq){
 const uint8_t ctrl[]={seq==0xF7?0x40:0,0};
 TRY(idle(s,b));TRY(ram(s,b,0x26,prev,prev_fill));TRY(ram(s,b,0x24,target,target_fill));
 TRY(tx(s,b,0x21,ctrl,2));TRY(one(s,b,0x18,0x80));TRY(one(s,b,0x3C,seq==0xF7?0x01:0x80));
 return activate(s,b,seq);
}
static int sync_base(fb_gray_state* s,const fb_bus* b,const uint8_t* base){
 TRY(ram(s,b,0x24,base,0));TRY(ram(s,b,0x26,base,0));s->baseline_synced=true;return FB_OK;
}
static int clean(fb_gray_state* s,const fb_bus* b,const uint8_t* target,bool full){
 if(full){TRY(bw(s,b,target,0,NULL,255,0xF7));}
 else{
  TRY(bw(s,b,NULL,0,NULL,255,0xFC)); /* white previous -> black */
  TRY(bw(s,b,target,0,NULL,0,0xFC)); /* black -> target */
 }
 TRY(sync_base(s,b,target));s->gray_residue=false;s->fast_since_clean=0;s->last.cleaned=true;return FB_OK;
}
static int overlay(fb_gray_state* s,const fb_bus* b,const uint8_t* base,const uint8_t* lo,const uint8_t* hi){
 TRY(ram(s,b,0x24,lo,0));TRY(ram(s,b,0x26,hi,0));s->baseline_synced=false;
 TRY(tx(s,b,0x32,lut,105));TRY(one(s,b,0x03,lut[105]));TRY(tx(s,b,0x04,lut+106,3));TRY(one(s,b,0x2C,lut[109]));TRY(one(s,b,0x3C,0x80));
 const uint8_t ctrl[]={0,0};TRY(tx(s,b,0x21,ctrl,2));
 /* Mark possible physical residue BEFORE activation. Failure cannot erase this fact. */
 s->gray_residue=true;TRY(activate(s,b,0xCC));
 TRY(ram(s,b,0x26,base,0));s->baseline_synced=true;return FB_OK;
}
static bool valid(const fb_gray_state* s,const fb_bus* b){return s&&b&&b->write&&b->wait_idle&&b->millis;}
int fb_gray_present(fb_gray_state* s,const fb_bus* b,const uint8_t* base,const uint8_t* lo,const uint8_t* hi,bool gray,bool full){
 if(!valid(s,b)||!base||(gray&&(!lo||!hi)))return FB_ARGUMENT;
 if(s->fault)return FB_FAULT;
 memset(&s->last,0,sizeof(s->last));s->last.grayscale=gray;const uint32_t started=b->millis(b->context);
 TRY(idle(s,b));
 const bool needs=full||!s->active||!s->baseline_synced||s->fast_since_clean>=FB_GRAY_MAX_FAST||(!gray&&s->gray_residue);
 if(needs){TRY(clean(s,b,base,full||!s->active));}
 else { /* RED already holds the committed B/W baseline. */
  TRY(ram(s,b,0x24,base,0));const uint8_t ctrl[]={0,0};TRY(tx(s,b,0x21,ctrl,2));TRY(one(s,b,0x18,0x80));TRY(one(s,b,0x3C,0x80));TRY(activate(s,b,0xFC));TRY(sync_base(s,b,base));++s->fast_since_clean;
 }
 s->active=true;if(gray){TRY(overlay(s,b,base,lo,hi));TRY(one(s,b,0x3C,0x80));TRY(activate(s,b,0x83));}
 s->last.success=true;s->last.baseline_synced=s->baseline_synced;s->last.total_ms=b->millis(b->context)-started;
 /* physical_quality_verified stays false: only a person can confirm the screen. */
 return FB_OK;
}
int fb_gray_restore(fb_gray_state* s,const fb_bus* b,const uint8_t* base){
 if(!valid(s,b)||!base)return FB_ARGUMENT;
 if(s->fault)return FB_FAULT;
 if(!s->active)return FB_OK;
 memset(&s->last,0,sizeof(s->last));uint32_t started=b->millis(b->context);
 TRY(idle(s,b));TRY(clean(s,b,base,true));TRY(one(s,b,0x3C,0x80));TRY(activate(s,b,0x83));
 s->active=false;s->gray_residue=false;s->baseline_synced=false;s->last.success=true;s->last.total_ms=b->millis(b->context)-started;return FB_OK;
}
