#include "paper/storage.hpp"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
namespace paper {
    namespace {
        Status io(const char*m) {
            return Status::fail(Error::Io,m);
        }
        bool islink(const std::string&p) {
            struct stat s {
            };
#ifdef ESP_PLATFORM
            // VFS FAT has no symbolic links; desktop counterpart rejects them explicitly.
            return stat(p.c_str(),&s)==0&&S_ISLNK(s.st_mode);
#else
            return lstat(p.c_str(),&s)==0&&S_ISLNK(s.st_mode);
#endif
        }
        Status dirs(const std::string&p) {
            size_t b=p[0]=='/'?1:0;
            for(size_t i=b;i<p.size();++i)if(p[i]=='/') {
                auto q=p.substr(0,i);
                if(islink(q))return Status::fail(Error::Invalid,"路径不能经过符号链接");
                if(mkdir(q.c_str(),0755)&&errno!=EEXIST)return io("创建目录失败");
            }
            return {
            };
        }
        void put32(std::vector<uint8_t>&v,uint32_t x) {
            for(int i=0;i<4;++i)v.push_back(uint8_t(x>>(i*8)));
        }
        void put64(std::vector<uint8_t>&v,uint64_t x) {
            for(int i=0;i<8;++i)v.push_back(uint8_t(x>>(i*8)));
        }
        uint32_t get32(const uint8_t*p) {
            return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
        }
        uint64_t get64(const uint8_t*p) {
            return get32(p)|(uint64_t(get32(p+4))<<32);
        }
    }
    Status Store::resolve(const std::string&rel,std::string&abs,bool missing)const {
        std::string safe;
        auto s=safeRelative(rel,safe);
        if(!s)return s;
        abs=root_+"/"+safe;
        size_t b=root_.size()+1;
        for(size_t i=b;i<=abs.size();++i)if(i==abs.size()||abs[i]=='/') {
            auto q=abs.substr(0,i);
            if(islink(q))return Status::fail(Error::Invalid,"禁止符号链接资源");
            struct stat st {
            };
            if(stat(q.c_str(),&st)!=0) {
                if(errno==ENOENT&&missing)return {
                };
                return Status::fail(errno==ENOENT?Error::NotFound:Error::Io,"资源无法访问");
            }
            if(i!=abs.size()&&!S_ISDIR(st.st_mode))return Status::fail(Error::Invalid,"路径经过非目录");
        }
        return {
        };
    }
    Status Store::initialize() {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        invalidateResources();
        if(root_.empty()||root_.find('\0')!=root_.npos||islink(root_))return Status::fail(Error::Invalid,"存储根目录无效");
        struct stat st {
        };
        if(stat(root_.c_str(),&st)!=0||!S_ISDIR(st.st_mode))return Status::fail(Error::Unavailable,"SD 或存储目录不可用");
        LeaseGuard lease;
        auto s=lease.acquire(gate_,"sd","storage-init");
        if(!s)return s;
        for(const char*p:{"books/",".paper/records/",".paper/staging/",".paper/cache/","paper/fonts/","paper/ime/","paper/font-inbox/","paper/reader-fonts/","paper/updates/","exports/"}) {
            s=dirs(root_+"/"+p);
            if(!s)return s;
        }
        return {
        };
    }
    Status Store::read(const std::string&rel,std::vector<uint8_t>&out,size_t cap) {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        out.clear();
        LeaseGuard lease;
        auto s=lease.acquire(gate_,"sd","read");
        if(!s)return s;
        std::string p;
        s=resolve(rel,p,false);
        if(!s)return s;
        struct stat st {
        };
        if(stat(p.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_size<0)return io("不是有效文件");
        if(uint64_t(st.st_size)>cap)return Status::fail(Error::TooLarge,"文件超过读取预算");
        FILE*f=fopen(p.c_str(),"rb");
        if(!f)return io("打开文件失败");
        out.resize(size_t(st.st_size));
        bool ok=fread(out.data(),1,out.size(),f)==out.size()&&!ferror(f);
        ok=fclose(f)==0&&ok;
        if(!ok) {
            out.clear();
            return io("文件读取不完整");
        }
        return {
        };
    }
    Status Store::write(const std::string&rel,const void*bytes,size_t n,bool replace) {
        return publish(rel,bytes,n,replace,false);
    }
    Status Store::publish(const std::string&rel,const void*bytes,size_t n,bool replace,bool alternateRecord) {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        LeaseGuard mutation;
        if(rel=="books"||rel.rfind("books/",0)==0){auto status=mutation.acquire(gate_,"book-mutate","write");if(!status)return status;}
        if(n>16*1024*1024||(!bytes&&n))return Status::fail(Error::TooLarge,"写入超过上限");
        LeaseGuard lease;
        auto s=lease.acquire(gate_,"sd","write");
        if(!s)return s;
        std::string p;
        s=resolve(rel,p,true);
        if(!s)return s;
        struct stat st {
        };
        if(!replace&&stat(p.c_str(),&st)==0)return Status::fail(Error::Conflict,"目标已经存在");
        s=dirs(p);
        if(!s)return s;
        const auto tmp=p+".paper-tmp";
        if(islink(tmp))return Status::fail(Error::Invalid,"临时路径无效");
        FILE*f=fopen(tmp.c_str(),"wb");
        if(!f)return io("无法建立临时文件");
        bool ok=fwrite(bytes,1,n,f)==n;
        ok=fflush(f)==0&&ok;
        ok=fsync(fileno(f))==0&&ok;
        ok=fclose(f)==0&&ok;
        if(!ok) {
            ::remove(tmp.c_str());
            return io("写入或同步失败");
        }
        if(!replace&&stat(p.c_str(),&st)==0) {
            ::remove(tmp.c_str());
            return Status::fail(Error::Conflict,"提交时目标已存在");
        }
        // FAT rename cannot replace an existing destination. A validated
        // alternating record keeps its opposite slot intact; derived reader
        // caches may also retire a stale copy because the book remains intact.
        if(alternateRecord&&stat(p.c_str(),&st)==0&&(!S_ISREG(st.st_mode)||::remove(p.c_str())!=0)){::remove(tmp.c_str());return io("无法轮换备用记录");}
        if(::rename(tmp.c_str(),p.c_str())) {
            ::remove(tmp.c_str());
            return io("提交文件失败");
        }
        if(rel.rfind("paper/fonts/",0)==0||rel.rfind("paper/reader-fonts/",0)==0||rel.rfind("books/",0)==0)invalidateResources();
        return {
        };
    }
    Status Store::writeReaderCache(const std::string&rel,const void*data,size_t size) {
        if(rel.rfind(".paper/cache/crossmux-1/",0)!=0)return Status::fail(Error::Unauthorized,"仅允许发布阅读派生缓存");
        return publish(rel,data,size,true,true);
    }
    Status Store::list(const std::string&rel,std::vector<FileEntry>&o,size_t cap) {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        o.clear();
        LeaseGuard lease;
        auto s=lease.acquire(gate_,"sd","list");
        if(!s)return s;
        std::string p;
        s=resolve(rel,p,false);
        if(!s)return s;
        DIR*d=opendir(p.c_str());
        if(!d)return io("打开目录失败");
        while(auto*e=readdir(d)) {
            std::string n=e->d_name;
            if(n.empty()||n[0]=='.')continue;
            std::string r=rel+"/"+n,full;
            auto q=resolve(r,full,false);
            if(!q)continue;
            struct stat st {
            };
            if(stat(full.c_str(),&st)||(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)))continue;
            if(o.size()>=cap) {
                closedir(d);
                return Status::fail(Error::TooLarge,"目录条目超过预算，请使用子目录");
            }
            o.push_back({n,r,uint64_t(st.st_size),bool(S_ISDIR(st.st_mode))});
        }
        closedir(d);
        std::sort(o.begin(),o.end(),[](auto&a,auto&b){if(a.directory!=b.directory)return a.directory>b.directory;return a.name<b.name;});
        return {
        };
    }
    Status Store::renameFile(const std::string&a,const std::string&b) {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        LeaseGuard mutation;
        if(a=="books"||b=="books"||a.rfind("books/",0)==0||b.rfind("books/",0)==0){auto status=mutation.acquire(gate_,"book-mutate","rename");if(!status)return status;}
        if(a==b)return {
        };
        LeaseGuard lease;
        auto s=lease.acquire(gate_,"sd","rename");
        if(!s)return s;
        std::string p,q;
        s=resolve(a,p,false);
        if(!s)return s;
        s=resolve(b,q,true);
        if(!s)return s;
        struct stat st {
        };
        if(stat(q.c_str(),&st)==0)return Status::fail(Error::Conflict,"目标文件已存在");
        s=dirs(q);
        if(!s)return s;
        if(::rename(p.c_str(),q.c_str()))return io("改名失败");
        if(a.rfind("books/",0)==0||b.rfind("books/",0)==0||a.rfind("paper/fonts/",0)==0||b.rfind("paper/fonts/",0)==0||a.rfind("paper/reader-fonts/",0)==0||b.rfind("paper/reader-fonts/",0)==0)invalidateResources();
        return {};
    }
    Status Store::removeTemporary(const std::string&rel) {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        if(rel.rfind(".paper/staging/",0)!=0)return Status::fail(Error::Unauthorized,"只能清理传输临时文件");
        LeaseGuard lease;
        auto s=lease.acquire(gate_,"sd","cancel");
        if(!s)return s;
        std::string p;
        s=resolve(rel,p,false);
        if(s.code==Error::NotFound)return {
        };
        if(!s)return s;
        return ::remove(p.c_str())==0?Status {
        }
        :io("临时文件清理失败");
    }
    Status Store::hash(const std::string&rel,std::string&h,const std::function<Status(size_t,size_t)>&progress) {
        LeaseGuard lease;
        auto s=lease.acquire(gate_,"sd","fingerprint");
        if(!s)return s;
        std::string p;
        s=resolve(rel,p,false);
        if(!s)return s;
        return fileSha256(p,h,512ull*1024*1024,progress);
    }
    Status Store::loadRecord(const std::string&key,std::string&payload,uint64_t*generation) {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        std::string clean;
        auto s=safeRelative(key,clean);
        if(!s)return s;
        bool found=false;
        uint64_t best=0;
        Status last=Status::fail(Error::NotFound,"尚无保存记录");
        for(auto suffix:{".a",".b"}) {
            std::vector<uint8_t>v;
            auto x=read(".paper/records/"+key+suffix,v,256*1024);
            if(!x) {
                if(x.code!=Error::NotFound)last=x;
                continue;
            }
            if(v.size()<24||memcmp(v.data(),"PPR1",4)||get32(v.data()+4)!=1||get32(v.data()+16)!=v.size()-24||crc32(v.data()+24,v.size()-24)!=get32(v.data()+20)) {
                last=Status::fail(Error::Corrupt,"记录校验失败");
                continue;
            }
            uint64_t g=get64(v.data()+8);
            if(!g||bool(g&1)!=(suffix[1]=='a')){last=Status::fail(Error::Corrupt,"记录代次与槽位不符");continue;}
            if(!found||g>best) {
                found=true;
                best=g;
                payload.assign((char*)v.data()+24,v.size()-24);
            }
        }
        if(!found)return last;
        if(generation)*generation=best;
        return {
        };
    }
    Status Store::saveRecord(const std::string&key,const std::string&p) {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        if(p.size()>250*1024)return Status::fail(Error::TooLarge,"记录过大");
        std::string old;
        uint64_t g=0;
        auto s=loadRecord(key,old,&g);
        if(!s&&s.code!=Error::NotFound)return s;
        if(s&&old==p)return {
        };
        if(g==UINT64_MAX)return Status::fail(Error::TooLarge,"记录版本溢出");
        ++g;
        std::vector<uint8_t>v= {
            'P','P','R','1'
        };
        put32(v,1);
        put64(v,g);
        put32(v,uint32_t(p.size()));
        put32(v,crc32(p.data(),p.size()));
        v.insert(v.end(),p.begin(),p.end());
        return publish(".paper/records/"+key+(g%2?".a":".b"),v.data(),v.size(),true,g>1);
    }
    void Transfers::release(Upload&u) {
        if(u.file) {
            fclose(u.file);
            u.file=nullptr;
        }
        gate_.release(u.sd);
        gate_.release(u.awake);
    }
    Transfers::~Transfers() {
        cancelAll();
    }
    Status Transfers::begin(const std::string&dest,uint64_t length,const std::string&sha,uint64_t&id) {
        std::lock_guard<std::mutex>l(mutex_);
        if(uploads_.size()>=2)return Status::fail(Error::Busy,"已有传输任务");
        if(!length||length>128ull*1024*1024||sha.size()!=64||sha.find_first_not_of("0123456789abcdef")!=sha.npos)return Status::fail(Error::Invalid,"文件大小或SHA-256无效");
        if(dest.rfind("books/",0)!=0&&dest.rfind("paper/fonts/",0)!=0&&dest.rfind("paper/ime/",0)!=0&&dest.rfind("paper/updates/",0)!=0&&dest.rfind("exports/",0)!=0&&dest.rfind("paper/font-inbox/",0)!=0&&!(publishReaderFonts_&&dest.rfind("paper/reader-fonts/",0)==0))return Status::fail(Error::Unauthorized,"不允许写入该目录");
        std::string full;
        auto s=store_.path(dest,full,true);
        if(!s)return s;
        struct stat st {
        };
        if(stat(full.c_str(),&st)==0)return Status::fail(Error::Conflict,"目标已存在，禁止覆盖");
        Upload u;
        s=gate_.acquire("sd","transfer",u.sd);
        if(!s)return s;
        s=gate_.acquire("awake","transfer",u.awake);
        if(!s) {
            release(u);
            return s;
        }
        static std::atomic<uint64_t> globalUpload{1};
        id=globalUpload.fetch_add(1);
        u.info= {
            id,0,length,dest,"receiving",sha
        };
        u.temp=".paper/staging/upload-"+hex64(id)+".part";
        s=store_.path(u.temp,full,true);
        if(!s) {
            release(u);
            return s;
        }
        if(stat(full.c_str(),&st)==0) {
            release(u);
            return Status::fail(Error::Conflict,"发现旧未完成传输，保留待恢复");
        }
        u.file=fopen(full.c_str(),"wb");
        if(!u.file) {
            release(u);
            return io("无法建立传输文件");
        }
        uploads_.emplace(id,std::move(u));
        return {
        };
    }
    Status Transfers::append(uint64_t id,uint64_t offset,const void*p,size_t n) {
        std::lock_guard<std::mutex>l(mutex_);
        auto it=uploads_.find(id);
        if(it==uploads_.end())return Status::fail(Error::NotFound,"传输不存在");
        auto&u=it->second;
        if(u.info.phase!="receiving")return Status::fail(Error::Conflict,"传输不再接收数据");
        if(offset!=u.info.received)return Status::fail(Error::Conflict,"数据偏移不一致");
        if(!p||!n||n>16384||n>u.info.expected-u.info.received)return Status::fail(Error::Invalid,"数据块大小无效");
        if(fwrite(p,1,n,u.file)!=n) {
            u.info.phase="failed";
            return io("存储写入失败");
        }
        u.info.received+=n;
        return {
        };
    }
    Status Transfers::commit(uint64_t id,TransferInfo&receipt,const std::function<Status(size_t,size_t)>&progress) {
        std::lock_guard<std::mutex>l(mutex_);
        auto it=uploads_.find(id);
        if(it==uploads_.end())return Status::fail(Error::NotFound,"传输不存在");
        auto&u=it->second;
        if(u.info.phase!="receiving"||u.info.received!=u.info.expected)return Status::fail(Error::Conflict,"传输尚未完整");
        bool ok=fflush(u.file)==0;
        ok=fsync(fileno(u.file))==0&&ok;
        ok=fclose(u.file)==0&&ok;
        u.file=nullptr;
        if(!ok) {
            u.info.phase="failed";
            return io("文件同步失败");
        }
        std::string actual;
        auto s=store_.hash(u.temp,actual,progress);
        if(!s||actual!=u.info.sha) {
            u.info.phase="failed";
            return !s?s:Status::fail(Error::Corrupt,"SHA-256不匹配，未提交");
        }
        s=store_.renameFile(u.temp,u.info.path);
        if(!s) {
            u.info.phase="failed";
            return s;
        }
        u.info.phase="committed";
        receipt=u.info;
        release(u);
        uploads_.erase(it);
        return {
        };
    }
    Status Transfers::cancel(uint64_t id) {
        std::lock_guard<std::mutex>l(mutex_);
        auto it=uploads_.find(id);
        if(it==uploads_.end())return {
        };
        auto temp=it->second.temp;
        release(it->second);
        uploads_.erase(it);
        return store_.removeTemporary(temp);
    }
    void Transfers::cancelAll() {
        std::vector<uint64_t>ids;
        {
            std::lock_guard<std::mutex>l(mutex_);
            for(auto&x:uploads_)ids.push_back(x.first);
        }
        for(auto id:ids)cancel(id);
    }
    Status Transfers::status(uint64_t id,TransferInfo&o)const {
        std::lock_guard<std::mutex>l(mutex_);
        auto it=uploads_.find(id);
        if(it==uploads_.end())return Status::fail(Error::NotFound,"传输不存在");
        o=it->second.info;
        return {
        };
    }
}
