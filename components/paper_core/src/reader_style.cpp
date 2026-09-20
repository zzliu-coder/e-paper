#include "paper/reader_style.hpp"
#include "paper/font_catalog.hpp"
#include <iomanip>
#include <sstream>
namespace paper {
namespace {
void put(std::ostream&o,const ReaderStyle&s){o<<std::quoted(s.font.family)<<' '<<std::quoted(s.font.revision)<<' '<<s.font.px<<' '<<s.font.weight<<' '<<int(s.font.gray)<<' '<<int(s.font.allowFallback)<<' '<<s.margin<<' '<<s.lineHeight<<'\n';
}
bool get(std::istream&i,ReaderStyle&s){int gray=0,fallback=0;
if(!(i>>std::quoted(s.font.family)>>std::quoted(s.font.revision)>>s.font.px>>s.font.weight>>gray>>fallback>>s.margin>>s.lineHeight)||gray<0||gray>1||fallback<0||fallback>1)return false;
s.font.gray=gray;
s.font.allowFallback=fallback;
return validReaderStyle(s);
}
}
bool validReaderStyle(const ReaderStyle&s){
 return (s.font.family=="misans"||validFontId(s.font.family))&&
    (s.font.family=="misans"?s.font.revision.empty():validFontRevision(s.font.revision))&&
    s.font.px>=16&&s.font.px<=40&&(s.font.weight==400||s.font.weight==500||s.font.weight==700)&&
    s.margin>=16&&s.margin<=48&&s.lineHeight>=s.font.px+4&&s.lineHeight<=64;
}
Status ReaderStyleStore::load(const ReaderStyle&legacy){
    std::string payload;
    auto st=store_.loadRecord("reader-styles-v1",payload);
    if(st.code==Error::NotFound){defaults_=legacy;
    overrides_.clear();
    return {};
    }
    if(!st)return st;
    ReaderStyle def;
    std::map<std::string,ReaderStyle>records;
    std::istringstream in(payload);
    std::string magic,id,tail;
    size_t count=0;
    if(!(in>>magic)||magic!="PAPERSTYLE1"||!get(in,def)||!(in>>count)||count>256)return Status::fail(Error::Corrupt,"阅读字体配置损坏；原记录保留");
    for(size_t n=0;n<count;++n){ReaderStyle value;
    if(!(in>>id)||!validFontRevision(id)||!get(in,value)||records.count(id))return Status::fail(Error::Corrupt,"单本书字体配置无效");
    records.emplace(id,value);
    }
    if(in>>tail)return Status::fail(Error::Corrupt,"阅读配置含未知内容");
    defaults_=std::move(def);
    overrides_=std::move(records);
    return {};
}
ReaderStyle ReaderStyleStore::effective(const std::string&book)const{auto it=overrides_.find(book);
return it==overrides_.end()?defaults_:it->second;
}
Status ReaderStyleStore::commit(const std::string&book,const ReaderStyle&s,ReaderStyleScope scope){
    if((!validFontRevision(book) && !(book.empty() && scope==ReaderStyleScope::Default))||!validReaderStyle(s))return Status::fail(Error::Invalid,"阅读配置或书籍身份无效");
    auto next=overrides_;
    auto def=defaults_;
    if(scope==ReaderStyleScope::Book){if(!next.count(book)&&next.size()>=256)return Status::fail(Error::TooLarge,"独立字体设置已达256本上限");
    next[book]=s;
    }
    else if(scope==ReaderStyleScope::Default){def=s;
    next.erase(book);
    }
    else if(scope==ReaderStyleScope::FollowDefault)next.erase(book);
    else return Status::fail(Error::Invalid,"字体应用范围无效");
    std::ostringstream out;
    out<<"PAPERSTYLE1\n";
    put(out,def);
    out<<next.size()<<'\n';
    for(auto&entry:next){out<<entry.first<<' ';
    put(out,entry.second);
    }
    auto st=store_.saveRecord("reader-styles-v1",out.str());
    if(!st)return st;
    defaults_=std::move(def);
    overrides_=std::move(next);
    return {};
}
}
