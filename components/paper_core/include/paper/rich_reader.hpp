#pragma once
#include "document.hpp"
namespace paper {
// CrossMux owns content pagination. PAPER owns resources, fonts, commands and UI.
class RichReader {
 struct Impl;
 std::unique_ptr<Impl> impl_;
public:
 RichReader(Store&,FontProvider&,DocumentSource&,const Metadata&);
 ~RichReader();
 void progress(DocumentProgress);
 Status configure(const FontSpec&,int width,int height,int lineHeight);
 Status go(const Locator&);
 Status next();
 Status previous();
 Status anchor(size_t chapter,const std::string&);
 Status find(const std::string&,std::vector<Locator>&,size_t limit);
 Status paint(Canvas&,Rect);
 std::vector<ReaderLink> links(Rect)const;
 Status prepare(size_t,const std::function<bool()>&,size_t&);
 const Locator& location()const;
 const std::string& text()const;
 const std::string& href()const;
 const std::string& warning()const;
 std::string diagnostics()const;
 int progress28()const;
};
}
