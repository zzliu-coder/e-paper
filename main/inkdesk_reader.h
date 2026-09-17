#pragma once
#include "inkdesk_r2/core/types.h"
#include "crossmux_txt/TxtEncoding.h"
#include "inkdesk_epub.h"
#include "cJSON.h"
#include <string>
#include <vector>
// Bounded POSIX/LVGL adapter for CrossMux portable TXT modules, not its Activity runtime.
class InkDeskReader {
 struct Book { std::string name,path; };
 std::vector<Book> books_;
 std::vector<size_t> offsets_;
 std::vector<int> chapters_;
 InkDeskEpub epub_;
 std::string chapterText_;
 int loadedChapter_=-1,nextChapter_=0;
 bool isEpub_=false,loadOk_=false;
 std::array<uint8_t,8192> bytes_{};
 std::array<inkdesk::Text<96>,15> lines_{};
 inkdesk::Text<96> message_;
 size_t fileSize_=0,nextOffset_=0,page_=0,listPage_=0;
 int selected_=-1;
 txt_encoding::Encoding encoding_=txt_encoding::Encoding::Utf8;
 bool eof_=false;
 void Scan(const char* dir);
 bool Load();
 bool LoadEpub();
 public:
 InkDeskReader() { books_.reserve(32); offsets_.reserve(4096); chapters_.reserve(4096); }
 void Library();
 bool Tap(int x,int y); // false means return to InkDesk home
 void Next();
 void Previous();
 void Draw(inkdesk::Frame& frame);
 void Status(cJSON* reply) const;
};
