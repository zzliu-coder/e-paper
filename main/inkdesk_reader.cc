#include "inkdesk_reader.h"
#include "crossmux_txt/TxtParagraph.h"
#include "crossmux_txt/TxtPageIndex.h"
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using namespace inkdesk;
#ifndef INKDESK_SD_ROOT
#define INKDESK_SD_ROOT "/sdcard"
#endif
namespace {
std::string Short(const std::string& s,size_t n=75) {
 size_t at=0;
 while(at<s.size()) { uint32_t cp=0; size_t z=utf8Next(s.data()+at,s.size()-at,cp); if(!z || at+z>n) break; at+=z; }
 return s.substr(0,at);
}
}
void InkDeskReader::Scan(const char* dir) {
 DIR* d=opendir(dir); if(!d) return;
 dirent* entry;
 while(books_.size()<32 && (entry=readdir(d))) {
    std::string name=entry->d_name;
    bool txt=name.size()>=4&&!strcasecmp(name.c_str()+name.size()-4,".txt");
    bool epub=name.size()>=5&&!strcasecmp(name.c_str()+name.size()-5,".epub");
    if(!txt&&!epub) continue;
    std::string path=std::string(dir)+"/"+name; struct stat st{};
    if(stat(path.c_str(),&st)!=0 || !S_ISREG(st.st_mode) || st.st_size>64*1024*1024) continue;
    books_.push_back({name,path});
 }
 closedir(d);
}
void InkDeskReader::Library() {
 epub_.Close(); chapterText_.clear(); loadedChapter_=-1; isEpub_=false; loadOk_=false;
 mkdir(INKDESK_SD_ROOT "/inkdesk",0755);
 // Private example only; exclusive creation never overwrites an existing file.
 int fd=open(INKDESK_SD_ROOT "/inkdesk/SDK-reader-demo.txt",O_WRONLY|O_CREAT|O_EXCL,0600);
 if(fd>=0) {
    FILE* f=fdopen(fd,"wb");
    if(f) {
        for(int i=1;i<=40;++i) fprintf(f,"第%d段：这是纸间与 CrossMux TXT 模块的本地阅读示例。中文、English 和数字 12345。上一页、下一页都通过保留的开发底座接入。\n\n",i);
        fclose(f);
    } else close(fd);
 }
 selected_=-1; listPage_=0; message_.clear(); books_.clear();
 Scan(INKDESK_SD_ROOT "/books"); Scan(INKDESK_SD_ROOT); Scan(INKDESK_SD_ROOT "/inkdesk");
 if(books_.empty()) message_.assign("请传入 TXT / EPUB，再重新进入");
}
bool InkDeskReader::LoadEpub() {
 const int chapter=chapters_[page_];
 if(loadedChapter_!=chapter) {
   if(!epub_.Chapter(chapter,chapterText_)){loadedChapter_=-1;message_.assign("章节读取失败或超过512KB限制");return false;}
   loadedChapter_=chapter;
 }
 size_t at=offsets_[page_],row=0; int width=0;
 while(at<chapterText_.size()&&row<lines_.size()) {
   if(chapterText_[at]=='\r'){++at;continue;}
   if(chapterText_[at]=='\n'){++at;++row;width=0;continue;}
   uint32_t cp=0; size_t z=utf8Next(chapterText_.data()+at,chapterText_.size()-at,cp);
   bool invalid=!z; if(invalid){z=1;cp='?';}
   int advance=cp<128?14:25;
   if(width+advance>432){++row;width=0;if(row==lines_.size())break;}
   if(invalid)lines_[row].append("?"); else lines_[row].append(chapterText_.data()+at,z);
   width+=advance;at+=z;
 }
 nextChapter_=chapter; nextOffset_=at;
 eof_=at>=chapterText_.size() && chapter+1>=epub_.Chapters();
 if(at>=chapterText_.size()&&!eof_){nextChapter_=chapter+1;nextOffset_=0;}
 return true;
}
bool InkDeskReader::Load() {
 for(auto& line:lines_) line.clear();
 message_.clear();
 if(selected_<0 || size_t(selected_)>=books_.size() || page_>=offsets_.size()) return false;
 if(isEpub_) return LoadEpub();
 FILE* f=fopen(books_[selected_].path.c_str(),"rb");
 if(!f) { message_.assign("文件打开失败"); return false; }
 if(fseek(f,0,SEEK_END)!=0) { fclose(f); return false; }
 long length=ftell(f);
 if(length<0 || size_t(length)!=fileSize_) { fclose(f); message_.assign("文件已变化，请回书库重新打开"); return false; }
 size_t start=offsets_[page_];
 if(fseek(f,start,SEEK_SET)!=0) { fclose(f); return false; }
 size_t raw=fread(bytes_.data(),1,2048,f); bool failed=ferror(f); fclose(f);
 if(failed) { message_.assign("读取失败"); return false; }
 size_t n=raw;
 if(encoding_==txt_encoding::Encoding::Gbk) {
    auto result=txt_encoding::transcodeGbkInPlace(bytes_.data(),raw,bytes_.size(),start+raw>=fileSize_);
    n=result.utf8Length;
 }
 size_t at=0,row=0; int width=0; bool lineStart=true;
 txt_paragraph::State paragraphs(start==0 || start==3);
 while(at<n && row<lines_.size()) {
    if(lineStart) {
        size_t end=at; while(end<n && bytes_[end]!='\n') ++end;
        auto info=txt_paragraph::analyzeLine(bytes_.data()+at,end-at);
        if(info.kind==txt_paragraph::LineKind::Blank) {
            paragraphs.noteBlankLine(); at=end+(end<n); ++row; width=0; continue;
        }
        if(paragraphs.consume(info.kind)) { lines_[row].append("　"); width=25; }
        at+=info.contentOffset; lineStart=false;
        if(at>=n) break;
    }
    if(bytes_[at]=='\r') { ++at; continue; }
    if(bytes_[at]=='\n') { ++at; ++row; width=0; lineStart=true; continue; }
    uint32_t cp=0; size_t z=utf8Next(reinterpret_cast<char*>(bytes_.data()+at),n-at,cp);
    if(!z) {
        if(n-at<4 && start+raw<fileSize_) break;
        cp='?'; z=1;
    }
    int advance=cp<128 ? 14 : 25;
    if(width+advance>432) { ++row; width=0; if(row==lines_.size()) break; }
    if(cp=='?' && bytes_[at]!='?') lines_[row].append("?");
    else lines_[row].append(reinterpret_cast<char*>(bytes_.data()+at),z);
    width+=advance; at+=z;
 }
 const size_t used=encoding_==txt_encoding::Encoding::Gbk ? txt_encoding::gbkSourceLength(bytes_.data(),at) : at;
 nextOffset_=std::min(fileSize_,start+used); eof_=nextOffset_>=fileSize_;
 if(!used && !eof_) { message_.assign("无法解析文本，未前进"); return false; }
 return true;
}
void InkDeskReader::Next() {
 if(selected_<0) { if((listPage_+1)*7<books_.size()) ++listPage_; return; }
 if(eof_||!loadOk_) return;
 if(page_+1==offsets_.size()) {
    if(offsets_.size()>=4096) { message_.assign("达到本次4096页索引上限"); return; }
    if(isEpub_){offsets_.push_back(nextOffset_);chapters_.push_back(nextChapter_);}
    else if(txt_page_index::recordNextOffset(offsets_,fileSize_,nextOffset_)!=txt_page_index::AdvanceResult::PageAdded) return;
 }
 ++page_; loadOk_=Load();
}
void InkDeskReader::Previous() {
 if(selected_<0) { if(listPage_) --listPage_; return; }
 if(page_) { --page_; loadOk_=Load(); }
}
bool InkDeskReader::Tap(int x,int y) {
 if(y>=710 && y<770) {
    if(x<160) Previous(); else if(x>=320) Next();
    else if(selected_>=0) { selected_=-1; epub_.Close(); chapterText_.clear(); message_.clear(); } else return false;
    return true;
 }
 if(selected_<0 && x>=24 && x<456 && y>=140 && y<140+7*72) {
    size_t i=listPage_*7+(y-140)/72;
    if(i>=books_.size()) return true;
    selected_=i; page_=0; offsets_.clear(); chapters_.clear(); fileSize_=0; nextOffset_=0; eof_=false; loadOk_=false;
    epub_.Close(); chapterText_.clear(); loadedChapter_=-1;
    for(auto& line:lines_)line.clear();
    const auto& path=books_[i].path;
    isEpub_=path.size()>=5&&!strcasecmp(path.c_str()+path.size()-5,".epub");
    if(isEpub_){
      struct stat st{}; if(!stat(path.c_str(),&st))fileSize_=st.st_size;
      if(!epub_.Open(path.c_str())){message_.assign("EPUB 无法打开：损坏或加密资源");return true;}
      offsets_.push_back(0);chapters_.push_back(0);loadOk_=Load();return true;
    }
    FILE* f=fopen(books_[i].path.c_str(),"rb");
    if(!f) { message_.assign("文件不可读"); return true; }
    fseek(f,0,SEEK_END); long length=ftell(f); rewind(f);
    if(length<0) { fclose(f); return true; }
    fileSize_=length; size_t n=fread(bytes_.data(),1,2048,f); fclose(f);
    encoding_=txt_encoding::detect(bytes_.data(),n,n>=fileSize_);
    if(encoding_==txt_encoding::Encoding::Unknown) encoding_=txt_encoding::Encoding::Utf8;
    offsets_.push_back(n>=3 && bytes_[0]==0xef && bytes_[1]==0xbb && bytes_[2]==0xbf ? 3 : 0);
    loadOk_=Load();
 }
 return true;
}
void InkDeskReader::Draw(Frame& f) {
 f.reset(); f.text(24,20,"INKDESK · TXT / EPUB",24); f.line(62);
 if(selected_<0) {
    f.text(24,85,"本地书库 · TXT / EPUB",28);
    for(size_t i=listPage_*7;i<books_.size() && i<(listPage_+1)*7;++i)
        f.button({24,140+int(i%7)*72,432,60},Short(books_[i].name,60).c_str(),Action::None,0,false,24);
 } else {
    f.text(24,85,Short(isEpub_?epub_.Title():books_[selected_].name,48).c_str(),24);
    for(size_t i=0;i<lines_.size();++i) f.text(24,142+i*35,lines_[i].c_str(),24);
    char info[96];
    if(isEpub_)snprintf(info,sizeof(info),"第 %u 页 · 章节 %d/%d%s",unsigned(page_+1),page_<chapters_.size()?chapters_[page_]+1:0,epub_.Chapters(),eof_?" · 完":"");
    else snprintf(info,sizeof(info),"第 %u 页 · %d%% · %s%s",unsigned(page_+1),txt_page_index::progressPercent(nextOffset_,fileSize_),
        encoding_==txt_encoding::Encoding::Gbk ? "GBK" : "UTF-8",eof_ ? " · 完" : "");
    f.text(24,677,info,18);
 }
 f.button({24,714,132,52},"上一页",Action::None,0,false,20);
 f.button({174,714,132,52},selected_<0 ? "回首页" : "回书库",Action::None,0,false,20);
 f.button({324,714,132,52},"下一页",Action::None,0,false,20);
 if(!message_.empty()) f.text(10,775,message_.c_str(),16);
}
void InkDeskReader::Status(cJSON* p) const {
 cJSON_AddStringToObject(p,"engine",isEpub_?"Official EPUB parser / SDK text layout":"CrossMux portable TXT modules");
 cJSON_AddStringToObject(p,"format",isEpub_?"EPUB":"TXT");
 cJSON_AddStringToObject(p,"title",isEpub_?epub_.Title().c_str():selected_>=0?books_[selected_].name.c_str():"");
 cJSON_AddBoolToObject(p,"loaded",loadOk_);
 cJSON_AddNumberToObject(p,"chapter",isEpub_&&page_<chapters_.size()?chapters_[page_]+1:0);
 cJSON_AddNumberToObject(p,"chapters",isEpub_?epub_.Chapters():0);
 cJSON_AddNumberToObject(p,"text_bytes",isEpub_?chapterText_.size():0);
 cJSON_AddNumberToObject(p,"books",books_.size());
 cJSON_AddNumberToObject(p,"page",page_+1);
 cJSON_AddNumberToObject(p,"offset",page_<offsets_.size() ? offsets_[page_] : 0);
 cJSON_AddNumberToObject(p,"next_offset",nextOffset_);
 cJSON_AddNumberToObject(p,"bytes",fileSize_);
 cJSON_AddBoolToObject(p,"eof",eof_);
 cJSON_AddBoolToObject(p,"library",selected_<0);
 cJSON_AddStringToObject(p,"error",message_.c_str());
}
