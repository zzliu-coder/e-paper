#include "paper/document.hpp"
#include "paper/image.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace paper;
const std::vector<uint8_t> png={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,2,8,6,0,0,0,114,182,13,36,0,0,0,18,73,68,65,84,120,156,99,96,96,96,248,15,2,12,80,240,31,0,60,213,5,251,109,11,168,27,0,0,0,0,73,69,78,68,174,66,96,130};
struct Font:FontProvider {
    bool tall=false;
    Status glyph(uint32_t,const FontSpec&s,Glyph&g)override{g=tall?Glyph{s.px*64,3,12,0,12,std::vector<uint8_t>(9,255)}:Glyph{s.px*64,1,1,0,0,{255}};return {};}
    std::string identity(const FontSpec&)const override{return "fixture";}
};
struct Source:DocumentSource {
    Status open(const std::string&,Metadata&m)override{m.identity=std::string(64,'b');m.engine="fixture";m.chapters=1;return {};}
    Status chapter(size_t,Chapter&c)override{return markupToChapter("<p>before</p><img src='a.png' alt='test'/><p>after</p>","OPS/ch.xhtml",c,4096);}
    Status resource(const std::string&path,std::vector<uint8_t>&v,size_t)override{assert(path=="OPS/a.png");v=png;return {};}
    void close()override{}
};
struct StyledSource:DocumentSource {
    std::string markup;
    explicit StyledSource(std::string s):markup(std::move(s)){}
    Status open(const std::string&,Metadata&m)override{m.identity=sha256(markup.data(),markup.size());m.engine="style-fixture";m.chapters=1;return {};}
    Status chapter(size_t,Chapter&c)override{return markupToChapter(markup,"c.xhtml",c,4096);}
    Status resource(const std::string&,std::vector<uint8_t>&,size_t)override{return Status::fail(Error::NotFound,"test");}
    void close()override{}
};
int main(int argc,char**argv){
    assert(argc==2||argc==3);assert(!std::filesystem::exists(argv[1]));
    std::filesystem::create_directories(argv[1]);
    ResourceGate gate;Store store(argv[1],gate);assert(store.initialize());
    Chapter c;
    assert(markupToChapter("<h1 class='center'>Title</h1><p id='p'>text <a href='#p'>link</a></p><img src='../img/a.png' alt='A'/>","OPS/ch.xhtml",c,4096,".center {text-align:center;}"));
    assert(c.text=="Title\ntext link\n[图片：A]\n");
    assert(c.styles.size()==1&&c.styles[0].heading&&c.styles[0].align==1);
    assert(c.anchors.at("p")==6&&c.links.size()==1);
    assert(c.images.size()==1&&c.images[0].href=="img/a.png");
    assert(!markupToChapter("abc","ch",c,2));
    assert(markupToChapter("<img src='../../escape'/>","ch",c,4096));assert(c.images.empty());
    Canvas canvas(8,8);assert(paintBookImage(png,canvas,{0,0,8,8}));
    assert(canvas.pixel(3,3)==0&&canvas.pixel(4,3)==3&&canvas.pixel(3,4)==3&&canvas.pixel(4,4)==0);
    assert(!paintBookImage({},canvas,{0,0,8,8}));
    auto truncated=png;truncated.resize(12);assert(!paintBookImage(truncated,canvas,{0,0,8,8}));
    auto brokenData=png;brokenData[43]=0xff;
    for(int i=0;i<20;++i){assert(!paintBookImage(brokenData,canvas,{0,0,8,8}));assert(paintBookImage(png,canvas,{0,0,8,8}));}
    // A rejected image must release all allocation budget for the next decode.
    assert(paintBookImage(png,canvas,{0,0,8,8}));
    Font font;Reader reader(store,font,std::make_unique<Source>());assert(reader.open("fixture"));
    uint32_t plainHash=0;
    for(const auto& markup:{std::string("<p>A</p>"),std::string("<p><strong>A</strong></p>"),std::string("<p><del>A</del></p>"),std::string("<p style='text-indent:2em'>A</p>")}){
        Reader styled(store,font,std::make_unique<StyledSource>(markup));assert(styled.open("fixture"));Canvas image;
        assert(styled.paint(image,{24,100,432,560}));auto hash=image.checksum();
        if(!plainHash)plainHash=hash;else assert(hash!=plainHash);
        if(markup.find("indent")!=std::string::npos)assert(styled.page().lines[0].inset==2*styled.fontSpec().px);
    }
    Font tall;tall.tall=true;Canvas regular,italic;
    Reader plain(store,tall,std::make_unique<StyledSource>("<p>ABC</p>")),slanted(store,tall,std::make_unique<StyledSource>("<p><em>ABC</em></p>"));
    assert(plain.open("fixture")&&slanted.open("fixture"));assert(plain.paint(regular,{24,100,432,560})&&slanted.paint(italic,{24,100,432,560}));
    assert(regular.bytes()!=italic.bytes());assert(plain.page().lines[0].width64==slanted.page().lines[0].width64);
    const auto first=reader.location();assert(reader.page().next==7);
    assert(reader.next());assert(reader.location().offset==7&&reader.page().lines.empty());
    assert(reader.paint(canvas,{0,0,8,8}));const auto image=reader.location();
    assert(reader.contentDiagnostics().find("\"image_page\":true")!=std::string::npos);
    assert(reader.contentDiagnostics().find("\"chapter_images\":1")!=std::string::npos);
    assert(reader.next());assert(reader.page().lines.size()==1);
    // Cold reopen drops the navigation history: reverse pagination must use
    // the same image-aware composition as forward pagination.
    assert(reader.close());assert(reader.open("fixture"));
    assert(reader.previous());assert(reader.location().offset==image.offset);
    assert(reader.previous());assert(reader.location().offset==first.offset);
    assert(reader.close());assert(reader.open("fixture"));assert(reader.location().offset==first.offset);
    if(argc==3){
        std::filesystem::create_directories(std::filesystem::path(argv[1])/"books");
        std::filesystem::copy_file(argv[2],std::filesystem::path(argv[1])/"books/test.epub");
        LocalDocument doc(store);Metadata meta;assert(doc.open("books/test.epub",meta));
        assert(doc.chapter(0,c));assert(c.images.size()==1&&!c.styles.empty());
        std::vector<uint8_t> image;assert(doc.resource(c.images[0].href,image,1024*1024));
        Canvas full;assert(paintBookImage(image,full,{24,0,432,560}));assert(full.checksum()!=Canvas().checksum());
        doc.close();
        Reader epub(store,font,std::make_unique<LocalDocument>(store));assert(epub.setRichEnabled(false));assert(epub.open("books/test.epub"));
        assert(epub.paint(full,{24,0,432,560}));assert(epub.next());assert(epub.page().lines.empty());
        assert(epub.paint(full,{24,0,432,560}));assert(epub.next());
        assert(epub.close());assert(epub.open("books/test.epub"));assert(epub.previous());assert(epub.page().lines.empty());
    }
    std::cout<<"PASS: CSS alignment, heading ranges, stable text offsets, anchors, safe image paths, PNG alpha/dither, malformed decode, image pagination and position persistence\n";
}
