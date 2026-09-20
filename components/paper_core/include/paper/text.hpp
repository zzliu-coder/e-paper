#pragma once
#include "storage.hpp"
#include "font_proof.hpp"
#include <list>
#include <unordered_map>
#include <cstdio>
namespace paper {
    class FontProof;
    struct FontSpec {
        int px=22,weight=500;
        bool gray=false;
        // Empty revision selects the installed revision. System text always uses
        // the default family and has no dependency on reader preferences.
        std::string family="misans",revision;
        bool allowFallback=false;
        bool uiOnly=false;
        FontSpec()=default;
        FontSpec(int p,int w,bool g,std::string f="misans",std::string r="",bool fallback=false)
          :px(p),weight(w),gray(g),family(std::move(f)),revision(std::move(r)),allowFallback(fallback){}
        bool operator==(const FontSpec&o)const {
            return px==o.px&&weight==o.weight&&gray==o.gray&&family==o.family&&revision==o.revision&&allowFallback==o.allowFallback&&uiOnly==o.uiOnly;
        }
    };
    struct Glyph {
        int advance64=0,width=0,height=0,left=0,top=0;
        std::vector<uint8_t> coverage2;
    };
    class FontProvider {
        public:virtual ~FontProvider()=default;
        virtual Status glyph(uint32_t,const FontSpec&,Glyph&)=0;
        virtual Status advance(uint32_t c,const FontSpec&s,int&v) {
            Glyph g;
            auto st=glyph(c,s,g);
            v=st?g.advance64:s.px*64;
            return st;
        }
        virtual std::string identity(const FontSpec&)const=0;
        virtual Status validate(const FontSpec&) { return {}; }
        virtual Status coverage(const FontSpec&,const std::string&,std::vector<uint32_t>&missing) { missing.clear();return {}; }
        virtual Status beginReadBatch() { return {}; }
        virtual void endReadBatch() {}
    };
    // A short-lived drawing scope, never a permanently open SD handle.
    class FontReadBatch {
        FontProvider& fonts_;
        Status status_;
    public:
        explicit FontReadBatch(FontProvider& f):fonts_(f),status_(f.beginReadBatch()){}
        ~FontReadBatch(){if(status_)fonts_.endReadBatch();}
        const Status& status()const{return status_;}
        FontReadBatch(const FontReadBatch&)=delete;
        FontReadBatch& operator=(const FontReadBatch&)=delete;
    };
    class PackedFonts final:public FontProvider {
        struct Face {
            bool uiOnly=false;
            int px=0,weight=0;
            uint32_t dataOffset=0,dataLength=0;
            std::string path,identity,family,revision;
            std::vector<uint8_t>index;
            std::unique_ptr<FontProof> proof;
            uint64_t used=0;
        };
        struct Cache {
            std::string key;
            Glyph glyph;
        };
        Store&store_;
        Budget budget_;
        std::vector<Face>faces_;
        std::list<Cache>cache_;
        std::unordered_map<std::string,std::list<Cache>::iterator>cacheLookup_;
        std::map<std::string,std::string>verified_;
        uint64_t generation_=0;
        uint64_t validations_=0,indexLoads_=0,indexHits_=0,glyphHits_=0,glyphReads_=0;
        uint64_t hashUs_=0,validationUs_=0,indexUs_=0,glyphIoUs_=0,glyphOpens_=0;
        bool batchEnabled_=true;
        bool singlePassVerification_=true;
        bool forceFullVerification_=false;
        std::function<Status(size_t,size_t)> progress_;
        size_t batchDepth_=0;
        FILE* batchFile_=nullptr;
        std::string batchPath_;
        Lease batchLease_;
        void closeBatchFile();
        size_t bytes_=0;
        uint64_t tick_=0;
        mutable std::recursive_mutex mutex_;
        Status face(const FontSpec&,Face*&);
        Status record(uint32_t,const Face&,const uint8_t*&);
        Status exactGlyph(uint32_t,const FontSpec&,Glyph&);
        Status exactAdvance(uint32_t,const FontSpec&,int&);
        public: PackedFonts(Store&s,Budget b={}):store_(s),budget_(b) {
        }
        Status glyph(uint32_t,const FontSpec&,Glyph&) override;
        ~PackedFonts() override { clear(); }
        Status beginReadBatch() override;
        void endReadBatch() override;
        Status setBatchEnabled(bool);
        Status setSinglePassVerification(bool);
        void setProgressObserver(std::function<Status(size_t,size_t)> observer){
            std::lock_guard<std::recursive_mutex> guard(mutex_);progress_=std::move(observer);
        }
        Status advance(uint32_t,const FontSpec&,int&) override;
        std::string identity(const FontSpec&)const override;
        size_t cacheBytes()const {
            std::lock_guard<std::recursive_mutex> guard(mutex_);
            return bytes_;
        }
        void clear();
        void clearGlyphCache();
        // Release RAM/handles while retaining validation for the same media generation.
        void releaseMemory();
        std::string statistics() const;
        Status validate(const FontSpec&) override;
        Status revalidate(const FontSpec&); // invalidate only this face, retain UI indexes
        Status coverage(const FontSpec&,const std::string&,std::vector<uint32_t>&) override;
    };
    struct TextLine {
        size_t begin=0,end=0,next=0;
        int width64=0;
        int inset=0;
    };
    struct TextLayout {
        std::vector<TextLine>lines;
        size_t next=0;
        bool eof=false;
    };
    Status layoutText(FontProvider&,const FontSpec&,const std::string&,size_t offset,int width,int maxLines,TextLayout&);
    class Canvas {
        int width_,height_;
        std::vector<uint8_t>pixels_;
        public: Canvas(int w=480,int h=800):width_(w),height_(h),pixels_((size_t(w)*h+3)/4,255) {
        }
        int width()const {
            return width_;
        }
        int height()const {
            return height_;
        }
        const std::vector<uint8_t>&bytes()const {
            return pixels_;
        }
        uint8_t pixel(int x,int y)const;
        void pixel(int x,int y,uint8_t level);
        void clear(uint8_t level=3);
        void rect(Rect,uint8_t color,bool fill=true,int stroke=2,int radius=0);
        void line(int x0,int y0,int x1,int y1,uint8_t color,int width=1);
        void circle(int cx,int cy,int radius,uint8_t color,bool fill=true,int stroke=8);
        void arc(int cx,int cy,int radius,double from,double to,uint8_t color,int stroke=8);
        Status text(FontProvider&,FontSpec,const std::string&,Rect,int lineHeight=32,bool inverse=false);
        Status textLine(FontProvider&,FontSpec,const std::string&,int x,int baseline,Rect clip,bool inverse=false);
        void glyph(const Glyph&,int pen64,int baseline,Rect clip,bool gray=false,bool inverse=false);
        void dots(const std::string&,int x,int y,int step,uint8_t level=0);
        uint32_t checksum()const {
            return crc32(pixels_.data(),pixels_.size());
        }
    };
}
