#include "inkdesk_epub.h"
#include "reader/epub_document.h"
struct InkDeskEpub::Impl { reader::EpubDocument doc; };
InkDeskEpub::InkDeskEpub():impl_(new Impl){}
InkDeskEpub::~InkDeskEpub()=default;
bool InkDeskEpub::Open(const char* path){
 if(!impl_->doc.Open(path))return false;
 size_t bytes=0;
 // No DRM bypass, including conservative rejection of encrypted/obfuscated resources.
 if(impl_->doc.GetItemUncompressedSize("META-INF/encryption.xml",&bytes)){Close();return false;}
 return true;
}
void InkDeskEpub::Close(){impl_->doc.Close();}
int InkDeskEpub::Chapters()const{return impl_->doc.SpineCount();}
const std::string& InkDeskEpub::Title()const{return impl_->doc.Title();}
bool InkDeskEpub::Chapter(int index,std::string& text){
 text.clear(); std::vector<reader::ContentBlock> blocks;
 if(!impl_->doc.LoadChapterBlocks(index,blocks,512*1024))return false;
 for(const auto& block:blocks){
   const std::string& s=block.kind==reader::ContentKind::kText?block.text:std::string("[插图：基础阅读版暂不显示]");
   if(text.size()+s.size()+2>512*1024){text.clear();return false;}
   text+=s; text+="\n\n";
 }
 if(text.empty())text="[空章节]\n";
 return true;
}
