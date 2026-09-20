#include "paper/runtime.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <zlib.h>
using namespace paper;
namespace fs=std::filesystem;
int main(int argc,char**argv){
    assert(argc==4&&!fs::exists(argv[2]));
    fs::create_directories(fs::path(argv[2])/"paper");
    fs::create_directories(fs::path(argv[2])/"books");
    // Fixtures only: immutable font/IME files are hard-linked; never modify them.
    for(auto name:{"fonts","ime"}){
        auto source=fs::path(argv[1])/"paper"/name,dest=fs::path(argv[2])/"paper"/name;fs::create_directories(dest);
        for(auto&e:fs::recursive_directory_iterator(source)){
            auto to=dest/fs::relative(e.path(),source);
            if(e.is_directory())fs::create_directories(to);else if(e.is_regular_file())fs::create_hard_link(e.path(),to);
        }
    }
    {std::ofstream f(fs::path(argv[2])/"books/recent.txt");f<<"清晨，窗边的光。Hello world.\n";}
    fs::copy_file(argv[3],fs::path(argv[2])/"books/content.epub");
    HostHardware hw;
    {Runtime app(argv[2],hw);assert(app.initialize());assert(app.action("library"));assert(app.action("open","0"));assert(app.action("home"));}
    {Runtime app(argv[2],hw);assert(app.initialize());assert(app.action("continue"));assert(app.snapshot().find("\"open\":true")!=std::string::npos);assert(app.action("home"));}
    ResourceGate gate;Store store(argv[2],gate);assert(store.initialize());
    {assert(store.saveRecord("recent-book","../../escape"));Runtime app(argv[2],hw);assert(app.initialize());assert(!app.action("continue"));}
    {LocalDocument doc(store);Metadata m;doc.setProgressObserver([](const char*,size_t,size_t){return Status::fail(Error::Canceled,"test");});assert(doc.open("books/recent.txt",m).code==Error::Canceled);assert(gate.active().empty());doc.setProgressObserver({});assert(doc.open("books/recent.txt",m));doc.close();}
    for(const auto* stage:{"archive_index","resource_read","decompress","parse","metadata"})for(int cancelAfter:{1,3}){
        LocalDocument doc(store);Metadata m;bool canceled=false;Chapter chapter;int callbacks=0;
        doc.setProgressObserver([&](const char*s,size_t,size_t){if(std::string(s)==stage&&++callbacks==cancelAfter){canceled=true;return Status::fail(Error::Canceled,"test");}return Status{};});
        auto st=doc.open("books/content.epub",m);if(st)st=doc.chapter(0,chapter);
        assert(canceled&&st.code==Error::Canceled);doc.close();assert(gate.active().empty());
        doc.setProgressObserver({});assert(doc.open("books/content.epub",m));assert(doc.chapter(0,chapter));assert(!chapter.images.empty());doc.close();
    }
    {std::ofstream f(fs::path(argv[2])/"books/large.txt");f<<std::string(200000,'x');}
    {LocalDocument doc(store);Metadata m;doc.setProgressObserver([](const char*stage,size_t,size_t){return std::string(stage)=="txt_index"?Status::fail(Error::Canceled,"index canceled"):Status{};});
     assert(doc.open("books/large.txt",m).code==Error::Canceled);assert(gate.active().empty());
     doc.setProgressObserver({});assert(doc.open("books/large.txt",m));assert(m.chapters==4);doc.close();}
    std::string input(100000,'x');std::vector<uint8_t> packed(200000),decoded;
    z_stream z{};assert(deflateInit2(&z,Z_DEFAULT_COMPRESSION,Z_DEFLATED,-MAX_WBITS,8,Z_DEFAULT_STRATEGY)==Z_OK);
    z.next_in=(Bytef*)input.data();z.avail_in=input.size();z.next_out=packed.data();z.avail_out=packed.size();assert(deflate(&z,Z_FINISH)==Z_STREAM_END);packed.resize(z.total_out);deflateEnd(&z);
    size_t calls=0;
    assert(inflateRaw(packed,decoded,input.size(),200000,[&](const char*,size_t,size_t){++calls;return Status::fail(Error::Canceled,"test");}).code==Error::Canceled);assert(decoded.empty()&&calls==1);
    assert(inflateRaw(packed,decoded,input.size(),200000));assert(std::string(decoded.begin(),decoded.end())==input);
    calls=0;assert(inflateRaw(packed,decoded,input.size(),200000,[&](const char*,size_t done,size_t){if(++calls==3){assert(done>16384);return Status::fail(Error::Canceled,"mid-stream");}return Status{};}).code==Error::Canceled);assert(decoded.empty());
    assert(!inflateRaw(packed,decoded,input.size()-1,200000));
    Chapter c;
    assert(markupToChapter("<p style='text-indent:2em'>中文<strong>粗体</strong><em>斜体</em><del>删除</del></p>","x",c,4096));
    assert(c.text=="中文粗体斜体删除\n"&&c.styles.size()==4);
    assert(c.styles[0].indentEm==2&&c.styles[1].bold&&c.styles[2].italic&&c.styles[3].strike);
    assert(markupToChapter("<strong style='font-weight:normal'>A</strong><em style='font-style:normal'>B</em>","x",c,4096));assert(c.styles.empty());
    assert(markupToChapter("<p>cancel</p>","x",c,4096,"",[](const char*,size_t,size_t){return Status::fail(Error::Canceled,"test");}).code==Error::Canceled);
    PackedFonts fonts(store);FontSpec ui{22,500,false};ui.uiOnly=true;FontSpec book{26,400,false};
    assert(fonts.validate(ui)&&fonts.validate(book));auto before=fonts.statistics();assert(fonts.revalidate(book));auto after=fonts.statistics();
    assert(fonts.validate(ui));assert(fonts.statistics().find("\"validations\":3")!=std::string::npos);
    std::cout<<"PASS: restart continue, invalid recent path, hash/decompress/parse cancellation, emphasis/indent parsing, targeted font revalidation preserves UI\n";
}
