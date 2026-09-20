#define main fixture_test_main
#include "crossmux.cpp"
#undef main
int main(int argc,char**argv){
 assert(argc==3&&!fs::exists(argv[2]));fs::create_directories(fs::path(argv[2])/"books");
 fs::copy_file(argv[1],fs::path(argv[2])/"books/images.epub");
 ResourceGate gate;Store store(argv[2],gate);ok(store.initialize());Font font;
 Reader reader(store,font,std::make_unique<LocalDocument>(store));ok(reader.open("books/images.epub"));size_t turns=0,imagePages=0;
 for(;;){
  Canvas page;ok(reader.paint(page,{24,100,432,560}));
  auto diag=reader.contentDiagnostics();
  assert(diag.find("\"image_error\":\"\"")!=std::string::npos);
  if(diag.find("\"image_page\":true")!=std::string::npos){++imagePages;assert(page.checksum()!=Canvas().checksum());}
  auto st=reader.next();if(!st){assert(st.code==Error::NotFound);break;}assert(++turns<20);
 }
 assert(imagePages>=1);ok(reader.close());
 size_t png=0,jpg=0;for(auto&e:fs::recursive_directory_iterator(fs::path(argv[2])/".paper/cache/crossmux-1")){
  if(e.path().extension()==".png")++png;if(e.path().extension()==".jpg"||e.path().extension()==".jpeg")++jpg;
 }
 assert(png&&jpg);ok(reader.open("books/images.epub"));Canvas reopened;ok(reader.paint(reopened,{24,100,432,560}));
 assert(reader.contentDiagnostics().find("\"cache_hits\":1")!=std::string::npos);ok(reader.close());
 std::cout<<"PASS: real ZIP/CSS/Section/Page PNG+JPEG extraction, decode, navigation, sealed reopen and FAT rename simulation\n";
}
