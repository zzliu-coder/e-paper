#include "paper/document.hpp"
#include "paper/image.hpp"
#include "paper/rich_reader.hpp"
#include <iomanip>
#include <sstream>
namespace paper {
    void Reader::setDocumentProgressObserver(DocumentProgress p){
        progressObserver_=p;source_->setProgressObserver(p);if(rich_)rich_->progress(std::move(p));
    }
    Status Reader::setRichEnabled(bool enabled){
        if(opened_)return Status::fail(Error::Busy,"请先退出阅读再切换排版");
        richEnabled_=enabled;return {};
    }
    void Reader::syncRich(){
        current_=rich_->location();next_=current_;chapter_.text=rich_->text();chapter_.href=rich_->href();
        chapter_.images.clear();chapter_.styles.clear();loaded_=current_.chapter;
        layout_={};layout_.next=current_.offset;
    }
    int Reader::progress28()const{
        if(rich_)return rich_->progress28();
        return chapter_.text.empty()?28:int(std::min<size_t>(28,current_.offset*28/chapter_.text.size()));
    }
    std::string Reader::contentDiagnostics()const{
        if(rich_)return rich_->diagnostics();
        bool imagePage=false;
        for(const auto&image:chapter_.images)if(current_.offset>=image.begin&&current_.offset<image.end)imagePage=true;
        return "{\"pipeline\":\"crossmux-css+paper-image-1\",\"chapter_images\":"+std::to_string(chapter_.images.size())+
            ",\"image_page\":"+(imagePage?"true":"false")+",\"image_error\":"+jsonString(imageWarning_)+"}";
    }
    std::vector<ReaderLink> Reader::links(Rect area)const{return rich_?rich_->links(area):std::vector<ReaderLink>{};}
    Status Reader::returnLink(){
        if(!hasLinkReturn())return Status::fail(Error::NotFound,"没有原文返回位置");
        const auto target=history_.back();auto st=pageAt(target,true);if(st)history_.pop_back();return st;
    }
    Status Reader::prepareAdjacentGlyphs(size_t limit,const std::function<bool()>&stop,size_t&prepared){
        if(rich_)return rich_->prepare(limit,stop,prepared);
        prepared=0;
        if(!opened_||next_.chapter!=current_.chapter||next_.offset>=chapter_.text.size()||!limit)return {};
        const auto key=metadata_.identity+":"+std::to_string(next_.chapter)+":"+std::to_string(next_.offset)+":"+
            font_.identity(spec_)+":"+std::to_string(layoutRevision_)+":"+std::to_string(store_.resourceGeneration());
        if(key!=preparedKey_){preparedKey_=key;preparedOffset_=next_.offset;preparedCount_=preparedBytes_=0;}
        FontReadBatch batch(font_);if(!batch.status())return batch.status();
        while(prepared<limit&&preparedCount_<384&&preparedBytes_<48*1024&&preparedOffset_<chapter_.text.size()){
            if(stop&&stop())break;
            for(const auto&i:chapter_.images)if(preparedOffset_>=i.begin&&preparedOffset_<i.end)return {};
            size_t next=preparedOffset_;Rune rune;auto st=readRune(chapter_.text,next,rune);if(!st)return st;
            if(rune.cp!='\r'&&rune.cp!='\n'){
                Glyph glyph;st=font_.glyph(rune.cp=='\t'?' ':rune.cp,spec_,glyph);if(!st)return st;
                preparedBytes_+=glyph.coverage2.size()+128;
            }
            preparedOffset_=next;++prepared;++preparedCount_;
        }
        return {};
    }
    Status Reader::compose(size_t offset,TextLayout&out) {
        for(const auto& image:chapter_.images)if(offset>=image.begin&&offset<image.end){
            out={};out.next=image.end;out.eof=out.next>=chapter_.text.size();return {};
        }
        out={};out.next=offset;
        for(int row=0;row<height_/lineHeight_&&out.next<chapter_.text.size();++row){
            const auto begin=out.next;int inset=0;
            if(begin==0||chapter_.text[begin-1]=='\n'){
                for(const auto&s:chapter_.styles)if(s.begin<=begin&&s.end>begin){inset=std::max(0,std::min(width_/3,int(s.indentEm*spec_.px)));break;}
            }
            TextLayout line;auto st=layoutText(font_,spec_,chapter_.text,begin,width_-inset,1,line);if(!st)return st;
            if(line.next<=begin)return Status::fail(Error::Corrupt,"排版未前进");
            for(auto&l:line.lines){l.inset=inset;out.lines.push_back(l);}out.next=line.next;
        }
        out.eof=out.next>=chapter_.text.size();
        for(const auto& image:chapter_.images)if(image.begin>=offset&&image.begin<out.next){
            while(!out.lines.empty()&&out.lines.back().begin>=image.begin)out.lines.pop_back();
            if(!out.lines.empty()){
                auto& last=out.lines.back();last.end=std::min(last.end,image.begin);last.next=image.begin;
            }
            out.next=image.begin;out.eof=false;break;
        }
        return {};
    }
    Status Reader::load(size_t n) {
        if(n>=metadata_.chapters)return Status::fail(Error::Invalid,"章节不存在");
        if(loaded_==n)return {
        };
        if(previousLoaded_==n){std::swap(chapter_,previousChapter_);std::swap(loaded_,previousLoaded_);return {};}
        Chapter next;
        auto st=source_->chapter(n,next);
        if(!st)return st;
        previousChapter_=std::move(chapter_);previousLoaded_=loaded_;
        chapter_=std::move(next);
        loaded_=n;
        return {
        };
    }
    Status Reader::loadState() {
        std::string payload;
        auto st=store_.loadRecord((rich_?"rich-book-":"book-")+metadata_.identity,payload);
        if(st.code==Error::NotFound)return {
        };
        if(!st)return st;
        std::istringstream in(payload);
        std::string magic,engine;
        size_t ch,off,count;
        if(!(in>>magic>>std::quoted(engine)>>ch>>off>>count)||magic!=(rich_?"PAPERPOS2":"PAPERPOS1")||count>64) return Status::fail(Error::Corrupt,"阅读位置损坏，保留原记录");
        size_t hint=SIZE_MAX;std::string renderKey;
        if(rich_&&(!(in>>hint>>std::quoted(renderKey))||(hint!=SIZE_MAX&&hint>=4096)||renderKey.size()!=64||off>1024*1024))
            return Status::fail(Error::Corrupt,"图文位置损坏，保留原记录");
        // Never interpret offsets from a different parser as if they were ours.
        if(engine!=metadata_.engine)return Status::fail(Error::Conflict,"阅读引擎已变化，原位置需迁移");
        if(ch>=metadata_.chapters)return Status::fail(Error::Corrupt,"阅读位置章节无效");
        std::vector<Bookmark> marks;
        for(size_t i=0;i<count;++i) {
            Bookmark b;
            b.location= {
                metadata_.identity,engine,0,0
            };
            if(!(in>>b.location.chapter>>b.location.offset>>std::quoted(b.excerpt))||b.location.chapter>=metadata_.chapters||b.excerpt.size()>192||!validUtf8(b.excerpt)) return Status::fail(Error::Corrupt,"书签损坏，保留原记录");
            if(rich_&&(!(in>>b.location.pageHint>>std::quoted(b.location.renderKey))||b.location.pageHint>=4096||b.location.renderKey.size()!=64||b.location.offset>1024*1024))
                return Status::fail(Error::Corrupt,"图文书签损坏，保留原记录");
            marks.push_back(std::move(b));
        }
        current_= {
            metadata_.identity,engine,ch,off,hint,renderKey
        };
        bookmarks_=std::move(marks);
        return {
        };
    }
    Status Reader::saveState() {
        if(!opened_)return {
        };
        std::ostringstream out;
        out<<(rich_?"PAPERPOS2 ":"PAPERPOS1 ")<<std::quoted(metadata_.engine)<<' '<<current_.chapter<<' '<<current_.offset<<' '<<bookmarks_.size();
        if(rich_)out<<' '<<current_.pageHint<<' '<<std::quoted(current_.renderKey);
        out<<'\n';
        for(auto&b:bookmarks_){out<<b.location.chapter<<' '<<b.location.offset<<' '<<std::quoted(b.excerpt);
            if(rich_)out<<' '<<b.location.pageHint<<' '<<std::quoted(b.location.renderKey);
            out<<'\n';}
        return store_.saveRecord((rich_?"rich-book-":"book-")+metadata_.identity,out.str());
    }
    Status Reader::pageAt(Locator pos,bool save) {
        if(!opened_||pos.identity!=metadata_.identity||pos.engine!=metadata_.engine)return Status::fail(Error::Conflict,"位置不属于本书或当前引擎");
        if(rich_){
            auto old=current_;auto st=rich_->configure(spec_,width_,height_,lineHeight_);
            if(st)st=rich_->go(pos);
            if(st){syncRich();if(save)st=saveState();}
            if(!st){rich_->progress({});rich_->go(old);rich_->progress(progressObserver_);syncRich();}
            return st;
        }
        const auto old=current_;
        auto st=load(pos.chapter);
        if(!st)return st;
        if(pos.offset>chapter_.text.size()||(pos.offset<chapter_.text.size()&&(uint8_t(chapter_.text[pos.offset])&192)==128)) {
            load(old.chapter);
            return Status::fail(Error::Corrupt,"阅读位置超出文本或不是字符边界");
        }
        st=ensureCoverage();
        if(!st){load(old.chapter);return st;}
        TextLayout newLayout;
        const auto key=std::make_pair(pos.chapter,pos.offset);
        if(pageGeneration_!=store_.resourceGeneration()){pageCache_.clear();pageGeneration_=store_.resourceGeneration();}
        auto cached=pageCache_.find(key);
        if(cached!=pageCache_.end())newLayout=cached->second;
        else st=compose(pos.offset,newLayout);
        if(!st) {
            load(old.chapter);
            return st;
        }
        current_=pos;
        next_=pos;
        next_.offset=newLayout.next;
        if(newLayout.eof&&pos.chapter+1<metadata_.chapters) {
            next_.chapter=pos.chapter+1;
            next_.offset=0;
        }
        layout_=std::move(newLayout);
        if(pageCache_.size()>=64)pageCache_.erase(pageCache_.begin());
        pageCache_[key]=layout_;
        if(save) {
            st=saveState();
            if(!st) {
                current_=old;
                load(old.chapter);
                compose(old.offset,layout_);
                next_=old;
                next_.offset=layout_.next;
                if(layout_.eof&&old.chapter+1<metadata_.chapters) {
                    next_.chapter++;
                    next_.offset=0;
                }
                return st;
            }
        }
        return {
        };
    }
    Status Reader::open(const std::string&path) {
        auto st=close();
        if(!st)return st;
        Metadata meta;
        st=source_->open(path,meta);
        if(!st) {
            source_->close();
            return st;
        }
        if(!meta.chapters||meta.identity.size()!=64) {
            source_->close();
            return Status::fail(Error::Corrupt,"书籍元数据无效");
        }
        metadata_=std::move(meta);
        if(richEnabled_&&source_->hasRichContent()){
            metadata_.engine="paper-crossmux-1";
            rich_=std::make_shared<RichReader>(store_,font_,*source_,metadata_);
            rich_->progress(progressObserver_);
        }
        loaded_=SIZE_MAX;
        opened_=true;
        current_= {
            metadata_.identity,metadata_.engine,0,0
        };
        fontWarning_.clear();fallbackCodepoints_.clear();validatedChapter_=SIZE_MAX;
        st=styles_.load(legacyStyle_);
        if(st){auto desired=styles_.effective(metadata_.identity);spec_=desired.font;if(spec_.gray&&!grayAllowed_){spec_.gray=false;fontWarning_="本机灰阶未验证，本次使用黑白显示";}width_=480-desired.margin*2;lineHeight_=desired.lineHeight;}
        if(st)st=loadState();
        if(st){
            st=pageAt(current_,false);
            if(!st&&(st.code==Error::ResourceMissing||st.code==Error::NotFound||st.code==Error::Corrupt||st.code==Error::TooLarge)){
                auto reason=st.message;spec_=legacyStyle_.font;width_=480-legacyStyle_.margin*2;lineHeight_=legacyStyle_.lineHeight;validatedChapter_=SIZE_MAX;
                auto safe=pageAt(current_,false);
                if(safe){fontWarning_="已临时使用 MiSans；保存的阅读字体未改："+reason;st={};}
            }
        }
        if(st)++layoutRevision_;
        if(!st) {
            rich_.reset();
            source_->close();
            opened_=false;
            loaded_=SIZE_MAX;
            return st;
        }
        return {
        };
    }
    Status Reader::close() {
        auto st=saveState();
        if(!st)return st;
        rich_.reset();source_->close();
        opened_=false;
        imageWarning_.clear();
        loaded_=SIZE_MAX;
        previousLoaded_=SIZE_MAX;previousChapter_={};pageCache_.clear();
        chapter_= {
        };
        metadata_= {
        };
        history_.clear();
        bookmarks_.clear();
        layout_= {
        };
        return {
        };
    }
    Status Reader::next() {
        if(!opened_)return Status::fail(Error::Conflict,"尚未打开书籍");
        if(rich_){
            auto old=current_;auto st=rich_->next();
            if(st){syncRich();st=saveState();}
            if(!st){rich_->progress({});rich_->go(old);rich_->progress(progressObserver_);syncRich();}
            return st;
        }
        if(next_.chapter==current_.chapter&&next_.offset==current_.offset)return Status::fail(Error::NotFound,"已到末尾");
        if(next_.chapter==metadata_.chapters-1&&next_.offset==chapter_.text.size()&&layout_.eof&&current_.chapter==next_.chapter) return Status::fail(Error::NotFound,"已到末尾");
        auto old=current_;
        auto st=pageAt(next_,true);
        if(st) {
            if(history_.size()==128)history_.erase(history_.begin());
            history_.push_back(old);
        }
        return st;
    }
    Status Reader::previous() {
        if(!opened_)return Status::fail(Error::Conflict,"尚未打开书籍");
        if(rich_){
            auto old=current_;auto st=rich_->previous();
            if(st){syncRich();st=saveState();}
            if(!st){rich_->progress({});rich_->go(old);rich_->progress(progressObserver_);syncRich();}
            return st;
        }
        if(!history_.empty()) {
            auto st=pageAt(history_.back(),true);
            if(st)history_.pop_back();
            return st;
        }
        if(!current_.chapter&&!current_.offset)return Status::fail(Error::NotFound,"已到开头");
        auto original=current_;
        size_t n=current_.offset?current_.chapter:current_.chapter-1;
        auto st=load(n);
        if(!st)return st;
        size_t limit=n==original.chapter?original.offset:chapter_.text.size();
        size_t at=0,prev=0;
        for(const auto&entry:pageCache_)if(entry.first.first==n&&entry.first.second<limit)at=std::max(at,entry.first.second);
        for(size_t pages=0;at<limit&&pages<20000;++pages) {
            TextLayout t;
            st=compose(at,t);
            if(!st)break;
            prev=at;
            if(t.next<=at||t.next>=limit) {
                at=limit;
                break;
            }
            at=t.next;
        }
        if(!st) {
            load(original.chapter);
            return st;
        }
        return pageAt({metadata_.identity,metadata_.engine,n,prev},true);
    }
    Status Reader::ensureCoverage() {
        auto st=font_.validate(spec_);if(!st)return st;
        auto id=font_.identity(spec_);
        if(validatedChapter_==loaded_&&validatedSpec_==spec_&&validatedFont_==id)return {};
        std::vector<uint32_t>missing;st=font_.coverage(spec_,chapter_.text,missing);if(!st)return st;
        validatedChapter_=loaded_;validatedSpec_=spec_;validatedFont_=id;fallbackCodepoints_=std::move(missing);return {};
    }
    Status Reader::layout(FontSpec spec,int margin,int lineHeight) {
        if(spec.gray&&!grayAllowed_)return Status::fail(Error::Unavailable,"本机灰阶尚未完成验证");
        ReaderStyle requested{spec,margin,lineHeight};
        if(!validReaderStyle(requested))return Status::fail(Error::Invalid,"排版参数超出范围");
        auto st=font_.validate(spec);if(!st)return st;
        auto oldSpec=spec_;int ow=width_,ol=lineHeight_;auto oldLayout=layout_;auto oldNext=next_;auto oldMissing=fallbackCodepoints_;auto oldLocation=current_;
        pageCache_.clear();
        spec_=spec;width_=480-margin*2;lineHeight_=lineHeight;validatedChapter_=SIZE_MAX;
        if(opened_){
            st=pageAt(current_,false);
            if(st){Canvas scratch(480,800);st=paint(scratch,{margin,0,480-margin*2,height_});}
            if(!st){pageCache_.clear();spec_=oldSpec;width_=ow;lineHeight_=ol;layout_=std::move(oldLayout);next_=oldNext;fallbackCodepoints_=std::move(oldMissing);validatedChapter_=SIZE_MAX;
                if(rich_){rich_->progress({});rich_->configure(spec_,width_,height_,lineHeight_);rich_->go(oldLocation);rich_->progress(progressObserver_);syncRich();}return st;}
        }else legacyStyle_=requested;
        history_.clear();++layoutRevision_;return {};
    }
    Status Reader::initializeStyles(const ReaderStyle& legacy) {
        if (opened_ || !validReaderStyle(legacy)) return Status::fail(Error::Conflict,"阅读设置初始化条件无效");
        legacyStyle_ = legacy;
        if(!grayAllowed_)legacyStyle_.font.gray=false;
        return styles_.load(legacy);
    }
    Status Reader::applyDefaultStyle(const ReaderStyle& requested) {
        if(requested.font.gray&&!grayAllowed_)return Status::fail(Error::Unavailable,"本机灰阶尚未完成验证");
        if (opened_) return applyStyle(requested, ReaderStyleScope::Default);
        if (!validReaderStyle(requested)) return Status::fail(Error::Invalid,"阅读默认设置无效");
        auto st = font_.validate(requested.font);
        if (!st) return st;
        return styles_.commit("",requested,ReaderStyleScope::Default);
    }
    Status Reader::applyStyle(const ReaderStyle&requested,ReaderStyleScope scope) {
        if(!opened_)return Status::fail(Error::Conflict,"请先打开一本书");
        ReaderStyle next=scope==ReaderStyleScope::FollowDefault?styles_.defaults():requested;
        const auto old=style();auto oldLayout=layout_;auto oldNext=next_;auto oldHistory=history_;auto oldMissing=fallbackCodepoints_;auto revision=layoutRevision_;auto oldLocation=current_;
        auto st=layout(next.font,next.margin,next.lineHeight);if(!st)return st;
        st=styles_.commit(metadata_.identity,next,scope);
        if(!st){pageCache_.clear();spec_=old.font;width_=480-old.margin*2;lineHeight_=old.lineHeight;layout_=std::move(oldLayout);next_=oldNext;history_=std::move(oldHistory);fallbackCodepoints_=std::move(oldMissing);layoutRevision_=revision;validatedChapter_=SIZE_MAX;
            if(rich_){rich_->progress({});rich_->configure(spec_,width_,height_,lineHeight_);rich_->go(oldLocation);rich_->progress(progressObserver_);syncRich();}return st;}
        fontWarning_.clear();return {};
    }
    Status Reader::previewStyle(const ReaderStyle&s,Canvas&canvas,Rect area) {
        if(s.font.gray&&!grayAllowed_)return Status::fail(Error::Unavailable,"本机灰阶尚未完成验证");
        if(!opened_)return Status::fail(Error::Conflict,"请先打开一本书");
        if(!validReaderStyle(s))return Status::fail(Error::Invalid,"预览规格无效");
        std::vector<uint32_t>missing;auto st=font_.coverage(s.font,chapter_.text,missing);if(!st)return st;
        TextLayout layout;st=layoutText(font_,s.font,chapter_.text,rich_?0:current_.offset,area.w,area.h/s.lineHeight,layout);if(!st)return st;
        int y=area.y+s.font.px;for(auto&line:layout.lines){st=canvas.textLine(font_,s.font,chapter_.text.substr(line.begin,line.end-line.begin),area.x,y,area);if(!st)return st;y+=s.lineHeight;}return {};
    }
    Status Reader::go(const Locator&l) {
        auto st=pageAt(l,true);
        if(st)history_.clear();
        return st;
    }
    Status Reader::jump(size_t n) {
        if(n>=metadata_.toc.size())return Status::fail(Error::Invalid,"目录项不存在");
        auto t=metadata_.toc[n];
        if(rich_){auto old=current_;auto st=rich_->anchor(t.chapter,t.fragment);if(st){syncRich();st=saveState();}
            if(!st){rich_->progress({});rich_->go(old);rich_->progress(progressObserver_);syncRich();}return st;}
        auto st=load(t.chapter);
        if(!st)return st;
        size_t off=0;
        if(!t.fragment.empty()) {
            auto it=chapter_.anchors.find(t.fragment);
            if(it!=chapter_.anchors.end())off=it->second;
        }
        return go({metadata_.identity,metadata_.engine,t.chapter,off});
    }
    Status Reader::follow(const std::string&href) {
        if(!opened_)return Status::fail(Error::Conflict,"没有打开书籍");
        auto original=current_;
        std::string wanted,fragment;
        auto slash=chapter_.href.rfind('/');
        auto base=slash==std::string::npos?"":chapter_.href.substr(0,slash+1);
        auto st=href.size()&&href[0]=='#'?archivePath("",chapter_.href+href,wanted,&fragment):archivePath(base,href,wanted,&fragment);
        if(!st)return st;
        if(rich_){
            for(size_t n=0;n<metadata_.chapters;++n){auto path=source_->chapterHref(n);
                if(path==wanted){st=rich_->anchor(n,fragment);if(st){syncRich();st=saveState();if(st){if(history_.size()>=128)history_.erase(history_.begin());history_.push_back(original);}}
                    if(!st){rich_->progress({});rich_->go(original);rich_->progress(progressObserver_);syncRich();}return st;}}
            return Status::fail(Error::NotFound,"链接目标不在阅读顺序中");
        }
        for(size_t n=0;n<metadata_.chapters;++n) {
            st=load(n);
            if(!st) {
                load(original.chapter);
                return st;
            }
            if(chapter_.href==wanted) {
                size_t off=0;
                if(!fragment.empty()) {
                    auto it=chapter_.anchors.find(fragment);
                    if(it==chapter_.anchors.end()) {
                        load(original.chapter);
                        return Status::fail(Error::NotFound,"书内锚点不存在");
                    }
                    off=it->second;
                }
                st=pageAt({metadata_.identity,metadata_.engine,n,off},true);
                if(st)history_.push_back(original);
                return st;
            }
        }
        load(original.chapter);
        return Status::fail(Error::NotFound,"链接目标不在阅读顺序中");
    }
    Status Reader::bookmark() {
        if(!opened_)return Status::fail(Error::Conflict,"没有打开书籍");
        for(auto&b:bookmarks_)if(b.location.chapter==current_.chapter&&b.location.offset==current_.offset&&(!rich_||b.location.pageHint==current_.pageHint))return {
        };
        if(bookmarks_.size()>=64)return Status::fail(Error::TooLarge,"本书已达64个书签");
        const auto begin=rich_?0:current_.offset;
        size_t end=std::min(chapter_.text.size(),begin+120);
        while(end<chapter_.text.size()&&(uint8_t(chapter_.text[end])&192)==128)--end;
        bookmarks_.push_back({current_,chapter_.text.substr(begin,end-begin)});
        auto st=saveState();
        if(!st)bookmarks_.pop_back();
        return st;
    }
    Status Reader::find(const std::string&q,std::vector<Locator>&out,size_t limit) {
        out.clear();
        if(!opened_||q.empty()||q.size()>192||!validUtf8(q)||limit>64||!limit)return Status::fail(Error::Invalid,"搜索文字无效");
        if(rich_){auto st=rich_->find(q,out,limit);syncRich();return st;}
        auto original=current_;
        Status st;
        for(size_t n=0;n<metadata_.chapters&&out.size()<limit;++n) {
            st=load(n);
            if(!st)break;
            size_t pos=0;
            while((pos=chapter_.text.find(q,pos))!=std::string::npos&&out.size()<limit) {
                out.push_back({metadata_.identity,metadata_.engine,n,pos});
                pos+=q.size();
            }
        }
        auto restore=load(original.chapter);
        return st?restore:st;
    }
    Status Reader::paint(Canvas&canvas,Rect area) {
        imageWarning_.clear();
        if(!opened_)return Status::fail(Error::Conflict,"未打开书籍");
        if(rich_){auto st=rich_->paint(canvas,area);imageWarning_=rich_->warning();return st;}
        for(const auto& image:chapter_.images)if(current_.offset>=image.begin&&current_.offset<image.end){
            std::vector<uint8_t> bytes;
            auto st=source_->resource(image.href,bytes,1024*1024);
            if(st)st=paintBookImage(bytes,canvas,area);
            if(!st){
                imageWarning_=st.message;
                return canvas.text(font_,spec_,"插图暂无法显示："+st.message,area,lineHeight_);
            }
            return {};
        }
        FontReadBatch batch(font_);
        if(!batch.status())return batch.status();
        int y=area.y+spec_.px;
        for(auto&line:layout_.lines) {
            int x=area.x+line.inset;bool rule=false;
            for(const auto& style:chapter_.styles)if(style.begin<line.end&&style.end>line.begin){
                if(style.align==1)x+=std::max(0,(area.w-line.inset-line.width64/64)/2);
                if(style.align==2)x+=std::max(0,area.w-line.inset-line.width64/64);
                rule=style.heading;break;
            }
            // One installed face supplies metrics and pixels: mixed emphasis does
            // not thrash multi-megabyte SD face indexes on every inline boundary.
            int pen=x*64;size_t pos=line.begin;
            while(pos<line.end){
                const auto at=pos;Rune rune;auto st=readRune(chapter_.text,pos,rune);if(!st)return st;
                Glyph glyph;st=font_.glyph(rune.cp=='\t'?' ':rune.cp,spec_,glyph);if(!st)return st;
                const Chapter::StyleRange*style=nullptr;
                auto it=std::upper_bound(chapter_.styles.begin(),chapter_.styles.end(),at,[](size_t value,const auto&s){return value<s.begin;});
                if(it!=chapter_.styles.begin()){--it;if(at<it->end)style=&*it;}
                if(style&&style->italic){
                    const int width=glyph.width+std::max(0,glyph.height/6);
                    std::vector<uint8_t> pixels((width*glyph.height+3)/4,0);
                    for(int yy=0;yy<glyph.height;++yy)for(int xx=0;xx<glyph.width;++xx){
                        size_t src=size_t(yy)*glyph.width+xx,dst=size_t(yy)*width+xx+(glyph.height-1-yy)/6;
                        auto v=(glyph.coverage2[src/4]>>(6-2*(src%4)))&3;pixels[dst/4]|=v<<(6-2*(dst%4));
                    }
                    glyph.width=width;glyph.coverage2=std::move(pixels);
                }
                canvas.glyph(glyph,pen,y,area,spec_.gray,false);
                if(style&&style->bold)canvas.glyph(glyph,pen+64,y,area,spec_.gray,false);
                if(style&&style->strike)canvas.line(pen/64,y-spec_.px/3,std::min(area.x+area.w-1,(pen+glyph.advance64)/64),y-spec_.px/3,0,1);
                pen+=glyph.advance64;
            }
            if(rule&&y+2<area.y+area.h)canvas.line(x,y+2,std::min(area.x+area.w-1,x+line.width64/64),y+2,0,1);
            if(!rule&&y+2<area.y+area.h)for(const auto& style:chapter_.styles){
                if(!style.underline||style.begin>=line.end||style.end<=line.begin)continue;
                const auto begin=std::max(style.begin,line.begin),end=std::min(style.end,line.end);
                size_t pos=line.begin;int pen=0,start=0;
                while(pos<end){
                    if(pos==begin)start=pen;
                    Rune rune;auto measured=readRune(chapter_.text,pos,rune);if(!measured)return measured;
                    int advance=0;measured=font_.advance(rune.cp,spec_,advance);if(!measured)return measured;
                    pen+=advance;
                }
                if(pen>start)canvas.line(x+start/64,y+2,std::min(area.x+area.w-1,x+pen/64-1),y+2,0,1);
            }
            y+=lineHeight_;
        }
        return {
        };
    }
}
