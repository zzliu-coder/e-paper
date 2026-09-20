#include "paper/common.hpp"
#ifdef ESP_PLATFORM
#include "mbedtls/sha256.h"
#include "esp_rom_crc.h"
#endif
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <limits>
#include <new>
#include <sys/stat.h>
namespace paper {
    const char* errorName(Error e) {
        static const char* n[]= {
            "ok","invalid","not_found","unsupported","unavailable","busy","io","corrupt","too_large","conflict","canceled","unauthorized","resource_missing","backend_failure"
        };
        int i=int(e);
        return i>=0&&i<14?n[i]:"unknown";
    }
    std::string jsonString(const std::string&s) {
        std::string o="\"";
        const char*h="0123456789abcdef";
        for(unsigned char c:s) {
            if(c=='"'||c=='\\') {
                o+='\\';
                o+=char(c);
            }
            else if(c<32) {
                o+="\\u00";
                o+=h[c>>4];
                o+=h[c&15];
            }
            else o+=char(c);
        }
        return o+'"';
    }
    uint32_t crc32(const void*p,size_t n,uint32_t initial) {
#ifdef ESP_PLATFORM
        // ESP-IDF 5.5.4 esp_rom_crc.h: zero seed, complemented at both ends;
        // identical to the portable IEEE CRC and supports chained chunks.
        auto* bytes=static_cast<const uint8_t*>(p);
        while(n){uint32_t chunk=uint32_t(std::min<size_t>(n,UINT32_MAX));
            initial=esp_rom_crc32_le(initial,bytes,chunk);bytes+=chunk;n-=chunk;}
        return initial;
#else
        static const auto table=[](){std::array<uint32_t,256> t{};for(unsigned i=0;i<256;++i){uint32_t v=i;for(int k=0;k<8;++k)v=(v>>1)^(0xedb88320u&uint32_t(-int32_t(v&1)));t[i]=v;}return t;}();
        uint32_t c=~initial;
        auto*b=static_cast<const uint8_t*>(p);
        for(size_t i=0;i<n;++i) {
            c=table[(c^b[i])&255]^(c>>8);
        }
        return ~c;
#endif
    }
    std::string hex64(uint64_t x) {
        std::string out(16,'0');
        constexpr char digits[]="0123456789abcdef";
        for(int i=15;i>=0;--i){out[i]=digits[x&15];x>>=4;}
        return out;
    }
    namespace {
        constexpr uint32_t K[]= {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        uint32_t rr(uint32_t x,unsigned n) {
            return (x>>n)|(x<<(32-n));
        }
        class Sha {
            uint32_t h_[8]= {
                0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
            };
            uint8_t b_[64] {
            };
            size_t used_=0;
            uint64_t total_=0;
            void block() {
                uint32_t w[64];
                for(int i=0;i<16;++i)w[i]=(uint32_t(b_[i*4])<<24)|(uint32_t(b_[i*4+1])<<16)|(uint32_t(b_[i*4+2])<<8)|b_[i*4+3];
                for(int i=16;i<64;++i) {
                    uint32_t x=w[i-15],y=w[i-2];
                    w[i]=w[i-16]+(rr(x,7)^rr(x,18)^(x>>3))+w[i-7]+(rr(y,17)^rr(y,19)^(y>>10));
                }
                uint32_t a=h_[0],b=h_[1],c=h_[2],d=h_[3],e=h_[4],f=h_[5],g=h_[6],h=h_[7];
                for(int i=0;i<64;++i) {
                    uint32_t t=h+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+K[i]+w[i];
                    uint32_t u=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));
                    h=g;
                    g=f;
                    f=e;
                    e=d+t;
                    d=c;
                    c=b;
                    b=a;
                    a=t+u;
                }
                h_[0]+=a;
                h_[1]+=b;
                h_[2]+=c;
                h_[3]+=d;
                h_[4]+=e;
                h_[5]+=f;
                h_[6]+=g;
                h_[7]+=h;
            }
            public: void add(const void*p,size_t n) {
                auto*x=(const uint8_t*)p;
                total_+=n;
                while(n) {
                    size_t q=std::min(n,64-used_);
                    memcpy(b_+used_,x,q);
                    x+=q;
                    n-=q;
                    used_+=q;
                    if(used_==64) {
                        block();
                        used_=0;
                    }
                }
            }
            std::string finish() {
                uint64_t bits=total_*8;
                uint8_t one=128,zero=0;
                add(&one,1);
                while(used_!=56)add(&zero,1);
                uint8_t tail[8];
                for(int i=0;i<8;++i)tail[7-i]=uint8_t(bits>>(i*8));
                add(tail,8);
                std::string o;
                char h[9];
                for(uint32_t x:h_) {
                    snprintf(h,sizeof h,"%08" PRIx32,x);
                    o+=h;
                }
                return o;
            }
        };
        bool extender(uint32_t c) {
            return (c>=0x300&&c<=0x36f)||(c>=0x1ab0&&c<=0x1aff)||(c>=0xfe00&&c<=0xfe0f)||(c>=0xe0100&&c<=0xe01ef)||(c>=0x1f3fb&&c<=0x1f3ff)||c==0x200d;
        }
    }
    struct Sha256Stream::Impl {
        bool finished=false;
#ifdef ESP_PLATFORM
        mbedtls_sha256_context ctx;
        int result=0;
        Impl(){mbedtls_sha256_init(&ctx);result=mbedtls_sha256_starts(&ctx,0);}
        ~Impl(){mbedtls_sha256_free(&ctx);}
#else
        Sha sha;
#endif
    };
    Sha256Stream::Sha256Stream():impl_(new(std::nothrow) Impl){}
    Sha256Stream::~Sha256Stream()=default;
    Status Sha256Stream::add(const void*p,size_t n){
        if(!impl_)return Status::fail(Error::Unavailable,"SHA 内存不足");
        if(impl_->finished||(!p&&n))return Status::fail(Error::Invalid,"SHA 输入状态无效");
#ifdef ESP_PLATFORM
        if(impl_->result==0&&n)impl_->result=mbedtls_sha256_update(&impl_->ctx,static_cast<const unsigned char*>(p),n);
        if(impl_->result!=0)return Status::fail(Error::Io,"SHA 计算失败");
#else
        impl_->sha.add(p,n);
#endif
        return {};
    }
    Status Sha256Stream::finish(std::string&out){
        out.clear();
        if(!impl_)return Status::fail(Error::Unavailable,"SHA 内存不足");
        if(impl_->finished)return Status::fail(Error::Invalid,"SHA 已结束");
        impl_->finished=true;
#ifdef ESP_PLATFORM
        unsigned char digest[32];
        if(impl_->result==0)impl_->result=mbedtls_sha256_finish(&impl_->ctx,digest);
        if(impl_->result!=0)return Status::fail(Error::Io,"SHA 计算失败");
        char hex[65];for(size_t i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);out=hex;
#else
        out=impl_->sha.finish();
#endif
        return {};
    }
    std::string sha256(const void*p,size_t n) {
#ifdef ESP_PLATFORM
        // In-memory PGF indexes and proof blocks need the same acceleration as
        // file hashing. Keep the portable path as a correct allocation/driver
        // failure fallback; never return an empty digest that could compare equal.
        Sha256Stream hardware;
        std::string digest;
        if(hardware.add(p,n)&&hardware.finish(digest))return digest;
#endif
        Sha s;
        s.add(p,n);
        return s.finish();
    }
    Status fileSha256(const std::string&p,std::string&out,uint64_t maximum,const std::function<Status(size_t,size_t)>&progress) {
        out.clear();
        FILE*f=fopen(p.c_str(),"rb");
        if(!f)return Status::fail(Error::Io,"文件无法读取");
        struct stat info{};
        if(fstat(fileno(f),&info)||info.st_size<0||uint64_t(info.st_size)>maximum){fclose(f);return Status::fail(Error::TooLarge,"文件超过上限或长度无效");}
#ifdef ESP_PLATFORM
        // ESP-IDF dispatches this to the configured hardware SHA engine, with
        // its own shared-engine arbitration. Preserve the same SHA-256 format.
        mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);
        int result=mbedtls_sha256_starts(&ctx,0);uint64_t total=0;
        uint8_t bytes[4096],digest[32];size_t read=0;
        while(result==0&&(read=fread(bytes,1,sizeof bytes,f))){
            total+=read;if(total>maximum||total>uint64_t(info.st_size)){fclose(f);mbedtls_sha256_free(&ctx);return Status::fail(Error::TooLarge,"文件超过上限或读取期间长度变化");}
            result=mbedtls_sha256_update(&ctx,bytes,read);
            if(progress){auto st=progress(size_t(total),size_t(info.st_size));if(!st){fclose(f);mbedtls_sha256_free(&ctx);return st;}}
        }
        bool ioOk=!ferror(f)&&total==uint64_t(info.st_size);ioOk=fclose(f)==0&&ioOk;
        if(result==0)result=mbedtls_sha256_finish(&ctx,digest);
        mbedtls_sha256_free(&ctx);
        if(!ioOk||result!=0)return Status::fail(Error::Io,"文件读取或 SHA 校验失败");
        char hex[65];for(size_t i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);out=hex;return {};
