#include "paper/font_proof.hpp"
#include "paper/font_proof_anchors.hpp"
#include <cstring>
namespace paper {
namespace {
uint32_t u32(const uint8_t*p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
std::string hex(const uint8_t*p){const char*d="0123456789abcdef";std::string s;for(int i=0;i<32;++i){s+=d[p[i]>>4];s+=d[p[i]&15];}return s;}
Status bad(){return Status::fail(Error::Corrupt,"字体分块校验失败，请重新安装对应字体资源");}
}
Status FontProof::open(Store&store,const std::string&relative,int px,int weight,size_t budget,std::vector<uint8_t>&index){
    cache_={};proof_.reset();proofSize_=0;tick_=0;identity.clear();index.clear();dataOffset_=dataLength_=0;
    const char* expected=nullptr;for(const auto&anchor:fontProofAnchors)if(relative==anchor.path){expected=anchor.sha;break;}
    if(!expected)return Status::fail(Error::NotFound,"无固定字体校验索引");
    std::string path;auto st=store.path(relative+".pfv2",path,false);if(!st)return st;
    FILE*pf=fopen(path.c_str(),"rb");if(!pf)return Status::fail(Error::NotFound,"缺少字体校验索引，执行全量校验");
    if(fseek(pf,0,SEEK_END)){fclose(pf);return bad();}
    const long length=ftell(pf);
    if(length<160||length>256*1024||fseek(pf,0,SEEK_SET)){fclose(pf);return bad();}
    proofSize_=size_t(length);proof_.reset(new(std::nothrow)uint8_t[proofSize_]);
    if(!proof_){fclose(pf);return Status::fail(Error::Unavailable,"字体校验索引内存不足");}
    const bool proofRead=fread(proof_.get(),1,proofSize_,pf)==proofSize_;fclose(pf);
    if(!proofRead||sha256(proof_.get(),proofSize_)!=expected||memcmp(proof_.get(),"PFV2",4)||u32(proof_.get()+4)!=2048)return bad();
    auto*header=proof_.get()+16;
    const uint32_t size=u32(proof_.get()+8);dataOffset_=u32(proof_.get()+12);
    if(dataOffset_<80||dataOffset_>size||size>16*1024*1024||dataOffset_-80>budget||
       header[6]+256*header[7]!=px||header[8]+256*header[9]!=weight)return bad();
    dataLength_=size-dataOffset_;
    if(proofSize_!=160+32*((dataLength_+2047)/2048)||dataOffset_-80+reservedBytes()>budget)return bad();
    st=store.path(relative,path,false);if(!st)return st;
    FILE*f=fopen(path.c_str(),"rb");if(!f)return Status::fail(Error::Io,"字库不可读");
    uint8_t actual[80];bool ok=fread(actual,1,80,f)==80&&!memcmp(actual,header,80);
    index.resize(dataOffset_-80);
    ok=ok&&fread(index.data(),1,index.size(),f)==index.size();
    ok=ok&&fseek(f,0,SEEK_END)==0&&ftell(f)==long(size);fclose(f);
    if(!ok||sha256(index.data(),index.size())!=hex(proof_.get()+128))return bad();
    identity=hex(proof_.get()+96);return {};
}
Status FontProof::read(FILE*f,uint32_t offset,uint32_t length,std::vector<uint8_t>&out){
    if(uint64_t(offset)+length>dataLength_)return bad();
    out.resize(length);size_t copied=0;
    while(copied<length){
        size_t at=offset+copied,start=at/2048*2048;
        auto it=cache_.begin();for(;it!=cache_.end()&&it->offset!=start;++it){}
        if(it==cache_.end()){
            it=std::min_element(cache_.begin(),cache_.end(),[](const Block&a,const Block&b){return a.used<b.used;});
            it->offset=SIZE_MAX;it->used=0;
            if(!it->bytes)it->bytes.reset(new(std::nothrow)uint8_t[2048]);
            if(!it->bytes)return Status::fail(Error::Unavailable,"字体块缓存内存不足");
            it->size=std::min<size_t>(2048,dataLength_-start);
            if(fseek(f,dataOffset_+start,SEEK_SET)||fread(it->bytes.get(),1,it->size,f)!=it->size)return Status::fail(Error::Io,"字体块读取失败");
            if(sha256(it->bytes.get(),it->size)!=hex(proof_.get()+160+32*(start/2048)))return bad();
            it->offset=start;
        }
        it->used=++tick_;
        size_t within=at-start,n=std::min<size_t>(length-copied,it->size-within);
        memcpy(out.data()+copied,it->bytes.get()+within,n);copied+=n;
    }
    return {};
}
}
