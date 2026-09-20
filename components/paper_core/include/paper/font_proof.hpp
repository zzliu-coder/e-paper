#pragma once
#include "storage.hpp"
#include <cstdio>
#include <array>
#include <memory>
namespace paper {
// The proof root lives in firmware, never in the mutable SD manifest.
// Header/index verified eagerly; every bitmap block verified before use.
class FontProof {
    struct Block {size_t offset=SIZE_MAX,size=0;uint64_t used=0;std::unique_ptr<uint8_t[]> bytes;};
    std::unique_ptr<uint8_t[]> proof_;
    size_t proofSize_=0;
    uint64_t tick_=0;
    std::array<Block,8> cache_;
    uint32_t dataOffset_=0,dataLength_=0;
public:
    std::string identity;
    size_t reservedBytes()const{return proofSize_+cache_.size()*2048+sizeof(*this);}
    Status open(Store&,const std::string&,int px,int weight,size_t indexBudget,std::vector<uint8_t>& index);
    Status read(FILE*,uint32_t offset,uint32_t length,std::vector<uint8_t>&out);
};
}
