#include "ui_demo.h"
#include "ui/components.h"
#include "ui/typography.h"
#include "ui/gallery.h"
#include "ui/font_lab.h"
#include <cstdio>
namespace ui_demo {
using namespace inkdesk;
namespace {
enum Id {GoHome=1,Library,Reading,Settings,Wallpaper,Note,RealReader,Diagnostics,Legacy,
         Controls,FontDown,FontUp,Bookmark,Prev,Next,Tab,Pick,Apply,Detail,Fonts,Theme,
         Gallery,GalleryTab,Refresh,ToggleSample,SampleAction,Lab,LabTab,LabProfile,LabSize,LabVote};
void text(Frame& f,int x,int y,int w,const char* s,int size=25,bool black=true) {
  paper::Text(f,x,y,w,s,!black&&size==18?paper::token::InverseCaption:size,black);
}
void panel(Frame& f,Rect r,bool dark=false) {
  paper::Panel(f,r,dark);
}
void hit(Frame& f,Rect r,int id,int value=0) {
  paper::Hit(f,r,id,value);
}
void button(Frame& f,Rect r,const char* s,int id,int value=0,bool dark=false,int size=25) {
  paper::Button(f,r,s,id,value,dark,size);
}
void dots(Frame& f,int x,int y,const char* s,int cell,bool black=true) {
  paper::Dots(f,x,y,s,cell,black);
}
void bar(Frame& f,Rect r,int value,bool black=true) {
  paper::Progress(f,r,value,black);
}
void top(Frame& f,const char* title,bool home=false) {
  paper::Header(f,title,home?0:GoHome);
}
void marker(Frame& f,int x,int y,bool black=true){f.rect({x,y,6,6},true,black);}
void footer(Frame& f,const char* s){text(f,20,774,440,s,18);}
}
const char* Model::SampleText(){return
 "清晨，窗边的光一点点移到桌上。我把手机放远，翻开昨天没有读完的那一页。屋里很安静，只有水壶偶尔发出细小的声音。书里的时间走得慢。一个句子读完，还可以停下来，想一想它和自己的生活有什么关系。"
 "傍晚的风从窗边吹过。我合上书，把刚才想到的一句话记下来。阅读留下的东西，有时很轻，却能陪我们走很远。今天不必急着读完，明天还可以从这里继续。"
 "这是一段用于检查中文排版的文字。打开工具时，文字的位置保持不变；收起工具后，可以继续阅读。字号改变时，系统会寻找包含原来位置的页面。翻到最后一页，再回到第一页，句子应当完整，顺序也应当一致。";}
int Model::PageCount()const{return paper::Pagination(SampleText(),font,theme).count;}
size_t Model::ReadingOffset()const{paper::Pagination pages(SampleText(),font,theme);return pages.offsets[std::clamp(readingPage,1,pages.count)-1];}
void Model::Draw(Frame& f) const {
  f.reset(); f.view=View::Home; f.revision=revision;f.epoch=revision;
  switch(page){
  case Page::Home:
    top(f,"纸间",true);
    panel(f,{20,64,440,246},true);text(f,36,80,280,"设备时钟 / 示例",18,false);marker(f,438,86,false);
    dots(f,36,127,"09:41",8,false);
    // Static pixel pattern, no periodic animation or refresh.
    for(int i=0;i<9;++i){f.rect({352+i*9,133+i*9,6,6},true,false);f.rect({424-i*9,133+i*9,6,6},true,false);}
    text(f,36,197,284,"9月17日 周四",25,false);
    f.rect({36,241,408,1},true,false);
    text(f,36,258,122,"交互样机",18,false);text(f,180,258,112,"无写入",18,false);text(f,322,258,114,"示例数据",18,false);
    panel(f,{20,322,440,190});text(f,36,336,300,"正在阅读 / 示例",18);
    paper::Texture(f,{382,344,64,8},paper::Tone::Mid);
    text(f,36,367,388,"系统之美",30);text(f,36,410,350,"第64页 / 共758页",18);bar(f,{36,446,256,20},8);
    button(f,{310,440,136,54},"继续阅读",Reading,0,true,25);
    panel(f,{20,524,214,142});text(f,36,538,158,"书库",25);dots(f,36,578,"04",8);text(f,146,610,66,"本书",18);hit(f,{20,524,214,142},Library);
    panel(f,{246,524,214,142});text(f,262,538,174,"电池 / 示例",18);dots(f,262,578,"82",7);text(f,354,603,76,"%",25);bar(f,{262,638,178,18},82);hit(f,{246,524,214,142},Settings);
    button(f,{20,680,138,66},"笔记",Note);button(f,{170,680,138,66},"壁纸",Wallpaper);button(f,{320,680,140,66},"设置",Settings);
    paper::Dashed(f,{20,760,440,1});footer(f,"UI 样机 · 设置内可打开真实功能");break;
  case Page::Library:
    top(f,"返回 / 书库");panel(f,{20,64,440,198},true);
    text(f,36,80,280,"我的书库 / 示例",25,false);dots(f,36,127,"04",9,false);text(f,163,160,96,"本书",25,false);
    for(int i=0;i<3;++i)button(f,{20+i*148,214,144,48},i==0?"全部":i==1?"在读":"已读",Tab,i,i!=tab,25);
    if(tab!=2){
      panel(f,{20,274,440,194});text(f,36,290,340,"系统之美",30);text(f,36,334,372,"第64页 · 阅读至8%",18);bar(f,{36,375,224,20},8);
      button(f,{278,400,166,52},"阅读示例",Reading,0,true);hit(f,{20,274,440,116},Reading);
    }else{paper::Message(f,{20,274,440,194},"日常观察","已读完 · 示例书目");hit(f,{20,274,440,194},Reading);}
    button(f,{20,480,214,132},tab==2?"日常观察":"慢下来的一天",Reading,0,false,25);
    button(f,{246,480,214,132},"山间来信",Reading,0,false,25);
    text(f,24,630,432,"这些示例不会修改你的 SD 书籍",18);
    button(f,{20,680,440,66},"打开真实 SD 书库",RealReader,0,true);
    footer(f,"真实书库继续使用现有 EPUB / TXT 引擎");break;
  case Page::Reading:{
    top(f,"返回 / 阅读示例");text(f,24,76,420,"慢下来的一天 / 排版测试",25);
    paper::BodyPage(SampleText(),ReadingOffset(),font,theme,&f);
    char p[32];std::snprintf(p,sizeof(p),"%d / %d 页",readingPage,PageCount());
    paper::Dashed(f,{24,570,432,1});text(f,24,586,260,p,20);
    bar(f,{274,590,182,16},readingPage*100/PageCount());
    if(controls){
      button(f,{20,634,54,60},"-",FontDown);char n[8];std::snprintf(n,sizeof(n),"%d",font);text(f,94,646,62,n,25);
      button(f,{170,634,54,60},"+",FontUp);
      button(f,{246,634,214,60},bookmarked?"已加入书签":"加入书签",Bookmark,0,bookmarked,25);
      button(f,{20,724,138,60},"上页",Prev);button(f,{170,724,138,60},"下页",Next);button(f,{320,724,140,60},"收起",Controls);
    }else{
      text(f,24,646,432,bookmarked?"已加书签 · 本次会话":"轻触中间打开阅读工具",20);
      button(f,{20,724,138,60},"上页",Prev);button(f,{170,724,138,60},"工具",Controls);button(f,{320,724,140,60},"下页",Next);
      hit(f,{0,120,140,440},Prev);hit(f,{340,120,140,440},Next);hit(f,{140,120,200,440},Controls);
    }break;}
  case Page::Settings:
    top(f,"返回 / 设置");panel(f,{20,64,440,206},true);text(f,36,82,300,"设置 / 交互演示",25,false);
    dots(f,36,129,"82",10,false);text(f,176,168,64,"%",28,false);
    text(f,36,222,402,"电量为示例 · 本页不改硬件参数",18,false);
    paper::SettingRow(f,{20,282,214,142},"字体实验室","四种黑体方案",Lab);
    paper::SettingRow(f,{246,282,214,142},"网络与连接","功能说明",Detail,0);
    paper::SettingRow(f,{20,436,214,142},"校准与组件","文字 / 线框 / 灰",Gallery);
    paper::SettingRow(f,{246,436,214,142},"电源与休眠","功能说明",Detail,2);
    button(f,{20,590,440,54},"打开真实 SD 书库",RealReader);
    button(f,{20,656,440,54},"打开真实设备诊断",Diagnostics);
    button(f,{20,722,440,48},"返回原版纸间",Legacy,0,false,18);break;
  case Page::Fonts:
    top(f,"返回 / 字体主题");
    panel(f,{20,64,440,144},true);
    text(f,36,82,392,"思源黑体 / UI 默认",30,false);
    text(f,36,132,398,"Medium · 黑体方案进入字体实验室比较",20,false);
    text(f,36,170,390,"楷体方案暂不加入本轮",18,false);
    button(f,{20,220,440,68},"进入字体实验室",Lab,0,true,25);
    panel(f,{20,300,440,294});
    text(f,36,316,404,"中文与数字 / 同尺寸比较",25);
    text(f,36,364,404,"纸间 · 系统之美",30);
    text(f,36,414,404,"清晨的光，落在翻开的书页上。",25);
    text(f,36,458,404,"设置、阅读、笔记与连接状态",18);
    text(f,36,496,404,"Wi-Fi 82% 012 / 128",25);
    button(f,{36,536,404,50},"打开字体实验室",Lab,0,false,20);
    button(f,{20,608,440,62},"回首页比较整页",GoHome,0,true);
    button(f,{20,682,440,62},"打开阅读示例",Reading);
    footer(f,"本次开机记住选择 · 重启默认 A");break;
  case Page::Wallpaper:
    top(f,"返回 / 壁纸");panel(f,{20,64,440,150},true);text(f,36,82,380,"黑白图案 / 交互样机",25,false);text(f,36,136,380,"只在本次会话预览，不写入 SD",18,false);
    for(int i=0;i<4;++i){int x=20+(i%2)*226,y=228+(i/2)*216;
      panel(f,{x,y,214,204},i==wallpaper);
      for(int k=0;k<8;++k)f.rect({x+20+k*21,y+30,10,25+((k+i*3)%8)*12},true,i!=wallpaper);
      const char* names[]={"01 / 阶梯","02 / 节奏","03 / 序列","04 / 起伏"};text(f,x+16,y+164,184,names[i],25,i!=wallpaper);hit(f,{x,y,214,204},Pick,i);
    }
    button(f,{20,676,440,60},"查看所选图案",Apply,0,true);footer(f,"演示素材 · 原有壁纸与资源保持不变");break;
  case Page::Detail:
    top(f,"返回 / 功能说明");panel(f,{20,64,440,170},true);text(f,36,90,390,detail,28,false);
    text(f,36,148,390,"UI 样机 · 尚未绑定此硬件操作",18,false);
    text(f,24,278,432,"此处用于检查页面跳转与中文显示。",25);
    text(f,24,322,432,"联网、播放、休眠不会被自动执行。",25);
    if(std::strcmp(detail,"壁纸预览")==0){
      for(int k=0;k<12;++k)f.rect({30+k*35,404,22,30+((k+wallpaper*3)%8)*20},true);
    }
    button(f,{20,658,440,60},"打开真实设备诊断",Diagnostics);
    button(f,{20,730,440,50},"返回设置",Settings);break;
  case Page::Gallery:
    top(f,"返回 / 组件校准");paper::Gallery(f,gallery,GalleryTab,Refresh,ToggleSample,SampleAction,gallerySelected);break;
  case Page::FontLab:
    top(f,"返回 / 字体实验室");paper::FontLab(f,labPage,labProfile,labSize,labSaved,LabTab,LabProfile,LabSize,LabVote,Refresh);break;
  }
}
void Model::Home(){if(!visible||page!=Page::Home){visible=true;page=Page::Home;Changed();}}
void Model::Step(int d){
  if(page==Page::Reading){int n=std::clamp(readingPage+d,1,PageCount());if(n!=readingPage){readingPage=n;Changed();}}
  else if(page==Page::Library){int n=(tab+d+3)%3;if(n!=tab){tab=n;Changed();}}
  else if(page==Page::Gallery){gallery=(gallery+d+4)%4;refreshRequest=1;Changed();}
  else if(page==Page::FontLab){labPage=(labPage+d+paper::LabPageCount)%paper::LabPageCount;labSaved=0;refreshRequest=1;Changed();}
}
void Model::Tools(){if(page==Page::Reading){controls=!controls;Changed();}else{page=Page::Settings;Changed();}}
Exit Model::Tap(const Frame& f,int x,int y){
  if(f.epoch!=revision)return Exit::None;
  const auto e=f.hit(x,y);const int id=int(e.action);if(!id)return Exit::None;
  switch(id){
  case Note:visible=false;return Exit::Notes;
  case RealReader:visible=false;return Exit::Reader;
  case Diagnostics:visible=false;return Exit::Diagnostics;
  case Legacy:visible=false;return Exit::Legacy;
  case GoHome:Home();return Exit::None;
  case Library:page=Page::Library;break;
  case Reading:page=Page::Reading;break;
  case Settings:page=Page::Settings;break;
  case Wallpaper:page=Page::Wallpaper;break;
  case Fonts:page=Page::Fonts;break;
  case Lab:page=Page::FontLab;refreshRequest=1;break;
  case LabTab:labPage=std::clamp(e.value,0,paper::LabPageCount-1);labSaved=0;refreshRequest=1;break;
  case LabProfile:labProfile=std::clamp(e.value,0,3);labSaved=0;refreshRequest=1;break;
  case LabSize:labSize=std::clamp(labSize+e.value,0,7);labSaved=0;refreshRequest=1;break;
  case LabVote:labFeedback=std::clamp(e.value,0,3);++labVote;labSaved=0;refreshRequest=1;break;
  case Theme:{if(e.value<0||e.value>1||theme==e.value)return Exit::None;auto anchor=ReadingOffset();theme=e.value;readingPage=paper::Pagination(SampleText(),font,theme).Containing(anchor);break;}
  case Controls:controls=!controls;break;
  case FontDown:{if(font==18)return Exit::None;auto anchor=ReadingOffset();font=font==30?28:font==28?25:18;readingPage=paper::Pagination(SampleText(),font,theme).Containing(anchor);break;}
  case FontUp:{if(font==30)return Exit::None;auto anchor=ReadingOffset();font=font==18?25:font==25?28:30;readingPage=paper::Pagination(SampleText(),font,theme).Containing(anchor);break;}
  case Bookmark:bookmarked=!bookmarked;break;
  case Prev:Step(-1);return Exit::None;
  case Next:Step(1);return Exit::None;
  case Tab:if(tab==e.value)return Exit::None;tab=e.value;break;
  case Pick:if(wallpaper==e.value)return Exit::None;wallpaper=e.value;break;
  case Apply:detail="壁纸预览";page=Page::Detail;break;
  case Detail:detail=e.value==0?"网络与连接":e.value==1?"声音与按键":"电源与休眠";page=Page::Detail;break;
  case Gallery:page=Page::Gallery;refreshRequest=1;break;
  case GalleryTab:if(gallery==e.value)return Exit::None;gallery=e.value;refreshRequest=1;break;
  case Refresh:refreshRequest=e.value?2:1;break;
  case ToggleSample:gallerySelected=!gallerySelected;break;
  case SampleAction:gallerySelected=!gallerySelected;break;
  default:return Exit::None;
  }
  Changed();return Exit::None;
}
}
