#include "ui_demo.h"
#include "ui/font_lab.h"
#include "ui/typography.h"
#include "ui/mono_blend.h"
#include <cassert>
#include <iostream>
#include <random>
#include <string>
using namespace ui_demo;
using namespace inkdesk;
void validate(const Model& m,Frame& f){
  m.Draw(f);assert(!f.overflow);
  for(size_t i=0;i<f.drawCount;++i){auto b=f.draws[i].box;assert(b.x>=0&&b.y>=0&&b.x+b.w<=480&&b.y+b.h<=800);}
  for(size_t i=0;i<f.hitCount;++i){auto b=f.hits[i].box;assert(b.x>=0&&b.y>=0&&b.x+b.w<=480&&b.y+b.h<=800);assert(b.h>=44&&b.w>=44);}
}
void dump(const char* name,const Model& m){
  Frame f;m.Draw(f);assert(!f.overflow);
  std::cout<<"{\"name\":\""<<name<<"\",\"theme\":"<<m.theme<<",\"draws\":[";
  for(size_t i=0;i<f.drawCount;++i){const auto& d=f.draws[i];if(i)std::cout<<',';
    std::cout<<"{\"kind\":"<<int(d.kind)<<",\"box\":["<<d.box.x<<','<<d.box.y<<','<<d.box.w<<','<<d.box.h
      <<"],\"size\":"<<int(d.size)<<",\"black\":"<<(d.black?"true":"false")
      <<",\"radius\":"<<int(d.radius)<<",\"stroke\":"<<int(d.stroke)<<",\"dash\":"<<int(d.dash)<<",\"gap\":"<<int(d.gap)<<",\"density\":"<<int(d.density)
      <<",\"text\":\""<<d.text.c_str()<<"\",\"dotRows\":[";
    if(d.kind==DrawKind::Dots)for(size_t n=0;n<d.text.size();++n)for(int row=0;row<7;++row){
      if(n||row)std::cout<<',';std::cout<<int(DotRow(d.text.c_str()[n],row));
    }
    std::cout<<"]}";
  }std::cout<<"]}"<<'\n';
}
int main(int argc,char**){
  Model m;Frame f;
  if(argc>1){
    dump("home",m);m.page=Page::Library;dump("library",m);
    m.page=Page::Reading;dump("reading",m);m.controls=true;dump("reading-controls",m);
    m.page=Page::Settings;dump("settings",m);m.page=Page::Wallpaper;dump("wallpaper",m);
    m.page=Page::Detail;m.detail="网络与连接";dump("detail",m);
    m.page=Page::Fonts;dump("fonts-a",m);m.theme=1;dump("fonts-b",m);
    m.theme=0;m.page=Page::Gallery;for(int i=0;i<4;++i){m.gallery=i;dump(("gallery-"+std::to_string(i)).c_str(),m);}return 0;
  }
  validate(m,f);assert(m.Tap(f,60,560)==Exit::None&&m.page==Page::Library);
  const auto rev=m.revision;m.Tap(f,360,710);assert(m.revision==rev); // stale frame
  validate(m,f);assert(m.Tap(f,300,420)==Exit::None&&m.page==Page::Reading);
  validate(m,f);m.Tap(f,230,760);assert(m.controls);
  validate(m,f);m.Tap(f,196,650);assert(m.font==28);
  for(int i=0;i<10;++i){validate(m,f);m.Tap(f,196,650);}assert(m.font==30);
  for(int i=0;i<10;++i){validate(m,f);m.Tap(f,50,650);}assert(m.font==18);
  validate(m,f);m.Tap(f,340,650);assert(m.bookmarked);
  for(int i=0;i<200;++i)m.Step(-1);assert(m.readingPage==1);
  for(int i=0;i<200;++i)m.Step(1);assert(m.readingPage==m.PageCount());
  m.Home();validate(m,f);assert(m.Tap(f,60,710)==Exit::Notes&&!m.visible);
  m.Home();m.page=Page::Library;validate(m,f);assert(m.Tap(f,260,710)==Exit::Reader&&!m.visible);
  m.Home();m.page=Page::Settings;validate(m,f);assert(m.Tap(f,260,676)==Exit::Diagnostics&&!m.visible);
  m.Home();m.page=Page::Settings;validate(m,f);assert(m.Tap(f,260,746)==Exit::Legacy&&!m.visible);
  m.Home();m.page=Page::Wallpaper;validate(m,f);m.Tap(f,270,280);assert(m.wallpaper==1);
  validate(m,f);m.Tap(f,260,700);assert(m.page==Page::Detail&&std::string(m.detail)=="壁纸预览");
  std::mt19937 rng(1709);
  m.page=Page::Settings;validate(m,f);m.Tap(f,70,340);assert(m.page==Page::FontLab);
  for(int p=0;p<paper::LabPageCount;++p)for(int a=0;a<4;++a)for(int s=0;s<8;++s){m.labPage=p;m.labProfile=a;m.labSize=s;validate(m,f);}
  m.labSize=0;validate(m,f);m.Tap(f,40,230);assert(m.labSize==0);
  m.labSize=7;validate(m,f);m.Tap(f,350,230);assert(m.labSize==7);
  validate(m,f);m.Tap(f,400,180);assert(m.labProfile==3&&m.refreshRequest==1);
  validate(m,f);const auto votes=m.labVote;m.Tap(f,50,700);assert(m.labVote==votes+1&&m.labFeedback==0);
  m.Tap(f,50,700);assert(m.labVote==votes+1); // stale rendered frame cannot double-vote
  m.labPage=paper::LabPageCount-1;m.Step(1);assert(m.labPage==0);m.Step(-1);assert(m.labPage==paper::LabPageCount-1);
  for(int i=0;i<20000;++i){
    if(!m.visible)m.Home();validate(m,f);
    if(i%5==0)m.Step(int(rng()%3)-1);
    else if(i%7==0)m.Tools();
    else if(i%11==0)m.Home();
    else m.Tap(f,int(rng()%480),int(rng()%800));
  }
  for(Page p:{Page::Home,Page::Library,Page::Reading,Page::Settings,Page::Wallpaper,Page::Detail,Page::Fonts,Page::Gallery}){
    m.page=p;for(int theme:{0,1})for(int size:{18,25,28,30})for(bool controls:{false,true}){m.theme=theme;m.font=size;m.controls=controls;validate(m,f);}
  }
  for(int g=0;g<4;++g){m.page=Page::Gallery;m.gallery=g;validate(m,f);}
  m.gallery=3;validate(m,f);auto before=m.revision;m.Tap(f,70,280);assert(m.revision==before);m.Tap(f,300,280);assert(m.revision==before);
  validate(m,f);m.Tap(f,300,205);assert(m.gallerySelected);
  validate(m,f);m.Tap(f,60,755);assert(m.refreshRequest==1);
  validate(m,f);m.Tap(f,320,755);assert(m.refreshRequest==2);
  for(int theme:{0,1})for(int size:{18,25,28,30}){
    paper::Pagination pages(Model::SampleText(),size,theme);assert(pages.count>0&&pages.count<64);
    std::string rebuilt;
    for(int p=0;p<pages.count;++p){
      Frame plain;auto end=paper::BodyPage(Model::SampleText(),pages.offsets[p],size,theme,&plain);
      assert(end==pages.offsets[p+1]);
      for(size_t i=0;i<plain.drawCount;++i){const auto& d=plain.draws[i];rebuilt+=d.text.c_str();int width=0;size_t pos=0;while(d.text.c_str()[pos]){uint32_t cp=0;auto n=utf8Next(d.text.c_str()+pos,d.text.size()-pos,cp);width+=paper::Advance(cp,size,theme);pos+=n;}assert(width<=432);}
    }
    assert(rebuilt==Model::SampleText());
    m.page=Page::Reading;m.theme=theme;m.font=size;m.readingPage=1;m.controls=false;Frame a,b;m.Draw(a);m.controls=true;m.Draw(b);
    // Controls must not change a single body draw command or reading anchor.
    for(size_t i=0;i<a.drawCount&&i<b.drawCount;++i)if(a.draws[i].kind==DrawKind::Text&&a.draws[i].box.y>=130&&a.draws[i].box.y<560){assert(a.draws[i].box.y==b.draws[i].box.y);assert(std::strcmp(a.draws[i].text.c_str(),b.draws[i].text.c_str())==0);}
  }
  // Exhaustive coverage/opacity, complementary colors and unchanged same-color fill.
  for(int a=0;a<256;++a)for(int opacity=0;opacity<256;++opacity){
    assert(paper_mono_mix(0,1,a,opacity)==1-paper_mono_mix(1,0,a,opacity));
    assert(paper_mono_mix(1,1,a,opacity)==1);assert(paper_mono_mix(0,0,a,opacity)==0);
  }
  for(int offset=0;offset<8;++offset){
    uint8_t pixels[8]={0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55};uint8_t previous[8];std::memcpy(previous,pixels,8);
    uint8_t mask[32];for(int i=0;i<32;++i)mask[i]=uint8_t(i*8);
    paper_mono_fill(pixels,9,2,4,offset,1,mask,16,255);
    for(int y=0;y<2;++y)for(int x=0;x<32;++x){bool v=(pixels[y*4+x/8]>>(7-x%8))&1;bool old=(previous[y*4+x/8]>>(7-x%8))&1;
      if(x<offset||x>=offset+9)assert(v==old);else assert(v==bool(paper_mono_mix(1,old,mask[y*16+x-offset],255)));
    }
  }
  for(auto tone:{paper::Tone::Paper,paper::Tone::Subtle,paper::Tone::Mid,paper::Tone::Strong,paper::Tone::Ink}){
    Frame f;paper::Texture(f,{17,19,40,40},tone);int black=0;for(int y=0;y<40;++y)for(int x=0;x<40;++x)black+=paper::PatternBlack(f.draws[0],x,y);
    assert(black==16*paper::Density(tone));
  }
  std::cout<<"PASS: navigation, stale input, exits, font limits, bookmarks, page bounds, wallpaper, 20000 randomized actions, render bounds\n";
}
