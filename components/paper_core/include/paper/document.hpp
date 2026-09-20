#pragma once
#include "text.hpp"
#include "reader_style.hpp"
namespace paper {
    class RichReader;
    using DocumentProgress=std::function<Status(const char*,size_t,size_t)>;
    struct Link {
        size_t begin=0,end=0;
        std::string href;
    };
    struct Chapter {
        struct StyleRange {
            size_t begin=0,end=0;
            int align=0; // 0 left, 1 center, 2 right
            bool heading=false,underline=false;
            bool bold=false,italic=false,strike=false;
            float indentEm=0;
        };
        std::string href,title,text;
        std::map<std::string,size_t>anchors;
        std::vector<Link>links;
        std::vector<StyleRange>styles;
        struct ImageBlock {size_t begin=0,end=0;std::string href;};
        std::vector<ImageBlock>images;
    };
    struct TocEntry {
        std::string title;
        size_t chapter=0;
        std::string fragment;
    };
    struct Metadata {
        std::string title,author,engine,identity;
        size_t chapters=0;
        std::vector<TocEntry>toc;
    };
    class DocumentSource {
        public:virtual ~DocumentSource()=default;
        virtual void setProgressObserver(DocumentProgress) {}
        virtual Status open(const std::string&relative,Metadata&)=0;
        virtual Status chapter(size_t index,Chapter&)=0;
        virtual bool hasRichContent()const{return false;}
        virtual std::string chapterHref(size_t)const{return {};}
        virtual Status chapterMarkup(size_t,std::string&,std::string&,std::string&){return Status::fail(Error::Unsupported,"无图文源");}
        virtual Status resource(const std::string&href,std::vector<uint8_t>&,size_t cap)=0;
        virtual void close()=0;
    };
    // Compact built-in backend; CrossMux may be selected through the same interface.
    // Its explicit capabilities are EPUB/TXT + TOC/local links, NOT full browser CSS.
    class LocalDocument final:public DocumentSource {
        struct Entry {
            std::string name;
            uint32_t compressed=0,size=0,offset=0,crc=0;
            uint16_t method=0,flags=0;
        };
        Store&store_;
        Budget budget_;
        std::string relative_,absolute_,base_,encoding_;
        std::vector<Entry>entries_;
        std::vector<std::string>spine_,titles_;
        std::vector<uint64_t>txtOffsets_;
        Metadata meta_;
        bool epub_=false;
        bool opened_=false,cacheValid_=false;
        uint64_t cachedGeneration_=0;
        LeaseGuard lease_;
        DocumentProgress progress_;
        Status zipIndex();
        Status item(const std::string&,std::vector<uint8_t>&,size_t);
        public: LocalDocument(Store&s,Budget b={}):store_(s),budget_(b) {
        }
        Status open(const std::string&,Metadata&)override;
        void setProgressObserver(DocumentProgress p)override{progress_=std::move(p);}
        Status chapter(size_t,Chapter&)override;
        bool hasRichContent()const override{return epub_;}
        std::string chapterHref(size_t n)const override{return epub_&&n<spine_.size()?spine_[n]:"";}
        Status chapterMarkup(size_t,std::string&,std::string&,std::string&)override;
        Status resource(const std::string&,std::vector<uint8_t>&,size_t)override;
        void close()override;
    };
    Status markupToChapter(const std::string&html,const std::string&href,Chapter&out,size_t cap,const std::string&css="",DocumentProgress progress={});
    Status archivePath(const std::string&base,const std::string&href,std::string&path,std::string*fragment=nullptr);
    Status inflateRaw(const std::vector<uint8_t>&in,std::vector<uint8_t>&out,size_t expected,size_t cap,DocumentProgress progress={});
    struct Locator {
        std::string identity,engine;
        size_t chapter=0,offset=0;
        // CrossMux uses visible-codepoint offsets; the page hint disambiguates
        // image-only pages sharing an offset. Hints are valid only for renderKey.
        size_t pageHint=SIZE_MAX;
        std::string renderKey;
    };
    struct Bookmark {
        Locator location;
        std::string excerpt;
    };
    struct ReaderLink {Rect box;std::string href;};
    class Reader {
        Store&store_;
        FontProvider&font_;
        std::unique_ptr<DocumentSource>source_;
        std::shared_ptr<RichReader>rich_;
        bool richEnabled_=true;
        DocumentProgress progressObserver_;
        void syncRich();
        Metadata metadata_;
        Chapter chapter_;
        Chapter previousChapter_;
        size_t previousLoaded_=SIZE_MAX;
        std::map<std::pair<size_t,size_t>,TextLayout> pageCache_;
        uint64_t pageGeneration_=0;
        bool grayAllowed_=true;
        size_t loaded_=SIZE_MAX;
        FontSpec spec_ {
            26,400,false
        };
        int width_=432,height_=560,lineHeight_=40;
        Locator current_,next_;
        TextLayout layout_;
        std::vector<Locator>history_;
        std::vector<Bookmark>bookmarks_;
        bool opened_=false;
        ReaderStyleStore styles_;
        ReaderStyle legacyStyle_;
        uint64_t layoutRevision_=0;
        size_t validatedChapter_=SIZE_MAX;
        std::string validatedFont_,fontWarning_;
        std::string imageWarning_;
        std::vector<uint32_t>fallbackCodepoints_;
        FontSpec validatedSpec_;
        std::string preparedKey_;
        size_t preparedOffset_=0,preparedCount_=0,preparedBytes_=0;
        Status ensureCoverage();
        Status load(size_t);
        Status compose(size_t offset,TextLayout&);
        Status pageAt(Locator,bool save);
        Status loadState();
        Status saveState();
        public: Reader(Store&s,FontProvider&f,std::unique_ptr<DocumentSource>d):store_(s),font_(f),source_(std::move(d)),styles_(s) {
        }
        Status open(const std::string&);
        void setDocumentProgressObserver(DocumentProgress);
        Status setRichEnabled(bool enabled);
        bool richEnabled()const{return richEnabled_;}
        int progress28()const;
        std::string contentDiagnostics()const;
        void setGrayAllowed(bool allowed) { grayAllowed_=allowed; }
        Status close();
        Status next();
        Status previous();
        Status layout(FontSpec,int margin,int lineHeight);
        Status jump(size_t tocIndex);
        Status go(const Locator&);
        Status follow(const std::string&href);
        std::vector<ReaderLink> links(Rect)const;
        bool hasLinkReturn()const{return rich_&&!history_.empty();}
        Status returnLink();
        Status bookmark();
        Status find(const std::string&,std::vector<Locator>&,size_t limit=32);
        bool opened()const {
            return opened_;
        }
        const Metadata&metadata()const {
            return metadata_;
        }
        const Locator&location()const {
            return current_;
        }
        const std::vector<Bookmark>&bookmarks()const {
            return bookmarks_;
        }
        const TextLayout&page()const {
            return layout_;
        }
        const Chapter&chapter()const {
            return chapter_;
        }
        FontSpec fontSpec()const {
            return spec_;
        }
        int lineHeight()const {
            return lineHeight_;
        }
        int margin()const {
            return (480-width_)/2;
        }
        const Locator&nextLocation()const {
            return next_;
        }
        Status paint(Canvas&,Rect);
        // Same-chapter lookahead only. Does not navigate, save or decode images.
        Status prepareAdjacentGlyphs(size_t limit,const std::function<bool()>&stop,size_t&prepared);
        void resetPreparation(){preparedKey_.clear();}
        Status initializeStyles(const ReaderStyle&legacy);
        Status applyDefaultStyle(const ReaderStyle&);
        Status applyStyle(const ReaderStyle&,ReaderStyleScope);
        Status previewStyle(const ReaderStyle&,Canvas&,Rect);
        ReaderStyle style()const{return {spec_,margin(),lineHeight_};}
        ReaderStyle defaultStyle()const{return styles_.defaults();}
        bool hasFontOverride()const{return styles_.hasOverride(metadata_.identity);}
        uint64_t layoutRevision()const{return layoutRevision_;}
        const std::string& fontWarning()const{return fontWarning_;}
        const std::string& imageWarning()const{return imageWarning_;}
        const std::vector<uint32_t>& fallbackCodepoints()const{return fallbackCodepoints_;}
    };
}
