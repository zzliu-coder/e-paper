#pragma once
#include "text.hpp"

namespace paper {
// Native reader resource pack. System MiSans remains in paper/fonts/misans-*.
// Pack publication is append-only; one atomic Store record selects the revision.
struct FontFaceInfo {
    int px=0,weight=0;
    uint32_t bytes=0;
    std::string sha256;
};
struct ReaderFontFamily {
    std::string id,name,revision,error;
    std::vector<FontFaceInfo> faces;
    bool available=false;
    bool has(int px,int weight)const;
};
bool validFontId(const std::string&);
bool validFontRevision(const std::string&);
Status decodeFontManifest(const std::string&,ReaderFontFamily&);
std::string encodeFontManifest(const ReaderFontFamily&);
std::string readerFacePath(const std::string&id,const std::string&revision,int px,int weight);
Status resolveReaderFace(Store&,const FontSpec&,std::string&path,std::string&digest);
// Full PGF header, index, bitmap CRC, geometry and size checks; bounded I/O.
Status validatePackedFont(Store&,const std::string&,int px,int weight,bool allowTestAssets,std::string* digest=nullptr,
                         std::vector<uint8_t>* validatedIndex=nullptr,size_t indexBudget=0,
                         const std::function<Status(size_t,size_t)>& progress={});

class ReaderFontCatalog {
    Store& store_;
    Budget budget_;
    std::function<Status(size_t,size_t)> progress_;
public:
    ReaderFontCatalog(Store&s,Budget b={}):store_(s),budget_(b){}
    void setProgressObserver(std::function<Status(size_t,size_t)> callback){progress_=std::move(callback);}
    Status list(std::vector<ReaderFontFamily>&);
    Status resolve(const std::string&id,const std::string&revision,ReaderFontFamily&);
    // Only reads .pfr files from paper/font-inbox/. Does not modify UI resources.
    // Failed/incomplete imports cannot become a selected family.
    Status install(const std::string&inboxRelative,ReaderFontFamily&receipt);
};
}
