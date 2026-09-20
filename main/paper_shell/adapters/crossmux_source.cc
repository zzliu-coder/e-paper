#include "crossmux_source.hpp"
#include <Epub.h>
#include <Print.h>
#include <cstring>
namespace paper {
namespace {
class BoundedOutput final:public Print {
 std::vector<uint8_t>&out_;size_t cap_;
 public:bool overflow=false;BoundedOutput(std::vector<uint8_t>&o,size_t cap):out_(o),cap_(cap){}
 size_t write(uint8_t b)override{return write(&b,1);}
 size_t write(const uint8_t*p,size_t n)override{if(n>cap_-out_.size()){overflow=true;return 0;}out_.insert(out_.end(),p,p+n);return n;}
};
std::string localHref(std::string base,const std::string&href){if(!base.empty()&&base.back()!='/')base+='/';return href.rfind(base,0)==0?href.substr(base.size()):href;}
}
struct CrossMuxSource::Impl {Store&store;std::unique_ptr<Epub>book;LeaseGuard lease;Metadata meta;explicit Impl(Store&s):store(s){}};
CrossMuxSource::CrossMuxSource(Store&s):impl_(std::make_unique<Impl>(s)){}CrossMuxSource::~CrossMuxSource()=default;
void CrossMuxSource::close(){impl_->book.reset();impl_->lease.reset();impl_->meta={};}
Status CrossMuxSource::open(const std::string&relative,Metadata&out){close();auto&p=*impl_;auto st=p.lease.acquire(p.store.gate(),"sd","crossmux-reader");if(!st)return st;std::string file,cache;st=p.store.path(relative,file,false);if(st)st=p.store.path(".paper/cache",cache,false);if(!st){close();return st;}
 p.book=std::make_unique<Epub>(file,cache);
 // This adapter reuses upstream container/metadata extraction. CSS/ParsedText/
 // image layout are NOT integrated by this adapter and must not be advertised.
 if(!p.book->load(true,true)){close();return Status::fail(Error::Corrupt,"CrossMux无法打开此EPUB");}
 p.meta.title=p.book->getTitle();p.meta.author=p.book->getAuthor();p.meta.engine="crossmux-extract-paper-layout-1";p.meta.chapters=size_t(p.book->getSpineItemsCount());
 st=p.store.hash(relative,p.meta.identity);if(!st||!p.meta.chapters){close();return st?Status::fail(Error::Corrupt,"EPUB阅读顺序为空"):st;}
 for(int i=0;i<p.book->getTocItemsCount();++i){auto t=p.book->getTocItem(i);if(t.spineIndex>=0&&size_t(t.spineIndex)<p.meta.chapters)p.meta.toc.push_back({t.title,size_t(t.spineIndex),t.anchor});}
 out=p.meta;return {};
}
Status CrossMuxSource::resource(const std::string&href,std::vector<uint8_t>&out,size_t cap){out.clear();auto&p=*impl_;if(!p.book)return Status::fail(Error::Conflict,"EPUB未打开");size_t size=0;std::string local=localHref(p.book->getBasePath(),href);if(!p.book->getItemSize(local,&size))return Status::fail(Error::NotFound,"书内资源不存在");if(size>cap)return Status::fail(Error::TooLarge,"资源超出预算");BoundedOutput sink(out,cap);if(!p.book->readItemContentsToStream(local,sink,4096,false)||sink.overflow)return Status::fail(Error::Io,"CrossMux读取资源失败");return {};}
Status CrossMuxSource::chapter(size_t n,Chapter&out){auto&p=*impl_;if(!p.book||n>=p.meta.chapters)return Status::fail(Error::Invalid,"章节不存在");auto entry=p.book->getSpineItem(int(n));std::vector<uint8_t>bytes;auto st=resource(entry.href,bytes,768*1024);if(!st)return st;return markupToChapter(std::string(bytes.begin(),bytes.end()),entry.href,out,768*1024);}
}
