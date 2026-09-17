#pragma once
#include <memory>
#include <string>
// Official EPUB parser, kept separate from the portable CrossMux TXT adapter.
class InkDeskEpub {
 struct Impl;
 std::unique_ptr<Impl> impl_;
 public:
 InkDeskEpub();
 ~InkDeskEpub();
 bool Open(const char* path);
 void Close();
 int Chapters() const;
 const std::string& Title() const;
 bool Chapter(int index,std::string& text);
};
