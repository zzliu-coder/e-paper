#include "journal.h"
namespace inkdesk {
  namespace {
    constexpr const char*path(int slot) {
      return slot==0?"/inkdesk/state.a":"/inkdesk/state.b";
    }
    uint32_t get(const uint8_t*p) {
      return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
    }
    void put(uint8_t*p,uint32_t v) {
      for(int i=0;i<4;++i)p[i]=uint8_t(v>>(i*8));
    }
    bool newer(uint32_t a,uint32_t b) {
      return a!=b&&uint32_t(a-b)<0x80000000u;
    }
  }
  uint32_t crc32(const uint8_t*d,size_t n) {
    uint32_t crc=0xFFFFFFFFu;
    for(size_t i=0;i<n;++i) {
      crc^=d[i];
      for(int b=0;b<8;++b)crc=(crc>>1)^(0xEDB88320u&uint32_t(-int32_t(crc&1)));
    }return ~crc;
  }
  IoStatus Journal::readSlot(int slot,uint32_t&seq,size_t&length) {
    size_t n=0;
    IoStatus s=fs_.read(path(slot),scratch_.data(),scratch_.size(),n);
    if(s!=IoStatus::Ok)return s;
    if (n<24) return IoStatus::Corrupt;
    // A recognizable newer wrapper belongs to another firmware version. Keep
    // it untouched even when the other slot contains an older readable record.
    if (std::memcmp(scratch_.data(),"IDJ",3)==0 && scratch_[3]!='1') return IoStatus::Unsupported;
    if (std::memcmp(scratch_.data(),"IDJ1",4)!=0) return IoStatus::Corrupt;
    if(crc32(scratch_.data(),20)!=get(scratch_.data()+20))return IoStatus::Corrupt;
    if (get(scratch_.data()+4)!=1) return IoStatus::Unsupported;
    seq=get(scratch_.data()+8);
    length=get(scratch_.data()+12);
    if(length>kMaxPayload||n!=length+24||get(scratch_.data()+16)!=crc32(scratch_.data()+24,length))return IoStatus::Corrupt;
    return IoStatus::Ok;
  }
  IoStatus Journal::load(uint8_t*out,size_t capacity,size_t&size) {
    size=0;
    active_=-1;
    sequence_=0;
    loaded_=true;
    blocked_=false;
    if(!out&&capacity) {
      blocked_=true;
      return IoStatus::Error;
    }
    bool unavailable=false,corrupt=false,unsupported=false;
    for(int i=0;i<2;++i) {
      uint32_t seq=0;
      size_t len=0;
      IoStatus s=readSlot(i,seq,len);
      if(s==IoStatus::Unavailable||s==IoStatus::Error) {
        unavailable=true;
        continue;
      }
      if(s==IoStatus::Missing)continue;
      if(s==IoStatus::TooLarge||s==IoStatus::Unsupported) {
        unsupported=true;
        continue;
      }
      if(s!=IoStatus::Ok) {
        corrupt=true;
        continue;
      }
      if(len>capacity) {
        blocked_=true;
        return IoStatus::TooLarge;
      }
      if(active_<0||newer(seq,sequence_)) {
        active_=i;
        sequence_=seq;
        size=len;
        if(len)std::memcpy(out,scratch_.data()+24,len);
      }
    }
    if(active_>=0) {
      blocked_=unavailable||unsupported;
      return IoStatus::Ok;
    }
    blocked_=corrupt||unavailable||unsupported;
    return unavailable?IoStatus::Unavailable:unsupported?IoStatus::Unsupported:corrupt?IoStatus::Corrupt:IoStatus::Missing;
  }
  bool Journal::save(const uint8_t*data,size_t size) {
    // Caller must load first: never overwrite an unseen existing journal.
    if(!loaded_||blocked_||size>kMaxPayload||(!data&&size))return false;
    std::memcpy(scratch_.data(),"IDJ1",4);
    put(scratch_.data()+4,1);
    uint32_t next=sequence_+1;
    put(scratch_.data()+8,next);
    put(scratch_.data()+12,uint32_t(size));
    uint32_t crc=crc32(data,size);
    put(scratch_.data()+16,crc);
    put(scratch_.data()+20,crc32(scratch_.data(),20));
    if(size)std::memcpy(scratch_.data()+24,data,size);
    int target=active_==0?1:0;
    if(!fs_.write(path(target),scratch_.data(),size+24))return false;
    uint32_t verified=0;
    size_t len=0;
    if(readSlot(target,verified,len)!=IoStatus::Ok||verified!=next||len!=size||get(scratch_.data()+16)!=crc||
    (size&&std::memcmp(scratch_.data()+24,data,size)!=0))return false;
    sequence_=next;
    active_=target;
    return true;
  }
}
