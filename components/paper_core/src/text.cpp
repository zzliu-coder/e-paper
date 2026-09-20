#include "paper/text.hpp"
#include "paper/font_catalog.hpp"
#include "paper/font_proof.hpp"
#include <set>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <chrono>
namespace paper {
    void PackedFonts::clearGlyphCache(){
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        cache_.clear();cacheLookup_.clear();bytes_=0;
    }
    namespace {
        struct Timing {
            uint64_t& sum;
            std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
            explicit Timing(uint64_t& value):sum(value){}
            ~Timing(){sum+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();}
        };
        uint16_t u16(const uint8_t*p) {
            return p[0]|uint16_t(p[1])<<8;
        }
        uint32_t u32(const uint8_t*p) {
            return u16(p)|uint32_t(u16(p+2))<<16;
        }
        int16_t s16(const uint8_t*p) {
            return int16_t(u16(p));
        }
        bool cjk(uint32_t c) {
            return c>=0x2e80&&c<=0x9fff;
        }
        bool opening(uint32_t c) {
            return c=='('||c=='['||c=='{'||c==0xff08||c==0x300a||c==0x201c||c==0x2018||c==0x3010||c==0x300c||c==0x300e;
        }
        bool closing(uint32_t c) {
            return c=='.'||c==','||c==';'||c==':'||c=='!'||c=='?'||c==')'||c==']'||c=='}'||c==0xff0c||c==0x3002||c==0x3001||c==0xff1b||c==0xff1a||c==0xff01||c==0xff1f||c==0xff09||c==0x300b||c==0x201d||c==0x2019||c==0x3011||c==0x300d||c==0x300f;
        }
        bool space(uint32_t c) {
            return c==' '||c=='\t';
        }

    }
    Status PackedFonts::face(const FontSpec&s,Face*&f) {
        if(s.family!="misans" && !validFontRevision(s.revision))return Status::fail(Error::Invalid,"外置阅读字体需要固定版本");
        if(s.px<16||s.px>40||(s.weight!=400&&s.weight!=500&&s.weight!=700))return Status::fail(Error::Invalid,"字体规格无效");
        std::lock_guard<std::recursive_mutex> l(mutex_);
        if(generation_!=store_.resourceGeneration()) {
            clear();generation_=store_.resourceGeneration();
        }
        for(auto&x:faces_)if(x.px==s.px&&x.weight==s.weight&&x.family==s.family&&x.revision==s.revision&&x.uiOnly==s.uiOnly) {
            ++indexHits_;
            x.used=++tick_;
            f=&x;
            return {
            };
        }
        std::string relative=std::string("paper/fonts/misans-")+(s.uiOnly?"ui-":"")+std::to_string(s.weight)+"-"+std::to_string(s.px)+".pgf",expected;
        Status st;
        if(s.family!="misans") {
            st=resolveReaderFace(store_,s,relative,expected);
            if(!st)return st;
        } else if(!s.revision.empty()) return Status::fail(Error::Invalid,"系统字体不能指定外置版本");
        std::string path;
        st=store_.path(relative,path,false);
        if(!st)return Status::fail(Error::ResourceMissing,"字体规格尚未生成或文件已移除");
        std::string fileDigest;
        std::vector<uint8_t> checkedIndex;
        std::unique_ptr<FontProof> proof;
        if(!forceFullVerification_&&!s.uiOnly&&s.family=="misans"&&singlePassVerification_){
            std::unique_ptr<FontProof> candidate(new(std::nothrow)FontProof);
            if(!candidate)return Status::fail(Error::Unavailable,"字体校验工作区不足");
            {Timing measure(validationUs_);st=candidate->open(store_,relative,s.px,s.weight,budget_.fontIndex,checkedIndex);}
            if(st){proof=std::move(candidate);fileDigest=proof->identity;++validations_;}
            else if(st.code!=Error::NotFound)return st;
        }
        auto verified=verified_.find(relative);
        if(proof){} // Partial verification never enters the full-file verified cache.
        else if(verified!=verified_.end())fileDigest=verified->second;
        else {
            // One sequential read computes SHA and validates the complete PGF.
            // validation_us includes hashing in this path; hash_us stays separate
            // for the legacy comparison path only.
            if(!singlePassVerification_){Timing measure(hashUs_);st=store_.hash(relative,fileDigest);if(!st)return st;}
            {Timing measure(validationUs_);st=validatePackedFont(store_,relative,s.px,s.weight,budget_.allowTestAssets,singlePassVerification_?&fileDigest:nullptr,
                singlePassVerification_?&checkedIndex:nullptr,s.uiOnly?budget_.uiFontIndex:budget_.fontIndex,progress_);}
            if(!st)return st;
            ++validations_;
            if(verified_.size()>=128)verified_.erase(verified_.begin());
            verified_[relative]=fileDigest;
        }
        if(!expected.empty()&&fileDigest!=expected)return Status::fail(Error::Corrupt,"字体文件与已安装清单不一致");
        Timing indexMeasure(indexUs_);
        FILE*fp=fopen(path.c_str(),"rb");
        if(!fp)return Status::fail(Error::Io,"无法打开字库");
        uint8_t h[80];
        bool ok=fread(h,1,80,fp)==80;
        if(!ok||memcmp(h,"PGF1",4)||u16(h+4)!=1||u16(h+6)!=s.px||u16(h+8)!=s.weight||u16(h+10)!=2||u32(h+16)!=80||u32(h+76)!=crc32(h,76)) {
            fclose(fp);
            return Status::fail(Error::Corrupt,"字库头或规格无效");
        }
        if((u32(h+72)&0x80000000u)&&!budget_.allowTestAssets) {
            fclose(fp);
            return Status::fail(Error::ResourceMissing,"测试替身字库不能部署到正式固件");
        }
        uint32_t count=u32(h+12),off=u32(h+20),len=u32(h+64);
        const auto indexBudget=s.uiOnly?budget_.uiFontIndex:budget_.fontIndex;
        if(!count||count>65536||uint64_t(count)*24>indexBudget||off!=80+count*24||len>32*1024*1024) {
            fclose(fp);
            return Status::fail(Error::TooLarge,"字库索引超出预算");
        }
        Face n;
        n.proof=std::move(proof);
        n.uiOnly=s.uiOnly;
        n.px=s.px;
        n.weight=s.weight;
        n.path=path;
        n.identity=fileDigest;
        n.family=s.family;
        n.revision=s.revision;
        n.dataOffset=off;
        n.dataLength=len;
        n.used=++tick_;
        if(!checkedIndex.empty()){
            n.index=std::move(checkedIndex);
            ok=n.index.size()==size_t(count)*24;
        }else{
            n.index.resize(size_t(count)*24);
            ok=fread(n.index.data(),1,n.index.size(),fp)==n.index.size();
        }
        if(fseek(fp,0,SEEK_END)!=0)ok=false;
        long end=ftell(fp);
        fclose(fp);
        if(!ok||end<0||uint64_t(end)!=uint64_t(off)+len||crc32(n.index.data(),n.index.size())!=u32(h+60))return Status::fail(Error::Corrupt,"字库长度或索引校验失败");
        uint32_t prev=0;
        for(uint32_t i=0;i<count;++i) {
            auto*r=n.index.data()+i*24;
            uint32_t cp=u32(r),at=u32(r+4),bytes=u32(r+8);
            uint32_t w=u16(r+16),height=u16(r+18);
            if((i&&cp<=prev)||cp>0x10ffff||cp==0||(cp>=0xd800&&cp<=0xdfff)||w>96||height>96||uint64_t(at)+bytes>len||bytes!=(w*height+3)/4||int32_t(u32(r+12))<0||u32(r+12)>96*64)return Status::fail(Error::Corrupt,"字库字形描述无效");
            prev=cp;
        }
        auto used=[this,&s]() {
            size_t n=0;
            for(auto&x:faces_)if(x.uiOnly==s.uiOnly)n+=x.index.size()+(x.proof?x.proof->reservedBytes():0);
            return n;
        };
        while(used()+n.index.size()+(n.proof?n.proof->reservedBytes():0)>indexBudget) {
            auto it=faces_.end();
            for(auto p=faces_.begin();p!=faces_.end();++p)
                if(p->uiOnly==s.uiOnly&&(it==faces_.end()||p->used<it->used))it=p;
            if(it==faces_.end())return Status::fail(Error::TooLarge,"字体索引预算不足");
            faces_.erase(it);
        }
        ++indexLoads_;
        faces_.push_back(std::move(n));
        f=&faces_.back();
        return {
        };
    }
    Status PackedFonts::revalidate(const FontSpec&s) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        closeBatchFile();
        std::string path=std::string("paper/fonts/misans-")+(s.uiOnly?"ui-":"")+std::to_string(s.weight)+"-"+std::to_string(s.px)+".pgf",expected;
        if(s.family!="misans"){auto st=resolveReaderFace(store_,s,path,expected);if(!st)return st;}
        verified_.erase(path);
        faces_.erase(std::remove_if(faces_.begin(),faces_.end(),[&](const Face&f){return f.family==s.family&&f.revision==s.revision&&f.px==s.px&&f.weight==s.weight&&f.uiOnly==s.uiOnly;}),faces_.end());
        forceFullVerification_=true;
        auto result=validate(s);forceFullVerification_=false;return result;
    }
    Status PackedFonts::record(uint32_t cp,const Face&f,const uint8_t*&r) {
        size_t lo=0,hi=f.index.size()/24;
        while(lo<hi) {
            size_t mid=(lo+hi)/2;
            if(u32(f.index.data()+mid*24)<cp)lo=mid+1;
            else hi=mid;
        }
        if(lo==f.index.size()/24||u32(f.index.data()+lo*24)!=cp)return Status::fail(Error::NotFound,"当前字体缺少此字形");
        r=f.index.data()+lo*24;
        return {
        };
    }
    Status PackedFonts::exactAdvance(uint32_t cp,const FontSpec&s,int&v) {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        LeaseGuard lease;
        auto st=lease.acquire(store_.gate(),"sd","font-metrics");
        if(!st)return st;
        Face*f=nullptr;
        st=face(s,f);
        if(!st)return st;
        const uint8_t*r;
        st=record(cp,*f,r);
        if(!st)return st;
        v=int32_t(u32(r+12));
        return {
        };
    }
    Status PackedFonts::exactGlyph(uint32_t cp,const FontSpec&s,Glyph&g) {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        LeaseGuard lease;
        auto st=lease.acquire(store_.gate(),"sd","font-glyph");
        if(!st)return st;
        Face*f=nullptr;
        st=face(s,f);
        if(!st)return st;
        std::string key=s.family+":"+s.revision+":"+f->identity+":"+std::to_string(s.weight)+":"+std::to_string(s.px)+":"+std::to_string(cp);
        auto found=cacheLookup_.find(key);
        if(found!=cacheLookup_.end()) {
            auto it=found->second;
            ++glyphHits_;
            g=it->glyph;
            cache_.splice(cache_.begin(),cache_,it);
            return {
            };
        }
        const uint8_t*r;
        st=record(cp,*f,r);
        if(!st)return st;
        uint32_t off=u32(r+4),n=u32(r+8);
        g= {
            int32_t(u32(r+12)),int(u16(r+16)),int(u16(r+18)),int(s16(r+20)),int(s16(r+22)), {
            }
        };
        g.coverage2.resize(n);
        Timing glyphMeasure(glyphIoUs_);
        FILE*fp=nullptr;
        const bool batched=batchEnabled_&&batchDepth_>0;
        if(batched){
            if(batchPath_!=f->path)closeBatchFile();
            if(!batchFile_){batchFile_=fopen(f->path.c_str(),"rb");batchPath_=f->path;++glyphOpens_;}
            fp=batchFile_;
        }else{fp=fopen(f->path.c_str(),"rb");++glyphOpens_;}
        if(!fp)return Status::fail(Error::Io,"字库读取失败");
        Status blockStatus;
        bool ok;
        if(f->proof){blockStatus=f->proof->read(fp,off,n,g.coverage2);ok=bool(blockStatus);}
        else ok=fseek(fp,f->dataOffset+off,SEEK_SET)==0&&fread(g.coverage2.data(),1,n,fp)==n;
        if(!batched)ok=fclose(fp)==0&&ok;
        else if(!ok)closeBatchFile();
        if(!blockStatus)return blockStatus;
        if(!ok)return Status::fail(Error::Io,"字形读取不完整");
        ++glyphReads_;
        constexpr size_t lookupOverhead=sizeof(std::string)+sizeof(void*)*4;
        size_t cost=n+2*key.size()+sizeof(Cache)+lookupOverhead;
        if(cost<=budget_.glyphCache) {
            while(!cache_.empty()&&bytes_+cost>budget_.glyphCache) {
                auto&x=cache_.back();
                bytes_-=x.glyph.coverage2.size()+2*x.key.size()+sizeof(Cache)+lookupOverhead;
                cacheLookup_.erase(x.key);
                cache_.pop_back();
            }
            cache_.push_front({key,g});
            cacheLookup_[key]=cache_.begin();
            bytes_+=cost;
        }
        return {
        };
    }
    std::string PackedFonts::identity(const FontSpec&s)const {
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        for(auto&x:faces_)if(x.px==s.px&&x.weight==s.weight&&x.family==s.family&&x.revision==s.revision&&x.uiOnly==s.uiOnly)return x.identity;
        return "unloaded";
    }
    void PackedFonts::clear() {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        releaseMemory();
        verified_.clear();
    }
    void PackedFonts::releaseMemory() {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        closeBatchFile();
        faces_.clear();
        cacheLookup_.clear();
        cache_.clear();
        bytes_=0;
    }
    void PackedFonts::closeBatchFile(){
        if(batchFile_)fclose(batchFile_);
        batchFile_=nullptr;batchPath_.clear();
    }
    Status PackedFonts::beginReadBatch(){
        mutex_.lock();
        if(batchDepth_==0){
            auto st=store_.gate().acquire("sd","font-draw",batchLease_);
            if(!st){mutex_.unlock();return st;}
        }
        ++batchDepth_;return {};
    }
    void PackedFonts::endReadBatch(){
        if(--batchDepth_==0){closeBatchFile();store_.gate().release(batchLease_);}
        mutex_.unlock();
    }
    Status PackedFonts::setBatchEnabled(bool enabled){
        std::lock_guard<std::recursive_mutex> l(mutex_);
        if(batchDepth_)return Status::fail(Error::Busy,"字体绘制正在进行");
        clear();batchEnabled_=enabled;return {};
    }
    Status PackedFonts::setSinglePassVerification(bool enabled){
        std::lock_guard<std::recursive_mutex> l(mutex_);
        if(batchDepth_)return Status::fail(Error::Busy,"字体绘制正在进行");
        clear();singlePassVerification_=enabled;return {};
    }
    std::string PackedFonts::statistics()const {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        size_t ui=0,reader=0,proofs=0,proofBytes=0;
        for(auto&f:faces_)(f.uiOnly?ui:reader)+=f.index.size();
        for(auto&f:faces_)if(f.proof){++proofs;proofBytes+=f.proof->reservedBytes();}
        return "{\"proof_faces\":"+std::to_string(proofs)+",\"proof_reserved_bytes\":"+std::to_string(proofBytes)+",\"batch_io\":"+std::string(batchEnabled_?"true":"false")+",\"hash_us\":"+std::to_string(hashUs_)+",\"validation_us\":"+std::to_string(validationUs_)+",\"index_us\":"+std::to_string(indexUs_)+",\"glyph_io_us\":"+std::to_string(glyphIoUs_)+",\"glyph_opens\":"+std::to_string(glyphOpens_)+",\"generation\":"+std::to_string(generation_)+",\"validations\":"+std::to_string(validations_)+",\"index_loads\":"+std::to_string(indexLoads_)+",\"index_hits\":"+std::to_string(indexHits_)+",\"glyph_hits\":"+std::to_string(glyphHits_)+",\"glyph_reads\":"+std::to_string(glyphReads_)+",\"ui_index_bytes\":"+std::to_string(ui)+",\"reader_index_bytes\":"+std::to_string(reader)+"}";
    }
    Status PackedFonts::validate(const FontSpec&s) {
        std::lock_guard<std::recursive_mutex>l(mutex_);
        LeaseGuard hold;auto st=hold.acquire(store_.gate(),"sd","font-validation");if(!st)return st;
        Face*f=nullptr;return face(s,f);
    }
    Status PackedFonts::advance(uint32_t cp,const FontSpec&s,int&v) {
        auto st=exactAdvance(cp,s,v);
        if(!st&&s.uiOnly&&(st.code==Error::NotFound||st.code==Error::ResourceMissing)){auto full=s;full.uiOnly=false;return exactAdvance(cp,full,v);}
        if(st.code==Error::NotFound&&s.allowFallback&&s.family!="misans") {
            FontSpec fallback{s.px,s.weight,s.gray};return exactAdvance(cp,fallback,v);
        }
        return st;
    }
    Status PackedFonts::glyph(uint32_t cp,const FontSpec&s,Glyph&g) {
        auto st=exactGlyph(cp,s,g);
        if(!st&&s.uiOnly&&(st.code==Error::NotFound||st.code==Error::ResourceMissing)){auto full=s;full.uiOnly=false;return exactGlyph(cp,full,g);}
        if(st.code==Error::NotFound&&s.allowFallback&&s.family!="misans") {
            FontSpec fallback{s.px,s.weight,s.gray};return exactGlyph(cp,fallback,g);
        }
        return st;
    }
    Status PackedFonts::coverage(const FontSpec&s,const std::string&text,std::vector<uint32_t>&missing) {
        missing.clear();
        auto st=validate(s);
        if(!st)return st;
        std::map<uint32_t,size_t> frequencies;
        size_t cursor=0,total=0;
        Rune rune;
        while(cursor<text.size()) {
            st=readRune(text,cursor,rune);
            if(!st)return st;
            if(rune.cp=='\n'||rune.cp=='\r')continue;
            ++frequencies[rune.cp=='\t'?' ':rune.cp];
            ++total;
        }
        FontSpec exact=s;
        exact.allowFallback=false;
        size_t missingOccurrences=0;
        for(const auto&entry:frequencies) {
            int width=0;
            st=exactAdvance(entry.first,exact,width);
            if(st.code==Error::NotFound) {
                missing.push_back(entry.first);
                missingOccurrences+=entry.second;
            } else if(!st) return st;
        }
        if(missing.empty())return {};
        // Both unique characters and occurrence frequency are bounded: missing
        // a frequently used character cannot quietly make most of a page MiSans.
        if(!s.allowFallback||s.family=="misans"||missing.size()>8||
           missing.size()*20>frequencies.size()||missingOccurrences*20>total)
            return Status::fail(Error::ResourceMissing,"此章节缺字过多或未启用少量补字，保留原字体");
        FontSpec fallback{s.px,s.weight,s.gray};
        for(auto cp:missing) {
            int width=0;
            st=exactAdvance(cp,fallback,width);
            if(!st)return Status::fail(Error::ResourceMissing,"MiSans 也缺少所需补字或对应规格");
        }
        return {};
    }
    Status layoutText(FontProvider&fonts,const FontSpec&spec,const std::string&s,size_t offset,int width,int maxLines,TextLayout&out) {
        out= {
        };
        out.next=offset;
        if(width<spec.px||width>800||maxLines<1||maxLines>100||offset>s.size())return Status::fail(Error::Invalid,"排版范围无效");
        if(offset<s.size()&&(uint8_t(s[offset])&192)==128)return Status::fail(Error::Invalid,"位置落在UTF-8字符中间");
        size_t pos=offset;
        while(pos<s.size()&&out.lines.size()<size_t(maxLines)) {
            size_t begin=pos,end=pos,next=pos,lastBreak=pos,scan=pos,count=0,lastBegin=pos;
            int pen=0,lastWidth=0,lastAdvance=0;
            uint32_t lastCp=0;
            while(scan<s.size()) {
                if(++count>2048)return Status::fail(Error::TooLarge,"单行零宽字符过多");
                Rune r;
                auto st=readRune(s,scan,r);
                if(!st)return st;
                if(r.cp=='\r'||r.cp=='\n') {
                    end=r.begin;
                    next=r.end;
                    if(r.cp=='\r'&&next<s.size()&&s[next]=='\n')++next;
                    break;
                }
                int advance=0;
                st=fonts.advance(r.cp=='\t'?' ':r.cp,spec,advance);
                if(!st)return st;
                if(pen+advance>width*64&&end>begin) {
                    if(lastBreak>begin) {
                        end=lastBreak;
                        next=end;
                        pen=lastWidth;
                    }
                    else {
                        next=end;
                    }
                    break;
                }
                pen+=advance;
                end=r.end;
                next=end;
                lastCp=r.cp;
                lastBegin=r.begin;
                lastAdvance=advance;
                if(scan<s.size()) {
                    size_t peek=scan;
                    Rune following;
                    st=readRune(s,peek,following);
                    if(!st)return st;
                    if(!opening(r.cp)&&!closing(following.cp)&&(space(r.cp)||cjk(r.cp)||cjk(following.cp))) {
                        lastBreak=end;
                        lastWidth=pen;
                    }
                }
            }
            if(end==next&&end<s.size()&&count>1&&opening(lastCp)&&end>lastBegin&&lastBegin>begin) {
                end=lastBegin;
                next=end;
                pen-=lastAdvance;
            }
            if(next<=pos)return Status::fail(Error::Corrupt,"排版没有前进");
            out.lines.push_back({begin,end,next,pen});
            pos=next;
            out.next=pos;
        }
        out.eof=out.next>=s.size();
        return {
        };
    }
    uint8_t Canvas::pixel(int x,int y)const {
        if(x<0||y<0||x>=width_||y>=height_)return 3;
        size_t i=size_t(y)*width_+x;
        return (pixels_[i/4]>>(6-2*(i%4)))&3;
    }
    void Canvas::pixel(int x,int y,uint8_t v) {
        if(x<0||y<0||x>=width_||y>=height_)return;
        size_t i=size_t(y)*width_+x;
        int sh=6-2*(i%4);
        pixels_[i/4]=uint8_t((pixels_[i/4]&~(3<<sh))|((v&3)<<sh));
    }
    void Canvas::clear(uint8_t v) {
        std::fill(pixels_.begin(),pixels_.end(),uint8_t((v&3)*85));
    }
    void Canvas::rect(Rect r,uint8_t c,bool fill,int stroke,int radius) {
        if(r.w<=0||r.h<=0)return;
        radius=std::max(0,std::min(radius,std::min(r.w,r.h)/2));
        stroke=std::max(1,stroke);
        auto inside=[](int x,int y,Rect q,int rad) {
            if(!q.contains(x,y))return false;
            if(!rad)return true;
            int dx=std::max({q.x+rad-x,0,x-(q.x+q.w-1-rad)}),dy=std::max({q.y+rad-y,0,y-(q.y+q.h-1-rad)});
            return dx*dx+dy*dy<=rad*rad;
        };
        Rect inner {
            r.x+stroke,r.y+stroke,r.w-2*stroke,r.h-2*stroke
        };
        for(int y=std::max(0,r.y);y<std::min(height_,r.y+r.h);++y)for(int x=std::max(0,r.x);x<std::min(width_,r.x+r.w);++x)if(inside(x,y,r,radius)&&(fill||!inside(x,y,inner,std::max(0,radius-stroke))))pixel(x,y,c);
    }
    void Canvas::line(int x0,int y0,int x1,int y1,uint8_t c,int w) {
        int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
        for(int n=0;n<8192;++n) {
            rect({x0-w/2,y0-w/2,w,w},c);
            if(x0==x1&&y0==y1)break;
            int e=2*err;
            if(e>=dy) {
                err+=dy;
                x0+=sx;
            }
            if(e<=dx) {
                err+=dx;
                y0+=sy;
            }
        }
    }
    void Canvas::circle(int cx,int cy,int r,uint8_t c,bool fill,int stroke) {
        r=std::min(1600,std::max(0,r));
        int inner=std::max(0,r-stroke);
        for(int y=std::max(0,cy-r);y<=std::min(height_-1,cy+r);++y)for(int x=std::max(0,cx-r);x<=std::min(width_-1,cx+r);++x) {
            int q=(x-cx)*(x-cx)+(y-cy)*(y-cy);
            if(q<=r*r&&(fill||q>=inner*inner))pixel(x,y,c);
        }
    }
    void Canvas::arc(int cx,int cy,int r,double from,double to,uint8_t c,int stroke) {
        if(r<1||r>1600||!std::isfinite(from)||!std::isfinite(to))return;
        to=std::min(to,from+360.0);
        constexpr double pi=3.141592653589793;
        for(double a=from;a<=to;a+=0.5) {
            double rad=a*pi/180;
            circle(cx+int(std::lround(r*std::cos(rad))),cy+int(std::lround(r*std::sin(rad))),stroke/2,c);
        }
    }
    Status Canvas::textLine(FontProvider&fonts,FontSpec spec,const std::string&t,int x,int baseline,Rect clip,bool inverse) {
        FontReadBatch batch(fonts);
        if(!batch.status())return batch.status();
        std::vector<Rune>r;
        auto st=decodeUtf8(t,r);
        if(!st)return st;
        int pen=x*64;
        for(auto&c:r) {
            if(c.cp=='\r'||c.cp=='\n')continue;
            Glyph g;
            st=fonts.glyph(c.cp=='\t'?' ':c.cp,spec,g);
            if(!st)return st;
            glyph(g,pen,baseline,clip,spec.gray,inverse);
            pen+=g.advance64;
        }
        return {};
    }
    void Canvas::glyph(const Glyph&g,int pen,int baseline,Rect clip,bool gray,bool inverse) {
            int bx=(pen+32)/64+g.left,by=baseline-g.top;
            for(int y=0;y<g.height;++y)for(int xx=0;xx<g.width;++xx) {
                int px=bx+xx,py=by+y;
                if(!clip.contains(px,py))continue;
                size_t i=size_t(y)*g.width+xx;
                int a=(g.coverage2[i/4]>>(6-2*(i%4)))&3;
                if(!gray)a=a>=2?3:0;
                if(a) {
                    int bg=pixel(px,py),fg=inverse?3:0;
                    pixel(px,py,uint8_t((fg*a+bg*(3-a)+1)/3));
                }
            }
    }
    Status Canvas::text(FontProvider&f,FontSpec s,const std::string&t,Rect r,int lh,bool inverse) {
        FontReadBatch batch(f);
        if(!batch.status())return batch.status();
        if(t.empty())return {
        };
        TextLayout layout;
        auto st=layoutText(f,s,t,0,r.w,std::max(1,r.h/std::max(1,lh)),layout);
        if(!st)return st;
        int y=r.y+s.px;
        for(auto&l:layout.lines) {
            st=textLine(f,s,t.substr(l.begin,l.end-l.begin),r.x,y,r,inverse);
            if(!st)return st;
            y+=lh;
        }
        return {
        };
    }
    void Canvas::dots(const std::string&s,int x,int y,int step,uint8_t c) {
        static const uint8_t d[10][7]= {
            {
                14,17,19,21,25,17,14
            }
            , {
                4,12,4,4,4,4,14
            }
            , {
                14,17,1,2,4,8,31
            }
            , {
                30,1,1,14,1,1,30
            }
            , {
                2,6,10,18,31,2,2
            }
            , {
                31,16,16,30,1,1,30
            }
            , {
                14,16,16,30,17,17,14
            }
            , {
                31,1,2,4,8,8,8
            }
            , {
                14,17,17,14,17,17,14
            }
            , {
                14,17,17,15,1,1,14
            }
        };
        for(char a:s) {
            for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx) {
                int row=a>='0'&&a<='9'?d[a-'0'][yy]:a==':'&&(yy==2||yy==4)?4:0;
                if(row&(1<<(4-xx)))circle(x+xx*step,y+yy*step,std::max(1,step/3),c);
            }
            x+=6*step;
        }
    }
}
