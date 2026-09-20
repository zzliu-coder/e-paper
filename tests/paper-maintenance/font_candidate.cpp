#include "paper/font_catalog.hpp"
#include "paper/text.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace paper;
int main(int argc,char**argv){
    assert(argc==3);assert(!std::filesystem::exists(argv[2]));
    std::filesystem::create_directories(std::string(argv[2])+"/paper/font-inbox");
    std::filesystem::copy_file(argv[1],std::string(argv[2])+"/paper/font-inbox/candidate.pfr");
    ResourceGate gate;Store store(argv[2],gate);assert(store.initialize());
    ReaderFontCatalog catalog(store,{});ReaderFontFamily family;
    catalog.setProgressObserver([](size_t done,size_t){return done>100000?Status::fail(Error::Canceled,"test cancellation"):Status{};});
    assert(catalog.install("paper/font-inbox/candidate.pfr",family).code==Error::Canceled);
    assert(family.id.empty());ReaderFontFamily unpublished;assert(!catalog.resolve("misans-ma","",unpublished));
    size_t previous=0,expected=0;
    catalog.setProgressObserver([&](size_t done,size_t total){assert(done>=previous&&done<=total);previous=done;expected=total;return Status{};});
    auto st=catalog.install("paper/font-inbox/candidate.pfr",family);
    if(!st){std::cerr<<st.message<<'\n';return 1;}
    assert(previous==expected&&expected>0);
    assert(family.id=="misans-ma"&&family.faces.size()==8);
    PackedFonts fonts(store,{});
    for(auto&face:family.faces){
        FontSpec spec{face.px,face.weight,false};spec.family=family.id;spec.revision=family.revision;
        assert(fonts.validate(spec));
        for(auto cp:{uint32_t('A'),uint32_t('g'),uint32_t(0x6e05),uint32_t(0x56fd)}){
            Glyph g;assert(fonts.glyph(cp,spec,g));assert(g.advance64>0);
            for(auto byte:g.coverage2)for(int shift:{0,2,4,6}){int level=(byte>>shift)&3;assert(level==0||level==3);}
        }
        fonts.clear();
    }
    ReaderFontFamily resolved;assert(catalog.resolve(family.id,"",resolved));assert(resolved.revision==family.revision);
    assert(gate.active().empty());
    std::cout<<"PASS: PFR CRC/SHA/PGF installation, 8 exact M-A faces, Chinese/Latin monochrome glyphs, atomic catalog publication\n";
}