#else
        Sha s;
        uint8_t b[4096];
        uint64_t total=0;
        size_t n;
        while((n=fread(b,1,sizeof b,f))) {
            total+=n;
            if(total>maximum||total>uint64_t(info.st_size)) {
                fclose(f);
                return Status::fail(Error::TooLarge,"文件超过上限");
            }
            s.add(b,n);
            if(progress){auto st=progress(size_t(total),size_t(info.st_size));if(!st){fclose(f);return st;}}
        }
        bool ok=!ferror(f)&&total==uint64_t(info.st_size);
        ok=fclose(f)==0&&ok;
        if(!ok)return Status::fail(Error::Io,"读取失败");
        out=s.finish();
        return {
        };
#endif
    }
    Status readRune(const std::string&s,size_t&cursor,Rune&r) {
        size_t i=cursor;
        if(i>=s.size())return Status::fail(Error::NotFound,"没有后续字符");
        size_t begin=i;
        uint8_t a=uint8_t(s[i++]);
        uint32_t cp;
        int n;
        if(a<128) {
            cp=a;
            n=0;
        }
        else if(a>=0xc2&&a<=0xdf) {
            cp=a&31;
            n=1;
        }
        else if(a>=0xe0&&a<=0xef) {
            cp=a&15;
            n=2;
        }
        else if(a>=0xf0&&a<=0xf4) {
            cp=a&7;
            n=3;
        }
        else return Status::fail(Error::Invalid,"UTF-8编码无效");
        if(size_t(n)>s.size()-i)return Status::fail(Error::Invalid,"UTF-8字符不完整");
        for(int k=0;k<n;++k) {
            uint8_t b=uint8_t(s[i++]);
            if((b&192)!=128)return Status::fail(Error::Invalid,"UTF-8编码无效");
            cp=(cp<<6)|(b&63);
        }
        if((n==1&&cp<128)||(n==2&&cp<2048)||(n==3&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return Status::fail(Error::Invalid,"UTF-8码点无效");
        r= {
            cp,begin,i
        };
        cursor=i;
        return {
        };
    }
    Status decodeUtf8(const std::string&s,std::vector<Rune>&out,size_t cap) {
        out.clear();
        size_t i=0;
        while(i<s.size()) {
            if(out.size()>=cap)return Status::fail(Error::TooLarge,"文本超过字符上限");
            Rune r;
            auto st=readRune(s,i,r);
            if(!st)return st;
            out.push_back(r);
        }
        return {
        };
    }
    bool validUtf8(const std::string&s) {
        size_t i=0;
        Rune r;
        while(i<s.size())if(!readRune(s,i,r))return false;
        return true;
    }
    std::string encodeUtf8(uint32_t c) {
        std::string o;
        if(c<128)o+=char(c);
        else if(c<2048) {
            o+=char(192|(c>>6));
            o+=char(128|(c&63));
        }
        else if(c<=0xffff&&!(c>=0xd800&&c<=0xdfff)) {
            o+=char(224|(c>>12));
            o+=char(128|((c>>6)&63));
            o+=char(128|(c&63));
        }
        else if(c<=0x10ffff && !(c>=0xd800&&c<=0xdfff)) {
            o+=char(240|(c>>18));
            o+=char(128|((c>>12)&63));
            o+=char(128|((c>>6)&63));
            o+=char(128|(c&63));
        }
        return o;
    }
    size_t previousGrapheme(const std::string&s,size_t pos) {
        std::vector<Rune>r;
        if(!decodeUtf8(s,r)||pos==0)return 0;
        size_t i=r.size();
        while(i&&r[i-1].begin>=pos)--i;
        if(!i)return 0;
        --i;
        while(i&&(extender(r[i].cp)||r[i-1].cp==0x200d))--i;
        return r[i].begin;
    }
    size_t nextGrapheme(const std::string&s,size_t pos) {
        std::vector<Rune>r;
        if(!decodeUtf8(s,r))return pos;
        size_t i=0;
        while(i<r.size()&&r[i].end<=pos)++i;
        if(i==r.size())return s.size();
        ++i;
        while(i<r.size()&&(extender(r[i].cp)||r[i-1].cp==0x200d))++i;
        return i<r.size()?r[i].begin:s.size();
    }
    Status safeRelative(const std::string&s,std::string&out) {
        out.clear();
        if(s.empty()||s.size()>384||s[0]=='/'||s.find('\\')!=s.npos||s.find('\0')!=s.npos||!validUtf8(s))return Status::fail(Error::Invalid,"路径无效");
        size_t b=0;
        for(size_t i=0;i<=s.size();++i)if(i==s.size()||s[i]=='/') {
            auto t=s.substr(b,i-b);
            if(t.empty()||t=="."||t==".."||t.back()=='.'||t.back()==' ')return Status::fail(Error::Invalid,"路径包含无效片段");
            for(unsigned char c:t)if(c<32||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')return Status::fail(Error::Invalid,"文件名包含无效字符");
            b=i+1;
        }
        out=s;
        return {
        };
    }
    void ResourceGate::notifyActivity() {
        bool awake=false;
        for(auto&x:leases_) {
            auto&r=x.second.resource;
            awake|=r=="awake"||r=="mic"||r=="recording"||r=="usb"||r=="update";
        }
        if(awake!=observedAwake_) {
            observedAwake_=awake;
            if(activity_)activity_(awake);
        }
    }
    void ResourceGate::setActivityObserver(std::function<void(bool)>f) {
        std::lock_guard<std::mutex>lock(mutex_);
        activity_=std::move(f);
        if(activity_)activity_(observedAwake_);
    }
    Status ResourceGate::acquire(const std::string&r,const std::string&o,Lease&out) {
        std::lock_guard<std::mutex> l(mutex_);
        if(out)return Status::fail(Error::Conflict,"租约已经存在");
        if(r.empty()||o.empty()||leases_.size()>=64)return Status::fail(Error::Invalid,"资源申请无效");
        bool usb=false;
        for(auto&x:leases_) {
            const auto&q=x.second;
            if((r=="book-mutate"&&q.resource=="sd"&&q.owner=="reader")||(r=="sd"&&o=="reader"&&q.resource=="book-mutate"))return Status::fail(Error::Busy,"请先关闭书籍再更新书库文件");
            usb|=q.resource=="usb";
            if(r=="sd"&&o=="transfer"&&q.resource=="update")return Status::fail(Error::Busy,"升级期间暂停文件传输");
            if(r=="update"&&(q.resource=="update"||(q.resource=="sd"&&q.owner=="transfer")))return Status::fail(Error::Busy,"文件传输或升级尚未结束");
            if((r=="recording"||r=="mic")&&q.resource=="update")return Status::fail(Error::Busy,"升级任务正在使用资源");
            if(r=="mic"&&q.resource=="mic")return Status::fail(Error::Busy,"麦克风正在使用");
            if(r=="usb"&&(q.resource=="sd"||q.resource=="mic"||q.resource=="recording"||q.resource=="update"||q.resource=="usb"))return Status::fail(Error::Busy,"存储或录音正在使用");
            if(r=="update"&&(q.resource=="recording"||q.resource=="mic"||q.resource=="usb"))return Status::fail(Error::Busy,"当前不能更新");
        }
        if(usb&&(r=="sd"||r=="recording"||r=="mic"||r=="update"))return Status::fail(Error::Busy,"电脑正在独占 SD 卡");
        out= {
            next_++,o,r
        };
        leases_[out.id]=out;
        notifyActivity();
        return {
        };
    }
    void ResourceGate::release(Lease&x) {
        std::lock_guard<std::mutex> l(mutex_);
        auto i=leases_.find(x.id);
        if(i!=leases_.end()&&i->second.owner==x.owner&&i->second.resource==x.resource)leases_.erase(i);
        x= {
        };
        notifyActivity();
    }
    bool ResourceGate::held(const std::string&r)const {
        std::lock_guard<std::mutex> l(mutex_);
        for(auto&x:leases_)if(x.second.resource==r)return true;
        return false;
    }
    Status ResourceGate::allowUsb()const {
        for(auto&x:active())if(x.resource=="sd"||x.resource=="mic"||x.resource=="recording"||x.resource=="update"||x.resource=="usb")return Status::fail(Error::Busy,"当前有任务使用存储或麦克风");
        return {
        };
    }
    Status ResourceGate::allowSleep()const {
        for(auto&x:active())if(x.resource=="recording"||x.resource=="mic"||x.resource=="update"||x.resource=="usb"||x.resource=="awake")return Status::fail(Error::Busy,"当前任务需要保持唤醒");
        return {
        };
    }
    std::vector<Lease> ResourceGate::active()const {
        std::lock_guard<std::mutex> l(mutex_);
        std::vector<Lease>o;
        for(auto&x:leases_)o.push_back(x.second);
        return o;
    }
}
