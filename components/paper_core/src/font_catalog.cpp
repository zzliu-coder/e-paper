#include "paper/font_catalog.hpp"
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <new>

namespace paper {
namespace {
uint16_t get16(const uint8_t*p){return p[0]|uint16_t(p[1])<<8;
}
uint32_t get32(const uint8_t*p){return get16(p)|uint32_t(get16(p+2))<<16;
}
Status bad(const char*s){return Status::fail(Error::Corrupt,s);
}
std::string fileName(int px,int weight){return "font-"+std::to_string(weight)+"-"+std::to_string(px)+".pgf";
}
bool readExact(FILE*f,void*p,size_t n){return fread(p,1,n,f)==n;
}
struct File {FILE*p=nullptr;
~File(){if(p)fclose(p);
} };
}
bool validFontId(const std::string&s){
    return !s.empty()&&s.size()<=40&&s!="misans"&&s[0]>='a'&&s[0]<='z'&&
        s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==s.npos;
}
bool validFontRevision(const std::string&s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==s.npos;
}
bool ReaderFontFamily::has(int px,int w)const{for(auto&f:faces)if(f.px==px&&f.weight==w)return true;
return false;
}
Status decodeFontManifest(const std::string&s,ReaderFontFamily&out){
    if(s.size()>32768)return bad("字体清单过大");
    ReaderFontFamily f;
    std::istringstream in(s);
    std::string magic,extra;
    size_t n=0;
    if(!(in>>magic>>std::quoted(f.id)>>std::quoted(f.name)>>n)||magic!="PAPERFONT1"||!validFontId(f.id)||f.name.empty()||f.name.size()>96||!validUtf8(f.name)||n<1||n>75)return bad("字体清单身份无效");
    for(unsigned char c:f.name)if(c<32)return bad("字体名称包含控制字符");
    std::set<std::pair<int,int>>seen;
    for(size_t i=0;i<n;++i){FontFaceInfo a;
        if(!(in>>a.px>>a.weight>>a.bytes>>a.sha256)||a.px<16||a.px>40||(a.weight!=400&&a.weight!=500&&a.weight!=700)||a.bytes<80||a.bytes>8*1024*1024||!validFontRevision(a.sha256)||!seen.emplace(a.px,a.weight).second)return bad("字体规格重复、越界或缺少校验");
        f.faces.push_back(a);
    }
    if(in>>extra)return bad("字体清单含额外字段");
    out=std::move(f);
    return {};
}
std::string encodeFontManifest(const ReaderFontFamily&f){
    std::ostringstream o;
    o<<"PAPERFONT1 "<<std::quoted(f.id)<<' '<<std::quoted(f.name)<<' '<<f.faces.size()<<'\n';
    for(auto&a:f.faces)o<<a.px<<' '<<a.weight<<' '<<a.bytes<<' '<<a.sha256<<'\n';
    return o.str();
}
std::string readerFacePath(const std::string&id,const std::string&r,int px,int w){return "paper/reader-fonts/"+id+"/"+r+"/"+fileName(px,w);
}
Status ReaderFontCatalog::resolve(const std::string&id,const std::string&revision,ReaderFontFamily&out){
    if(!validFontId(id))return Status::fail(Error::Invalid,"阅读字体编号无效");
    std::string r=revision;
    if(r.empty()){auto st=store_.loadRecord("reader-font-active-"+id,r);
    if(!st)return st;
    }
    if(!validFontRevision(r))return bad("阅读字体版本无效");
    std::vector<uint8_t>bytes;
    auto st=store_.read("paper/reader-fonts/"+id+"/"+r+"/family.pfm",bytes,32768);
    if(!st)return st;
    ReaderFontFamily f;
    st=decodeFontManifest(std::string(bytes.begin(),bytes.end()),f);
    if(!st)return st;
    if(f.id!=id)return bad("阅读字体目录与身份不一致");
    f.revision=r;
    f.available=true;
    out=std::move(f);
    return {};
}
Status resolveReaderFace(Store&s,const FontSpec&spec,std::string&path,std::string&hash){
    ReaderFontFamily f;
    ReaderFontCatalog c(s);
    auto st=c.resolve(spec.family,spec.revision,f);
    if(!st)return st;
    for(auto&a:f.faces)if(a.px==spec.px&&a.weight==spec.weight){path=readerFacePath(f.id,f.revision,spec.px,spec.weight);
    hash=a.sha256;
    return {};
    }
    return Status::fail(Error::ResourceMissing,"此字体没有请求的字号或字重；保留原配置");
}
Status ReaderFontCatalog::list(std::vector<ReaderFontFamily>&out){
    out.clear();
    ReaderFontFamily system;
    system.id="misans";
    system.name="MiSans（系统默认）";
    for(int w:{400,500,700})for(int px=16;px<=40;++px){std::string abs;
    auto st=store_.path("paper/fonts/misans-"+std::to_string(w)+"-"+std::to_string(px)+".pgf",abs,false);
    if(st)system.faces.push_back({px,w,0,{}});
    }
    system.available=!system.faces.empty();
    if(!system.available)system.error="MiSans 字源未生成";
    out.push_back(system);
    std::vector<FileEntry>dirs;
    auto st=store_.list("paper/reader-fonts",dirs,64);
    if(st.code==Error::NotFound)return {};
    if(!st)return st;
    for(auto&d:dirs){if(!d.directory||!validFontId(d.name))continue;
    ReaderFontFamily f;
        auto r=resolve(d.name,"",f);
        if(r.code==Error::NotFound)continue;
        // unpublished import
        if(!r){f.id=d.name;
        f.name=d.name;
        f.error=r.message;
        f.available=false;
        }out.push_back(std::move(f));
        }
    return {};
}
Status validatePackedFont(Store&s,const std::string&relative,int px,int weight,bool allowTest,std::string*digest,
                         std::vector<uint8_t>*validatedIndex,size_t indexBudget,
                         const std::function<Status(size_t,size_t)>&progress){
    if(digest)digest->clear();
    if(validatedIndex)validatedIndex->clear();
    std::vector<uint8_t> checkedIndex;
    Sha256Stream hash;
    LeaseGuard lease;
    auto st=lease.acquire(s.gate(),"sd","font-verify");
    if(!st)return st;
    std::string path;
    st=s.path(relative,path,false);
    if(!st)return st;
    File f;
    f.p=fopen(path.c_str(),"rb");
    if(!f.p)return Status::fail(Error::Io,"字库不可读");
    uint8_t h[80];
    if(!readExact(f.p,h,80)||memcmp(h,"PGF1",4)||get16(h+4)!=1||get16(h+6)!=px||get16(h+8)!=weight||get16(h+10)!=2||get32(h+16)!=80||crc32(h,76)!=get32(h+76))return bad("字库头、字号或字重校验失败");
    if(digest){st=hash.add(h,sizeof h);if(!st)return st;}
    if((get32(h+72)&0x80000000u)&&!allowTest)return Status::fail(Error::ResourceMissing,"测试字库不能用于正式设备");
    uint32_t count=get32(h+12),off=get32(h+20),len=get32(h+64);
    // Full MiSans SC at 36-40px legitimately exceeds 8 MiB. Files are streamed;
    // only the bounded index and glyph cache enter RAM. External packs keep 8MiB.
    const size_t cap=relative.rfind("paper/fonts/misans-",0)==0?16*1024*1024:8*1024*1024;
    if(!count||count>65536||uint64_t(count)*24+80!=off||uint64_t(off)+len>cap)return bad("字库大小超限");
    if(validatedIndex){
        if(size_t(count)*24>indexBudget)return Status::fail(Error::TooLarge,"字库索引超出预算");
        checkedIndex.resize(size_t(count)*24);
    }
    if(progress){st=progress(80,size_t(off)+len);if(!st)return st;}
    int asc=int16_t(get16(h+24)),desc=int16_t(get16(h+26));
    if(asc<0||asc>96||desc>0||desc< -96)return bad("字库垂直度量无效");
    uint32_t crc=0,prev=0,priorEnd=0;
    // Read aligned batches: embedded stdio may otherwise issue one SD request
    // per 24-byte record (tens of thousands for a complete Chinese font).
    uint8_t records[3072];
    for(uint32_t i=0;i<count;++i){
    if(i%128==0){const size_t bytes=std::min<uint32_t>(128,count-i)*24;
        if(!readExact(f.p,records,bytes))return bad("字库索引截断");
        if(validatedIndex)memcpy(checkedIndex.data()+size_t(i)*24,records,bytes);
        if(digest){st=hash.add(records,bytes);if(!st)return st;}
        if(progress){st=progress(80+size_t(i)*24+bytes,size_t(off)+len);if(!st)return st;}
    }
    const uint8_t*r=records+(i%128)*24;
    crc=crc32(r,24,crc);
        uint32_t cp=get32(r),at=get32(r+4),n=get32(r+8),w=get16(r+16),height=get16(r+18),adv=get32(r+12);
        int left=int16_t(get16(r+20)),top=int16_t(get16(r+22));
        if(!cp||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||(i&&cp<=prev)||at!=priorEnd||uint64_t(at)+n>len||w>96||height>96||n!=(w*height+3)/4||adv>96*64||left< -96||left>96||top< -96||top>96)return bad("字库字形描述无效");
        prev=cp;
        priorEnd=at+n;
    }
    if(crc!=get32(h+60)||priorEnd!=len)return bad("字库索引 CRC 或范围不一致");
    // Large sequential transfers let FatFS/SDMMC combine sectors. Keep this
    // on the heap, with a small fallback; the worker stack is only 16 KiB.
    std::unique_ptr<uint8_t[]> bulk(new(std::nothrow)uint8_t[32768]);
    uint8_t fallback[4096];
    uint8_t*buf=bulk?bulk.get():fallback;
    const size_t capacity=bulk?32768:sizeof(fallback);
    crc=0;
    uint32_t remain=len;
    while(remain){size_t n=std::min<size_t>(remain,capacity);
    if(!readExact(f.p,buf,n))return bad("字形数据截断");
    if(digest){st=hash.add(buf,n);if(!st)return st;}
    crc=crc32(buf,n,crc);
    remain-=uint32_t(n);
    if(progress){st=progress(size_t(off)+len-remain,size_t(off)+len);if(!st)return st;}
    }
    if(crc!=get32(h+68)||fgetc(f.p)!=EOF||ferror(f.p))return bad("字形数据 CRC 或文件长度不一致");
    if(digest){st=hash.finish(*digest);if(!st)return st;}
    if(validatedIndex)*validatedIndex=std::move(checkedIndex);
    return {};
}
Status ReaderFontCatalog::install(const std::string&inbox,ReaderFontFamily&receipt){
    receipt={};
    if(inbox.rfind("paper/font-inbox/",0)!=0||inbox.size()<5||inbox.substr(inbox.size()-4)!=".pfr")return Status::fail(Error::Invalid,"只安装字体收件目录中的 .pfr 包");
    LeaseGuard sd,awake;
    auto st=sd.acquire(store_.gate(),"sd","reader-font-install");
    if(!st)return st;
    st=awake.acquire(store_.gate(),"awake","reader-font-install");
    if(!st)return st;
    std::string path;
    st=store_.path(inbox,path,false);
    if(!st)return st;
    struct stat info{};
    if(stat(path.c_str(),&info)||!S_ISREG(info.st_mode)||info.st_size<32||uint64_t(info.st_size)>128ull*1024*1024)return bad("字体包不是有效文件或超出上限");
    File input;
    input.p=fopen(path.c_str(),"rb");
    if(!input.p)return Status::fail(Error::Io,"字体包无法打开");
    uint8_t h[32];
    if(!readExact(input.p,h,32)||memcmp(h,"PFR1",4)||get16(h+4)!=1||get16(h+6)||get32(h+28)||get32(h+24)!=crc32(h,24))return bad("字体包头无效");
    uint32_t mlen=get32(h+8),dlen=get32(h+12);
    if(!mlen||mlen>32768||uint64_t(32)+mlen+dlen!=uint64_t(info.st_size))return bad("字体包清单或长度无效");
    std::string manifest(mlen,'\0');
    if(!readExact(input.p,manifest.data(),mlen)||crc32(manifest.data(),manifest.size())!=get32(h+16))return bad("字体包清单 CRC 不一致");
    ReaderFontFamily family;
    st=decodeFontManifest(manifest,family);
    if(!st)return st;
    uint64_t total=0;
    for(auto&a:family.faces)total+=a.bytes;
    if(total!=dlen)return bad("字体包规格大小之和不一致");
    const size_t progressTotal=size_t(info.st_size)+size_t(dlen)*3;
    size_t progressDone=0;
    auto progress=[&](size_t done){return progress_?progress_(done,progressTotal):Status{};};
    std::string packHash;
    // Hash the inbox through the existing checked handle; yield cancellation and
    // progress without publishing a partially verified family.
    if(fseek(input.p,0,SEEK_SET))return bad("字体包无法定位");
    Sha256Stream packHasher;std::unique_ptr<uint8_t[]> hashBlock(new(std::nothrow)uint8_t[32768]);
    if(!hashBlock)return Status::fail(Error::Unavailable,"字体导入缓冲区不足");
    while(progressDone<size_t(info.st_size)){
        size_t n=std::min(size_t(32768),size_t(info.st_size)-progressDone);
        if(!readExact(input.p,hashBlock.get(),n))return bad("字体包摘要读取失败");
        st=packHasher.add(hashBlock.get(),n);if(!st)return st;
        progressDone+=n;st=progress(progressDone);if(!st)return st;
    }
    st=packHasher.finish(packHash);if(!st)return st;
    if(fseek(input.p,32+mlen,SEEK_SET))return bad("字体包数据无法定位");
    family.revision=packHash;
    uint8_t*buf=hashBlock.get();const size_t copyCapacity=16384; // Transfers::append limit
    uint32_t dataCrc=0;
    Transfers transfer(store_,true);
    for(auto&a:family.faces){
        const auto target=readerFacePath(family.id,packHash,a.px,a.weight);
        std::string present;
        auto exists=store_.path(target,present,false);
        uint64_t upload=0;
        bool writing=!exists;
        if(writing){if(exists.code!=Error::NotFound)return exists;
        st=transfer.begin(target,a.bytes,a.sha256,upload);
        if(!st)return st;
        }
        uint32_t read=0;
        while(read<a.bytes){size_t n=std::min<size_t>(copyCapacity,a.bytes-read);
        if(!readExact(input.p,buf,n)){if(upload)transfer.cancel(upload);
        return bad("字体包数据截断");
        }
            dataCrc=crc32(buf,n,dataCrc);
            if(writing){st=transfer.append(upload,read,buf,n);
            if(!st){transfer.cancel(upload);
            return st;
            }}read+=uint32_t(n);
            progressDone+=n;st=progress(progressDone);
            if(!st){if(upload)transfer.cancel(upload);return st;}
            }
        if(writing){TransferInfo got;
        st=transfer.commit(upload,got,[&](size_t done,size_t){return progress(progressDone+done);});
        if(!st){transfer.cancel(upload);
        return st;
        }}
        progressDone+=a.bytes; // existing immutable files skip the write-commit pass
        std::string actual;
        st=validatePackedFont(store_,target,a.px,a.weight,budget_.allowTestAssets,&actual,nullptr,0,
            [&](size_t done,size_t){return progress(progressDone+done);});
        if(!st)return st;
        if(actual!=a.sha256)return bad("已存在的同版本字体校验失败");
        progressDone+=a.bytes;
        // Runtime uses a bounded face index: do not publish unusably large faces.
        std::string abs;
        store_.path(target,abs,false);
        File check;
        check.p=fopen(abs.c_str(),"rb");
        uint8_t ph[80];
        if(!check.p||!readExact(check.p,ph,80))return bad("无法复读字库头");
        if(uint64_t(get32(ph+12))*24>budget_.fontIndex)return Status::fail(Error::TooLarge,"字体索引超出本机预算，旧字体不变");
    }
    if(dataCrc!=get32(h+20)||fgetc(input.p)!=EOF||ferror(input.p))return bad("字体包数据 CRC 或尾部无效");
    const auto mf="paper/reader-fonts/"+family.id+"/"+packHash+"/family.pfm";
    std::vector<uint8_t>old;
    auto oldRead=store_.read(mf,old,32768);
    if(oldRead){if(std::string(old.begin(),old.end())!=manifest)return bad("同版本清单冲突");
    }
    else {if(oldRead.code!=Error::NotFound)return oldRead;
    st=store_.write(mf,manifest.data(),manifest.size(),false);
    if(!st)return st;
    }
    // Only this atomic, CRC-protected publication makes the new revision visible.
    // A failure above leaves previous font selection and catalog untouched.
    st=store_.saveRecord("reader-font-active-"+family.id,packHash);
    if(!st)return st;
    family.available=true;
    receipt=std::move(family);
    return {};
}
}
