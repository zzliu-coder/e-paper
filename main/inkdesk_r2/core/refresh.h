#pragma once
#include "types.h"
namespace inkdesk {
  enum class Intent:uint8_t {
    Interactive,Page,Probe,Clean,Full
  };
  enum class RefreshMode:uint8_t {
    Fast,Clean,Full
  };
  struct RefreshConfig {
    uint32_t coalesceMs=35,maxWaitMs=100,cleanIdleMs=1400,timeoutMs=7000;
    uint16_t cleanEvery=12;
    bool hasWindow=false;
  };
  struct RefreshJob {
    uint32_t sequence=0,revision=0;
    RefreshMode mode=RefreshMode::Fast;
    Rect requested {},applied {};
  };
  struct RefreshStats {
    uint32_t submitted=0,completed=0,fast=0,clean=0,full=0,coalesced=0,timeouts=0,failures=0,lastDurationMs=0,unchanged=0;
  };
  // Policy only. Never touches hardware or mutates a framebuffer. Single owner.
  class RefreshScheduler {
    RefreshConfig cfg_;
    RefreshStats stats_;
    bool pending_=false,busy_=false,cleanDue_=false,fault_=false;
    uint32_t first_=0,last_=0,started_=0,revision_=0,sequence_=0;
    uint16_t fastSinceClean_=0;
    Intent intent_=Intent::Interactive;
    Rect dirty_ {};
    RefreshJob job_ {};
    public:
    explicit RefreshScheduler(RefreshConfig c= {}):cfg_(c) {}
    void request(Intent i,Rect r,uint32_t rev,uint32_t now) {
      r=clip(r);
      if(r.empty())return;
      if(!pending_) {
        first_=now;
        dirty_=r;
        intent_=i;
      }else {
        dirty_=unite(dirty_,r);
        intent_=std::max(intent_,i);
        ++stats_.coalesced;
      }
      pending_=true;
      last_=now;
      revision_=rev;
    }
    bool ready(uint32_t now)const {
      return pending_&&!busy_&&!fault_&&(intent_>=Intent::Clean||elapsed(now,last_)>=cfg_.coalesceMs||elapsed(now,first_)>=cfg_.maxWaitMs);
    }
    // Visual equality is checked by Engine. Explicit display tests and cleans
    // always reach the driver, even if their pixels match the visible frame.
    bool discardUnchanged() {
      if (!pending_ || busy_ || fault_ || intent_ >= Intent::Probe) return false;
      pending_ = false;
      dirty_ = {};
      intent_ = Intent::Interactive;
      ++stats_.unchanged;
      return true;
    }
    bool start(uint32_t now,bool editing,RefreshJob&out) {
      if(!ready(now))return false;
      RefreshMode mode=intent_==Intent::Full?RefreshMode::Full:intent_==Intent::Clean?RefreshMode::Clean:RefreshMode::Fast;
      // Cleaning is folded into a requested page transition, never a timer-only flash while reading.
      if(mode==RefreshMode::Fast&&cleanDue_&&!editing&&intent_!=Intent::Probe)mode=RefreshMode::Clean;
      job_= {
        ++sequence_,revision_,mode,dirty_,cfg_.hasWindow&&mode==RefreshMode::Fast?dirty_:fullRect()
      };
      busy_=true;
      pending_=false;
      started_=now;
      dirty_= {};
      intent_=Intent::Interactive;
      ++stats_.submitted;
      if(mode==RefreshMode::Fast)++stats_.fast;
      else if(mode==RefreshMode::Clean)++stats_.clean;
      else ++stats_.full;
      out=job_;
      return true;
    }
    bool complete(uint32_t sequence,uint32_t now,bool success) {
      if(!busy_||fault_||job_.sequence!=sequence)return false;
      busy_=false;
      stats_.lastDurationMs=elapsed(now,started_);
      if(!success) {
        ++stats_.failures;
        fault_=true;
        return false;
      }
      ++stats_.completed;
      if(job_.mode==RefreshMode::Fast) {
        if(fastSinceClean_<UINT16_MAX)++fastSinceClean_;
        cleanDue_=fastSinceClean_>=cfg_.cleanEvery;
      }
      else {
        fastSinceClean_=0;
        cleanDue_=false;
      }return true;
    }
    bool timedOut(uint32_t now) {
      if(busy_&&!fault_&&elapsed(now,started_)>=cfg_.timeoutMs) {
        ++stats_.timeouts;
        fault_=true;
        return true;
      }return false;
    }
    // Only call once the driver has independently established that BUSY is low.
    void recover(bool driverIdle,uint32_t rev,uint32_t now) {
      if(!driverIdle)return;
      busy_=false;
      fault_=false;
      request(Intent::Full,fullRect(),rev,now);
    }
    bool busy()const {
      return busy_;
    }bool fault()const {
      return fault_;
    }bool pending()const {
      return pending_;
    }bool cleanDue()const {
      return cleanDue_;
    }
    const RefreshStats&stats()const {
      return stats_;
    }const RefreshConfig&config()const {
      return cfg_;
    }
  };
} // namespace inkdesk
