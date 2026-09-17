#pragma once
#include "types.h"
namespace inkdesk {
  class Pinyin {
    Text<32> raw_;
    uint32_t revision_=1;
    size_t page_=0;
    public:
    static constexpr size_t kPageSize=4;
    bool letter(char c);
    bool backspace();
    void clear();
    void nextPage();
    const char*raw()const {
      return raw_.c_str();
    }bool empty()const {
      return raw_.empty();
    }
    uint32_t revision()const {
      return revision_;
    }size_t page()const {
      return page_;
    }
    size_t count()const;
    bool candidate(size_t visibleIndex,Text<48>&out)const;
    static size_t dictionarySize();
  };
}
