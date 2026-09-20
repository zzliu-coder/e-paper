#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace paper {
    enum class Error : int {
        Ok=0, Invalid, NotFound, Unsupported, Unavailable, Busy, Io, Corrupt, TooLarge, Conflict, Canceled, Unauthorized, ResourceMissing, BackendFailure
    };
    struct Status {
        Error code=Error::Ok;
        std::string message;
        explicit operator bool() const noexcept {
            return code==Error::Ok;
        }
        static Status ok() {
            return {
            };
        }
        static Status fail(Error c,const std::string& s) {
            return {
                c,s
            };
        }
    };
    const char* errorName(Error);
    std::string jsonString(const std::string&);
    uint32_t crc32(const void*,size_t,uint32_t initial=0);
    std::string sha256(const void*,size_t);
    // Incremental digest shared by file validation; never buffers the full file.
    class Sha256Stream {
        struct Impl;
        std::unique_ptr<Impl> impl_;
    public:
        Sha256Stream();
        ~Sha256Stream();
        Sha256Stream(const Sha256Stream&)=delete;
        Sha256Stream& operator=(const Sha256Stream&)=delete;
        Status add(const void*,size_t);
        Status finish(std::string&);
    };
    Status fileSha256(const std::string&,std::string&,uint64_t maximum=512ull*1024*1024,const std::function<Status(size_t,size_t)>&progress={});
    std::string hex64(uint64_t);
    bool validUtf8(const std::string&);
    struct Rune {
        uint32_t cp=0;
        size_t begin=0,end=0;
    };
    Status readRune(const std::string&,size_t&cursor,Rune&);
    Status decodeUtf8(const std::string&,std::vector<Rune>&,size_t maxCharacters=4000000);
    std::string encodeUtf8(uint32_t);
    size_t previousGrapheme(const std::string&,size_t);
    size_t nextGrapheme(const std::string&,size_t);
    Status safeRelative(const std::string&,std::string&);
    struct Rect {
        int x=0,y=0,w=0,h=0;
        bool contains(int a,int b)const {
            return w>0&&h>0&&a>=x&&b>=y&&a<x+w&&b<y+h;
        }
    };
    struct Budget {
        bool allowTestAssets=false;
        size_t glyphCache=256*1024;
        size_t chapterBytes=768*1024;
        size_t documentIndex=256*1024;
        size_t fontIndex=1536*1024;
        size_t uiFontIndex=256*1024;
        size_t candidateCount=64;
    };
    // A capability describes availability. It is not a memory-protection sandbox.
    struct Capability {
        std::string id;
        bool available=false;
        std::string reason;
    };
    struct Lease {
        uint64_t id=0;
        std::string owner,resource;
        explicit operator bool() const {
            return id!=0;
        }
    };
    class ResourceGate {
        mutable std::mutex mutex_;
        uint64_t next_=1;
        std::map<uint64_t,Lease> leases_;
        std::function<void(bool)> activity_;
        bool observedAwake_=false;
        void notifyActivity();
        public:  // Observer is serialized and must not call back into this gate.
        void setActivityObserver(std::function<void(bool)>);
        Status acquire(const std::string& resource,const std::string& owner,Lease&);
        void release(Lease&);
        bool held(const std::string&)const;
        Status allowUsb()const;
        Status allowSleep()const;
        std::vector<Lease> active()const;
    };
    class LeaseGuard {
        ResourceGate* gate_=nullptr;
        Lease lease_ {
        };
        public: LeaseGuard()=default;
        LeaseGuard(const LeaseGuard&)=delete;
        LeaseGuard& operator=(const LeaseGuard&)=delete;
        ~LeaseGuard() {
            reset();
        }
        Status acquire(ResourceGate& g,const std::string&r,const std::string&o) {
            reset();
            auto s=g.acquire(r,o,lease_);
            if(s)gate_=&g;
            return s;
        }
        void reset() {
            if(gate_)gate_->release(lease_);
            gate_=nullptr;
        }
    };
}
