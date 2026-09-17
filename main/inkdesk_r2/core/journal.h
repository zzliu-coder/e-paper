#pragma once
#include "types.h"
namespace inkdesk {
  constexpr size_t kMaxPayload=16384;
  enum class IoStatus:uint8_t {
    Ok,Missing,Unavailable,TooLarge,Error,Corrupt,Unsupported
  };
  class FileStore {
    public:
    virtual ~FileStore()=default;
    virtual IoStatus read(const char*path,uint8_t*out,size_t capacity,size_t&size)=0;
    // Return success only after flush + close. Journal independently reads it back.
    virtual bool write(const char*path,const uint8_t*data,size_t size)=0;
  };
  uint32_t crc32(const uint8_t*data,size_t size);
  // Two independent slots: writing the inactive slot never truncates the last valid slot.
  // This protects against interrupted file writes; SD-card controller/FAT corruption
  // can still damage both slots. It is not a hardware power-loss guarantee.
  class Journal {
    FileStore&fs_;
    std::array<uint8_t,kMaxPayload+24> scratch_ {};
    uint32_t sequence_=0;
    int active_=-1;
    bool loaded_=false,blocked_=false;
    IoStatus readSlot(int slot,uint32_t&sequence,size_t&length);
    public:
    explicit Journal(FileStore&fs):fs_(fs) {}
    IoStatus load(uint8_t*out,size_t capacity,size_t&size);
    bool save(const uint8_t*data,size_t size);
    // Preserve unknown/newer payload formats until an explicit migration is supplied.
    void blockWrites() {
      blocked_=true;
    }
    bool writesBlocked()const {
      return blocked_;
    }
    uint32_t sequence()const {
      return sequence_;
    }
  };
}
