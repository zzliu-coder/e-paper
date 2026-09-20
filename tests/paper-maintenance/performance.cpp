#include "paper/runtime.hpp"
#include "paper/font_catalog.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace paper;
struct CountingFont final:FontProvider {
    int advances=0;
    int glyphRequests=0;
    bool canceled=false;
    Status glyph(uint32_t,const FontSpec&s,Glyph&g)override {++glyphRequests;g.advance64=s.px*64;g.width=g.height=1;g.coverage2={255};return {};}
    Status advance(uint32_t,const FontSpec&s,int&v)override{++advances;v=s.px*64;return {};}
    Status validate(const FontSpec&s)override{return canceled?Status::fail(Error::Canceled,"test cancel"):s.px==25?Status::fail(Error::ResourceMissing,"test absent font"):Status{};}
    std::string identity(const FontSpec&s)const override{return "fixture-"+std::to_string(s.px);}
};
struct CountingDocument final:DocumentSource {
    int loads=0;
    Status open(const std::string&,Metadata&m)override {m.identity=std::string(64,'a');m.engine="counting";m.chapters=2;return {};}
    void close()override{}
    Status chapter(size_t n,Chapter&c)override{++loads;c.text=std::string(5000,n?'b':'a');return {};}
    Status resource(const std::string&,std::vector<uint8_t>&,size_t)override{return Status::fail(Error::NotFound,"no resources");}
};
static uint64_t count(const PackedFonts& fonts,const std::string& key) {
    auto s=fonts.statistics();auto p=s.find("\""+key+"\":");assert(p!=s.npos);
    return std::stoull(s.substr(p+key.size()+3));
}
int main(int argc,char**argv) {
    assert(argc==3);
    const auto root=std::filesystem::path(argv[2]);
    // Dedicated test fixture; refuse an existing directory, never modify installed assets.
    assert(!std::filesystem::exists(root));std::filesystem::create_directories(root/"paper/fonts");
    for(const auto* name:{"misans-400-26.pgf","misans-500-32.pgf","misans-ui-500-32.pgf"})
        std::filesystem::copy_file(std::filesystem::path(argv[1])/"paper/fonts"/name,root/"paper/fonts"/name);
    ResourceGate gate;Store store(root.string(),gate);assert(store.initialize());
    HostHardware settingsHardware;Settings settings(store,settingsHardware);assert(settings.load());
    assert(settings.get("book_header")=="compact");assert(settings.apply("book_header","book"));
    assert(settings.apply("keyboard","26"));
    std::string oldCompatible;assert(store.loadRecord("settings",oldCompatible));
    assert(oldCompatible.find("book_header")==std::string::npos);
    Settings reload(store,settingsHardware);assert(reload.load());assert(reload.get("book_header")=="book");
    assert(!reload.apply("book_header","unsupported"));assert(reload.apply("book_header","compact"));
    for(int i=0;i<20;++i){assert(store.saveRecord("rotation",std::to_string(i)));std::string value;uint64_t gen=0;assert(store.loadRecord("rotation",value,&gen));assert(value==std::to_string(i)&&gen==uint64_t(i+1));}
    const char broken[]="broken";assert(store.write(".paper/records/rotation.b",broken,sizeof broken,true));
    std::string recovered;uint64_t recoveredGeneration=0;assert(store.loadRecord("rotation",recovered,&recoveredGeneration));assert(recovered=="18"&&recoveredGeneration==19);
    assert(store.saveRecord("rotation","recovered"));assert(store.loadRecord("rotation",recovered));assert(recovered=="recovered");
    assert(store.saveRecord("bad-header","preserved"));std::vector<uint8_t> badHeader;assert(store.read(".paper/records/bad-header.a",badHeader));badHeader[8]=2;
    assert(store.write(".paper/records/bad-header.a",badHeader.data(),badHeader.size(),true));assert(!store.loadRecord("bad-header",recovered));assert(!store.saveRecord("bad-header","must not replace"));
    std::vector<uint8_t> afterRefusal;assert(store.read(".paper/records/bad-header.a",afterRefusal));assert(afterRefusal==badHeader);
    assert(store.saveRecord("publish-failure","safe"));std::filesystem::create_directory(root/".paper/records/publish-failure.b");
    assert(!store.saveRecord("publish-failure","new"));assert(store.loadRecord("publish-failure",recovered));assert(recovered=="safe");
    CountingFont countingFont;auto counted=std::make_unique<CountingDocument>();auto*source=counted.get();Reader reader(store,countingFont,std::move(counted));
    ReaderStyle initial;initial.font={26,400,false};assert(reader.initializeStyles(initial));assert(reader.open("test"));
    auto firstLocation=reader.location();assert(reader.next());int warmed=countingFont.advances;
    auto prefetchLocation=reader.location();size_t prepared=99;
    assert(reader.prepareAdjacentGlyphs(24,[]{return true;},prepared));assert(prepared==0);
    const auto glyphs=countingFont.glyphRequests;
    assert(reader.prepareAdjacentGlyphs(24,[]{return false;},prepared));assert(prepared==24&&countingFont.glyphRequests==glyphs+24);
    assert(reader.location().offset==prefetchLocation.offset&&reader.location().chapter==prefetchLocation.chapter);
    assert(reader.previous());assert(countingFont.advances==warmed&&reader.location().offset==firstLocation.offset);
    auto cross=firstLocation;cross.chapter=1;assert(reader.go(cross));assert(source->loads==2);
    assert(reader.go(firstLocation));assert(source->loads==2);
    assert(!reader.layout({25,400,false},24,40));assert(reader.fontSpec().px==26&&reader.location().chapter==0);
    reader.setGrayAllowed(false);auto unsupported=reader.style();unsupported.font.gray=true;assert(!reader.applyStyle(unsupported,ReaderStyleScope::Book));
    assert(reader.close());
    // Failed cold opening must preserve the last saved locator and bookmarks.
    assert(reader.open("test"));assert(reader.next());assert(reader.bookmark());
    const auto savedPosition=reader.location();assert(reader.close());
    std::string beforeCancel,afterCancel;
    assert(store.loadRecord("book-"+std::string(64,'a'),beforeCancel));
    countingFont.canceled=true;
    assert(reader.open("test").code==Error::Canceled);assert(!reader.opened());assert(reader.close());
    assert(store.loadRecord("book-"+std::string(64,'a'),afterCancel));assert(beforeCancel==afterCancel);
    countingFont.canceled=false;assert(reader.open("test"));
    assert(reader.location().chapter==savedPosition.chapter&&reader.location().offset==savedPosition.offset);
    assert(reader.bookmarks().size()==1&&reader.bookmarks()[0].location.offset==savedPosition.offset);
    assert(reader.close());
    FontSpec a{26,400,false},b{32,500,false},ui=b;ui.uiOnly=true;
    // Standard SHA vectors and chunk boundaries; a finished context is unusable.
    for(const auto& input:{std::string(),std::string("abc"),std::string(8193,'x')}){
        Sha256Stream stream;std::string digest;
        for(size_t n=0;n<input.size();n+=37)assert(stream.add(input.data()+n,std::min<size_t>(37,input.size()-n)));
        assert(stream.finish(digest));assert(digest==sha256(input.data(),input.size()));
        assert(!stream.add("x",1));assert(!stream.finish(digest));
    }
    assert(sha256("abc",3)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string combined,independent;
    assert(validatePackedFont(store,"paper/fonts/misans-400-26.pgf",26,400,false,&combined));
    assert(store.hash("paper/fonts/misans-400-26.pgf",independent));assert(combined==independent);
    std::vector<uint8_t>checked;
    size_t lastProgress=0,totalProgress=0,calls=0;
    assert(validatePackedFont(store,"paper/fonts/misans-400-26.pgf",26,400,false,&combined,&checked,1536*1024,
        [&](size_t done,size_t total){assert(done>=lastProgress&&done<=total);lastProgress=done;totalProgress=total;++calls;return Status{};}));
    assert(combined==independent&&!checked.empty()&&calls>2&&lastProgress==totalProgress);
    assert(validatePackedFont(store,"paper/fonts/misans-400-26.pgf",26,400,false,&combined,&checked,1).code==Error::TooLarge);
    assert(checked.empty()&&combined.empty());
    // Cancel during index streaming, bitmap streaming, and the final callback.
    // No partial identity/index is published at any boundary.
    for(int boundary=0;boundary<3;++boundary){
        bool stopped=false;
        auto result=validatePackedFont(store,"paper/fonts/misans-400-26.pgf",26,400,false,&combined,&checked,1536*1024,
            [&](size_t done,size_t total){
                if((boundary==0&&done>100000)||(boundary==1&&done>total/2)||(boundary==2&&done==total)){
                    stopped=true;return Status::fail(Error::Canceled,"取消");
                }
                return Status{};
            });
        assert(stopped&&result.code==Error::Canceled&&combined.empty()&&checked.empty());
    }
    assert(validatePackedFont(store,"paper/fonts/misans-400-26.pgf",26,400,false,&combined,&checked,1536*1024,
        [](size_t,size_t){return Status::fail(Error::Canceled,"取消");}).code==Error::Canceled);
    assert(checked.empty()&&combined.empty());
    PackedFonts interrupted(store);interrupted.setProgressObserver([](size_t,size_t){return Status::fail(Error::Canceled,"取消");});
    assert(interrupted.validate(a).code==Error::Canceled);assert(count(interrupted,"validations")==0);
    interrupted.setProgressObserver({});assert(interrupted.validate(a));assert(count(interrupted,"validations")==1);
    PackedFonts fonts(store);Glyph glyph;
    for(int i=0;i<20;++i){assert(fonts.glyph(0x4e00,a,glyph));assert(fonts.glyph(0x4e00,b,glyph));assert(fonts.glyph(0x4e00,ui,glyph));}
    assert(count(fonts,"validations")==3);assert(count(fonts,"index_loads")==3);
    assert(count(fonts,"glyph_reads")==3);assert(count(fonts,"glyph_hits")==57);
    Budget bounded;bounded.fontIndex=800*1024;PackedFonts evict(store,bounded);
    assert(evict.validate(a));assert(evict.validate(b));assert(evict.validate(a));
    assert(count(evict,"validations")==2&&count(evict,"index_loads")==3);
    evict.releaseMemory();assert(evict.validate(a));assert(count(evict,"validations")==2&&count(evict,"index_loads")==4);
    store.invalidateResources();assert(evict.validate(a));assert(count(evict,"validations")==3);
    // Compare the same pixels with and without scoped file reuse. Disable glyph
    // caching so the assertion measures SD opens, not warm-cache success.
    Budget uncached;uncached.glyphCache=0;
    PackedFonts legacy(store,uncached),batched(store,uncached);
    assert(legacy.setBatchEnabled(false));
    Canvas oldFrame,newFrame;
    const std::string sample="一丁七万丈三上下";
    assert(oldFrame.text(legacy,a,sample,{0,0,480,80},40));
    assert(newFrame.text(batched,a,sample,{0,0,480,80},40));
    assert(oldFrame.bytes()==newFrame.bytes());
    PackedFonts twoPass(store,uncached);assert(twoPass.setSinglePassVerification(false));
    Canvas twoPassFrame;assert(twoPassFrame.text(twoPass,a,sample,{0,0,480,80},40));
    assert(twoPassFrame.bytes()==newFrame.bytes());assert(twoPass.identity(a)==batched.identity(a));
    assert(count(legacy,"glyph_reads")==8&&count(legacy,"glyph_opens")==8);
    assert(count(batched,"glyph_reads")==8&&count(batched,"glyph_opens")==1);
    {FontReadBatch outer(batched);assert(outer.status());
        Lease denied;assert(gate.acquire("usb","batch-test",denied).code==Error::Busy);
        assert(!batched.setBatchEnabled(false));
        assert(newFrame.text(batched,a,sample,{0,0,480,80},40));}
    // Error return and nested scopes must both close handles and release leases.
    assert(!newFrame.text(batched,a,encodeUtf8(0x10ffff),{0,0,480,80},40));
    Lease returned;assert(gate.acquire("usb","batch-test",returned));gate.release(returned);
    std::vector<uint8_t>bytes;assert(store.read("paper/fonts/misans-400-26.pgf",bytes,8*1024*1024));
    bytes.back()^=1;assert(store.write("paper/fonts/misans-400-26.pgf",bytes.data(),bytes.size(),true));
    combined="must clear";assert(!validatePackedFont(store,"paper/fonts/misans-400-26.pgf",26,400,false,&combined));assert(combined.empty());
    assert(!fonts.validate(a)); // Cached identity must never mask an in-process replacement.
    assert(!fonts.revalidate(a));assert(fonts.validate(ui));
    const std::string book="第一章\n测试书籍。Hello reader.\n";
    assert(store.write("books/test.txt",book.data(),book.size()));
    LocalDocument doc(store);Metadata first,second;Chapter ch;
    assert(doc.open("books/test.txt",first));assert(doc.chapter(0,ch));doc.close();
    assert(!doc.chapter(0,ch));assert(doc.open("books/test.txt",second));assert(first.identity==second.identity);
    assert(store.write("books/test.txt",book.data(),book.size(),true).code==Error::Busy);
    assert(store.renameFile("books/test.txt","books/renamed.txt").code==Error::Busy);
    doc.close();const auto changed=book+"修改内容。";
    assert(store.write("books/test.txt",changed.data(),changed.size(),true));
    assert(doc.open("books/test.txt",second));assert(first.identity!=second.identity);doc.close();
    Lease usb;assert(gate.acquire("usb","test",usb));assert(!fonts.validate(b));gate.release(usb);
    std::cout<<"PASS: split index pools, hash LRU hits, validation survives eviction, media invalidation, corrupt replacement rejected, closed book lease released, book identity changes.\n";
}
