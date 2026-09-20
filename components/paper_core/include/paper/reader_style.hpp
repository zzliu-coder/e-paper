#pragma once
#include "text.hpp"
namespace paper {
struct ReaderStyle {
    FontSpec font{26,400,false};
    int margin=24,lineHeight=40;
};
enum class ReaderStyleScope { Book, Default, FollowDefault };
bool validReaderStyle(const ReaderStyle&);
// One record makes default+current override updates atomic together.
// Position/bookmark records are deliberately separate and remain untouched.
class ReaderStyleStore {
    Store& store_;
    ReaderStyle defaults_;
    std::map<std::string,ReaderStyle> overrides_;
public:
    explicit ReaderStyleStore(Store&s):store_(s){}
    Status load(const ReaderStyle&legacyDefault);
    ReaderStyle effective(const std::string&book)const;
    const ReaderStyle& defaults()const{return defaults_;
    }
    bool hasOverride(const std::string&book)const{return overrides_.count(book)!=0;
    }
    Status commit(const std::string&book,const ReaderStyle&,ReaderStyleScope);
};
}
