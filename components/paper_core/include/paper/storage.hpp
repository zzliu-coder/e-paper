#pragma once
#include <cstdio>
#include <atomic>
#include "common.hpp"
namespace paper {
    struct FileEntry {
        std::string name,path;
        uint64_t size=0;
        bool directory=false;
    };
    class Store {
        std::string root_;
        ResourceGate& gate_;
        mutable std::recursive_mutex mutex_;
        std::atomic<uint64_t> resourceGeneration_{1};
        Status resolve(const std::string&,std::string&,bool allowMissing)const;
        Status publish(const std::string&,const void*,size_t,bool replace,bool alternateRecord);
        public: Store(std::string root,ResourceGate& gate):root_(std::move(root)),gate_(gate) {
        }
        const std::string& root()const {
            return root_;
        }
        ResourceGate& gate() {
            return gate_;
        }
        Status initialize();
        uint64_t resourceGeneration() const { return resourceGeneration_.load(); }
        void invalidateResources() { ++resourceGeneration_; }
        Status path(const std::string& r,std::string& abs,bool missing=false)const {
            return resolve(r,abs,missing);
        }
        Status read(const std::string&,std::vector<uint8_t>&,size_t cap=1024*1024);
        Status write(const std::string&,const void*,size_t,bool replace=false);
        // Rebuildable CrossMux cache only; may retire an older derived file on FAT.
        Status writeReaderCache(const std::string&,const void*,size_t);
        Status list(const std::string&,std::vector<FileEntry>&,size_t cap=1024);
        Status renameFile(const std::string&,const std::string&);
        Status removeTemporary(const std::string&);
        Status hash(const std::string&,std::string&,const std::function<Status(size_t,size_t)>&progress={});
        // Two committed slots, CRC and monotonic generation. Corrupt newer slot falls back.
        Status loadRecord(const std::string& key,std::string&payload,uint64_t* generation=nullptr);
        Status saveRecord(const std::string& key,const std::string&payload);
    };
    struct TransferInfo {
        uint64_t id=0,received=0,expected=0;
        std::string path,phase="receiving",sha;
    };
    class Transfers {
        struct Upload {
            TransferInfo info;
            FILE* file=nullptr;
            Lease sd {
            }
            ,awake {
            };
            std::string temp;
        };
        Store& store_;
        ResourceGate& gate_;
        std::map<uint64_t,Upload> uploads_;
        uint64_t next_=1;
        bool publishReaderFonts_=false;
        mutable std::mutex mutex_;
        void release(Upload&);
        public: Transfers(Store&s,bool publishReaderFonts=false):store_(s),gate_(s.gate()),publishReaderFonts_(publishReaderFonts) {
        }
        ~Transfers();
        Status begin(const std::string& destination,uint64_t length,const std::string& sha,uint64_t& id);
        Status append(uint64_t id,uint64_t offset,const void*,size_t n);
        Status commit(uint64_t id,TransferInfo&receipt,const std::function<Status(size_t,size_t)>&progress={});
        Status cancel(uint64_t id);
        Status status(uint64_t id,TransferInfo&)const;
        void cancelAll();
    };
}
