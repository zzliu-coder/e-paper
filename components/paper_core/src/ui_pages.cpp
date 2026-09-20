// Native PAPER GLYPH page composition. Business actions remain in runtime.cpp.
#include "paper/runtime.hpp"
#include <cstdio>
namespace paper {
using ui::Painter;using ui::Align;using ui::Icon;using ui::Control;
namespace {
std::string nameOf(const std::string&s){auto n=s.rfind('/');return n==s.npos?s:s.substr(n+1);}
std::string shortName(const std::string&s){auto n=s.rfind('.');return n==s.npos?s:s.substr(0,n);}
std::string two(size_t n){auto s=std::to_string(n);return s.size()<2?"0"+s:s;}
std::string val(const std::string&k,const std::string&v){
    if(k=="text_render")return v=="dots"?"网点柔化":"原始黑白";
    if(k=="epub_engine")return v=="crossmux"?"图文排版":"兼容排版";
    if(k=="book_header")return v=="book"?"显示书名":"简洁页眉";
    if(v=="on")return "开启";
    if(v=="off")return "关闭";
    if(v=="mono")return "黑白";
    if(v=="gray4")return "四灰阶";
    if(v=="zh-CN")return "简体中文";
    if(k=="reader_weight")return v=="400"?"常规":v=="500"?"中等":"粗体";
    if(k=="shutdown_s"&&v=="0")return "不自动关机";
    if(k=="standby_s"||k=="shutdown_s")return std::to_string(std::stoi(v)/60)+" 分钟";
    if(k=="network_grace_s")return v+" 秒";
    if(k=="cpu_idle")return v+" MHz";
    if(k=="keyboard")return v+" 键";
    if(k=="reader_px"||k=="margin"||k=="line_gap")return v+" px";
    return v;
}
std::string hintFor(const std::string&k){
    if(k=="text_render")return "使用黑白快速刷新。\n网点柔化只处理字形边缘；保留字号与阅读位置。";
    if(k=="epub_engine")return "图文保留插图、段落和注音。\n两套排版分别保存进度与书签。";
    if(k=="book_header")return "简洁页眉减少额外标题字库的首次加载。\n书名仍可在书库中查看。";
    if(k=="reader_px")return "改变正文大小，保留当前阅读位置。";
    if(k=="reader_weight")return "只使用字库提供的真实字重。";
    if(k=="reader_gray")return "黑白优先响应；四灰阶需设备验证。";
    if(k=="margin")return "正文左右两侧保留相同的阅读空间。";
    if(k=="line_gap")return "这里设置字高之外增加的行间距离。";
    if(k=="keyboard")return "九键更宽；全拼适合熟悉字母位置的输入。";
    if(k=="cpu_idle")return "设置设备空闲时的频率，繁忙任务由系统调度。";
    if(k=="standby_s")return "停止操作达到设定时间后进入待机。";
    if(k=="shutdown_s")return "进入待机后，达到设定时间再自动关机。";
    if(k=="haptic")return "轻触操作时提供一次短震动反馈。";
    if(k=="usb_disk")return "先在电脑安全弹出，再关闭磁盘模式。";
    if(k=="wifi")return "使用现有网络服务连接，不自动启动文件传输。";
    return "设置确认生效后保存；失败时保留原值。";
}
}
void Runtime::text(Rect b,const std::string&s,int px,int w,bool inverse,int line){(void)line;Painter(canvas_,fonts_,textError_,&uiAudit_).label(b,s,px,w,Align::Left,inverse);}
void Runtime::paragraph(Rect b,const std::string&s,int px,int w,int line){Painter(canvas_,fonts_,textError_,&uiAudit_).paragraph(b,s,px,w,line);}
void Runtime::button(Rect b,const std::string&s,const std::string&a,const std::string&v,bool dark,bool enabled,int px){
    Control type=Control::Action;
    if(a=="key")type=Control::Key;
    else if(a=="font-value"||a=="set"||a=="mode")type=Control::Choice;
    else if(a=="list-prev"||a=="list-next"||a=="font-page"||a=="candidate-prev"||a=="candidate-next")type=Control::Quiet;
    else if(b.h<=56)type=Control::Compact;
    Painter(canvas_,fonts_,textError_,&uiAudit_).control(b,s,type,dark,enabled,px);hits_.push_back({b,a,v,enabled});
}
void Runtime::footnote(const std::string&s){Painter(canvas_,fonts_,textError_,&uiAudit_).footer(notice_.empty()?s:notice_);footerDrawn_=true;}
void Runtime::title(const std::string&s){
    const char*code=screen_==Screen::Home?"纸间 / 本地":screen_==Screen::Library?"书库 / 本地":screen_==Screen::Reading?"阅读 / 纸间":screen_==Screen::Input?"输入 / 本地":screen_==Screen::Transfer?"连接 / 纸间":readerFontScreen()?"字体 / 阅读":"设置 / 纸间";
    Painter(canvas_,fonts_,textError_,&uiAudit_).header(s,code,screen_!=Screen::Home);
    if(screen_!=Screen::Home)hits_.push_back({{24,20,56,56},"back","",true});
}
void Runtime::uiRow(Rect b,const std::string&s,const std::string&detail,const std::string&value,const std::string&a,const std::string&v,bool selected,bool available){
    Painter(canvas_,fonts_,textError_,&uiAudit_).row(b,s,detail,value,selected,available);hits_.push_back({b,a,v,available});
}
void Runtime::pageFooter(size_t page,size_t pages,const std::string&a,bool refresh){
    pages=std::max<size_t>(1,pages);const int y=refresh?624:ui::token::FooterTop;
    button({24,y,112,56},"上一组",a=="font"?"font-page":"list-prev",a=="font"?"prev":"",false,page>0,18);
    button({344,y,112,56},"下一组",a=="font"?"font-page":"list-next",a=="font"?"next":"",false,page+1<pages,18);
    Painter(canvas_,fonts_,textError_,&uiAudit_).label({156,y,168,56},two(page+1)+" / "+two(pages),18,400,Align::Center);
}
void Runtime::home(){
    Painter p(canvas_,fonts_,textError_,&uiAudit_);title("纸间");
    std::vector<FileEntry>entries;auto st=store_.list("books",entries,1024);size_t count=0;
    if(st)for(auto&f:entries){auto n=f.name.rfind('.');auto ext=n==std::string::npos?"":f.name.substr(n);std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});if(!f.directory&&(ext==".epub"||ext==".txt"))++count;}
    canvas_.circle(129,225,105,0,true);
    canvas_.arc(129,205,64,205,335,3,8);canvas_.arc(129,205,64,25,155,3,8);
    p.icon(Icon::Book,{99,176,60,60},true,3);p.label({55,270,148,32},"把时间留给阅读",18,400,Align::Center,true);
    canvas_.rect({246,120,210,210},0,false,ui::token::Outline,ui::token::HeroRadius);
    p.label({266,139,166,28},"本地书籍",20,500);
    if(st)canvas_.dots(two(count),276,184,10,0);else p.label({268,190,162,60},"未就绪",26,500);
    p.label({266,281,164,28},st?"EPUB / TXT":"检查存储卡",16,400);canvas_.rect({266,316,42,3},0);
    p.section(358,selectedFile_.empty()?"从一页开始":"接着上次读","阅读 / 01");
    canvas_.rect({24,402,432,136},0,false,ui::token::Outline,ui::token::PanelRadius);p.icon(Icon::Book,{42,424,36,48},false,2);
    if(selectedFile_.empty()){
        p.label({96,422,338,34},"打开你的第一本书",26,500);
        p.label({96,468,314,30},"从本地书库开始阅读",18,400);
        hits_.push_back({{24,402,432,136},"library","",true});
    }else{
        p.label({96,422,320,38},shortName(nameOf(selectedFile_)),26,500);
        p.label({96,472,270,28},"本次最近打开 · 继续阅读",18,400);
        hits_.push_back({{24,402,432,136},"continue","",true});
    }
    p.icon(Icon::Next,{412,474,20,20});
    // Keep the primary action prominent without a pill silhouette.
    button({24,568,432,64},"打开书库","library","",true,true,22);
    uiRow({24,664,432,76},"系统设置","显示、输入与连接","","settings");
    footnote("纸间 · 本地阅读");
}
void Runtime::library(){
    Painter p(canvas_,fonts_,textError_,&uiAudit_);title("我的书库");
    p.section(116,folder_=="books"?"全部书籍":nameOf(folder_),two(files_.size())+" 项");
    size_t from=listPage_*6;
    for(size_t i=0;i<6&&from+i<files_.size();++i){auto&f=files_[from+i];int y=152+int(i)*76;
        p.icon(f.directory?Icon::Folder:Icon::Book,{25,y+14,32,42},false,2);
        std::string ext=f.directory?"文件夹":f.name.substr(f.name.rfind('.')+1);std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::toupper(c));});
        p.row({72,y,384,76},f.directory?f.name:shortName(f.name),ext+" / 本地文件","",false,true);
        hits_.push_back({{24,y,432,76},f.directory?"folder":"open",std::to_string(from+i),true});
    }
    if(files_.empty())p.empty(Icon::Book,"书库还没有书","通过系统传输导入 EPUB 或 TXT。\n文件准备好后，回到这里打开。",234);
    pageFooter(listPage_,(files_.size()+5)/6,"list",true);
    if(selectedFile_.empty()||files_.empty())button({24,708,432,64},"从电脑传入","transfer","",false,true,22);
    else button({24,708,432,64},"修改当前书籍名称","rename","",false,true,22);
}
void Runtime::reading(){
    Painter p(canvas_,fonts_,textError_,&uiAudit_);title(settings_.get("book_header")!="book"||reader_.metadata().title.empty()?"阅读":reader_.metadata().title);
    std::string sub="正文";
    // Spine positions include the cover, preface and navigation. They are not
    // printed chapter numbers. Prefer the publisher's unambiguous TOC label.
    for(const auto& entry:reader_.metadata().toc)
        if(entry.chapter==reader_.location().chapter&&!entry.title.empty()){
            sub=entry.title;break;
        }
    if(!reader_.fontWarning().empty())sub="字体缺失 · 临时使用 MiSans";
    if(!reader_.fallbackCodepoints().empty())sub="已用 MiSans 补字 "+std::to_string(reader_.fallbackCodepoints().size())+" 个";
    p.label({24,112,432,26},sub,18,400);
    auto st=reader_.paint(canvas_,ui::layout::body(reader_.margin()));if(!st&&textError_.empty())textError_=st.message;
    for(auto&link:reader_.links(ui::layout::body(reader_.margin())))hits_.push_back({link.box,"follow",link.href,true});
    if(reader_.hasLinkReturn())button({328,106,128,34},"返回原文","link-back","",false,true,18);
    int progress=reader_.progress28();
    p.segments({24,710,432,5},progress);
    button({24,734,96,56},"上页","prev","",false,true,18);
    button({136,734,96,56},"目录","toc","",false,true,18);
    button({248,734,96,56},"工具","reader-tools","",false,true,18);
    button({360,734,96,56},"下页","next","",true,true,18);
}
void Runtime::menus(){
    Painter p(canvas_,fonts_,textError_,&uiAudit_);
    if(screen_==Screen::Toc){
        title("目录与书签");
        button({24,122,99,56},"加书签","bookmark","",false,true,18);button({135,122,99,56},"书签","bookmarks","",false,true,18);button({246,122,99,56},"搜索","search","",false,true,18);button({357,122,99,56},"排版","font-menu","",false,true,18);
        const auto&toc=reader_.metadata().toc;size_t begin=listPage_*8;
        for(size_t i=0;i<8&&begin+i<toc.size();++i)uiRow({24,190+int(i)*60,432,60},toc[begin+i].title,"",two(begin+i+1),"toc-go",std::to_string(begin+i));
        if(toc.empty())p.empty(Icon::Book,"暂无目录","这本书没有可用的章节目录。",258);
        pageFooter(listPage_,(toc.size()+7)/8);
    }else{
        const bool b=screen_==Screen::Bookmarks;title(b?"我的书签":"搜索结果");size_t n=b?reader_.bookmarks().size():search_.size(),begin=listPage_*8;p.section(118,b?"收藏的位置":"找到的位置",two(n)+" 项");
        for(size_t i=0;i<8&&begin+i<n;++i){std::string t=b?reader_.bookmarks()[begin+i].excerpt:"章节 "+std::to_string(search_[begin+i].chapter+1)+" · 位置 "+std::to_string(search_[begin+i].offset);uiRow({24,158+int(i)*64,432,64},t,"","",b?"bookmark-go":"search-go",std::to_string(begin+i));}
        if(!n)p.empty(b?Icon::Book:Icon::Search,b?"还没有书签":"没有找到相关文字",b?"阅读时添加书签，\n方便下次回到这个位置。":"换一个关键词再试试。",252);
        pageFooter(listPage_,(n+7)/8);
    }
}
std::vector<size_t> Runtime::settingIndices()const{
    std::vector<size_t>ids;auto&defs=settingDefinitions();
    for(size_t i=0;i<defs.size();++i){auto&d=defs[i];if(d.key=="reader_gray")continue;bool yes=settingCategory_=="display"?d.category=="显示":settingCategory_=="input"?d.category=="输入":settingCategory_=="network"?(d.category=="连接"||d.key=="bt_audio_mode"||d.key=="cellular"):(d.category=="电源"||d.key=="haptic");if(yes)ids.push_back(i);}return ids;
}
void Runtime::settingsPage(){
    Painter p(canvas_,fonts_,textError_,&uiAudit_);
    if(screen_==Screen::Settings){
        title("控制面板");
        canvas_.rect({24,128,210,210},0,true,ui::token::Outline,ui::token::HeroRadius);p.label({46,157,150,64},"Aa",40,400,Align::Left,true);p.label({46,239,166,34},"显示与阅读",22,500,Align::Left,true);p.label({46,282,154,26},"字体 / 排版",16,400,Align::Left,true);hits_.push_back({{24,128,210,210},"settings-category","display",true});
        canvas_.circle(351,233,105,0,false,2);canvas_.circle(351,201,27,0,true);canvas_.circle(362,189,27,3,true);p.label({277,253,148,34},"设备与电源",22,500,Align::Center);p.label({277,292,148,24},"休眠 / 触觉",16,400,Align::Center);hits_.push_back({{246,128,210,210},"settings-category","device",true});
        canvas_.rect({24,366,210,110},0,false,ui::token::Outline,ui::token::PanelRadius);p.icon(Icon::Keyboard,{42,382,28,28});p.label({42,426,165,32},"中文输入",22,500);p.icon(Icon::Next,{200,432,18,18});hits_.push_back({{24,366,210,110},"settings-category","input",true});
        canvas_.rect({246,366,210,110},0,false,2,10);p.icon(Icon::Wifi,{266,384,28,28});p.label({264,426,168,32},"网络与蓝牙",22,500);p.icon(Icon::Next,{422,432,18,18});hits_.push_back({{246,366,210,110},"settings-category","network",true});
        uiRow({24,512,432,80},"连接与传输","书籍导入 / 文件导出","","transfer");
        uiRow({24,600,432,64},"开发与维护","USB 调试 / 字体 / 固件更新","","maintenance");
        p.label({24,693,360,28},"PAPER / 480 × 800 / 本地系统",18,400);
        footnote("MiSans · 统一界面");
    }else if(screen_==Screen::SettingsList){
        std::string titleText=settingCategory_=="display"?"显示与阅读":settingCategory_=="input"?"语言与输入":settingCategory_=="network"?"网络与蓝牙":"设备与电源";title(titleText);
        p.section(126,"偏好设置",settingCategory_=="display"?"阅读":"系统");auto ids=settingIndices();size_t begin=listPage_*6;
        for(size_t i=0;i<6&&begin+i<ids.size();++i){auto&d=settingDefinitions()[ids[begin+i]];bool available=settings_.available(d.key);p.row(ui::layout::row(int(i),170),d.label,"",available?val(d.key,effectiveSetting(d.key)):"暂不可用",false,available);hits_.push_back({ui::layout::row(int(i),170),"setting",d.key,true});}
        if(ids.size()>6)pageFooter(listPage_,(ids.size()+5)/6);
        else if(settingCategory_=="display")paragraph({24,652,432,72},"这里只影响阅读内容。\n系统菜单始终使用 MiSans。",18,400,30);
        else footnote("设置通过确认后保存");
    }else{
        auto it=std::find_if(settingDefinitions().begin(),settingDefinitions().end(),[&](auto&x){return x.key==settingKey_;});if(it==settingDefinitions().end())return;title(it->label);const bool avail=settings_.available(it->key);const auto current=effectiveSetting(it->key);
        p.section(128,"当前设置",val(it->key,current));
        const int cols=it->values.size()>8?5:it->values.size()==3?3:2;
        for(size_t i=0;i<it->values.size();++i)button(ui::layout::grid(int(i),188,cols,56,12),val(it->key,it->values[i]),"set",it->values[i],current==it->values[i],avail,cols==5?20:22);
        paragraph({24,620,432,84},avail?hintFor(it->key):"此设备能力尚未接入。\n当前设置保持不变。",20,400,32);footnote("点击选项应用 · 返回保持当前设置");
    }
}
void Runtime::networkPage(){
    Painter p(canvas_,fonts_,textError_,&uiAudit_);title("无线网络");
    auto state=hw_.network();networkAps_=state.aps;drawnNetwork_=state;
    p.section(122,"连接状态",state.connected?"已连接":state.busy?"处理中":"未连接");
    p.paragraph({24,158,432,82},state.message+(state.connected?"\n"+state.ssid+"  "+state.ip:""),20,400,30);
    button({24,250,210,52},"扫描网络","network-scan","",false,!state.busy,20);
    button({246,250,210,52},"查看结果","network-view","",false,true,20);
    for(size_t i=0;i<4&&listPage_*4+i<networkAps_.size();++i){size_t n=listPage_*4+i;auto&a=networkAps_[n];
        std::string name=validUtf8(a.ssid)?a.ssid:"名称编码不可显示";for(auto&c:name)if(static_cast<unsigned char>(c)<32)c=' ';
        uiRow({24,320+int(i)*68,432,64},name,a.secured?"需要密码":"开放网络",std::to_string(a.signal),"network-select",std::to_string(n),false,!state.busy);}
    button({24,602,132,50},"上一组","network-prev","",false,listPage_>0,18);
    button({168,602,132,50},"下一组","network-next","",false,(listPage_+1)*4<networkAps_.size(),18);
    button({312,602,144,50},"关闭无线","network-off","",false,!state.busy,18);
    button({24,680,432,54},"重连已保存网络","network-saved","",true,!state.busy,20);
    footnote("扫描与连接在后台进行 · 密码不写入日志");
}
void Runtime::bluetoothPage(){
    Painter p(canvas_,fonts_,textError_,&uiAudit_);title("蓝牙音频");
    auto s=hw_.bluetooth();drawnBluetooth_=s;
    p.section(122,"外置蓝牙模块",s.connected?"已连接":s.busy?"处理中":s.connectionKnown?"未连接":"状态待确认");
    p.paragraph({24,156,432,76},s.available?s.message:"蓝牙模块未就绪",20,400,30);
    const char* modes[]={"本机音频","连接耳机","接收音频"};
    for(int i=1;i<=3;++i)button({24+(i-1)*148,240,136,52},modes[i-1],"bluetooth-mode",std::to_string(i),s.mode==i,s.available&&!s.busy,20);
    button({24,308,432,52},"扫描音频设备","bluetooth-scan","",false,s.available&&!s.busy&&s.mode==2,20);
    for(size_t i=0;i<4&&listPage_*4+i<s.devices.size();++i){auto&d=s.devices[listPage_*4+i];
        std::string name=d.name;for(auto&c:name)if(static_cast<unsigned char>(c)<32)c=' ';
        uiRow({24,378+int(i)*64,432,60},name.empty()?"未命名设备":name,d.address,"连接","bluetooth-connect",d.address,s.connected&&s.address==d.address,!s.busy);
    }
    button({24,660,210,52},"上一组","bluetooth-prev","",false,listPage_>0,20);
    button({246,660,210,52},"下一组","bluetooth-next","",false,(listPage_+1)*4<s.devices.size(),20);
    button({24,720,432,42},"结束蓝牙控制","bluetooth-stop","",false,!s.busy,18);
    footnote(s.mode==3?"接收模式保持供电 · 用完请结束控制":"连接耳机可扫描 · 声音仍需试听确认");
}
void Runtime::keyboard(){
    Painter p(canvas_,fonts_,textError_,&uiAudit_);title(inputPurpose_=="wifi-password"?"输入无线网络密码":inputPurpose_=="rename"?"修改书籍名称":"搜索书中文字");
    canvas_.rect({24,116,432,84},0,false,ui::token::Outline,ui::token::PanelRadius);p.paragraph({40,124,400,68},input_.visible().empty()?"":input_.visible(),22,500,32);
    if(input_.visible().empty())p.label({40,132,400,40},inputPurpose_=="wifi-password"?"密码仅在本机保存":inputPurpose_=="rename"?"输入新的名称":"输入要查找的文字",20,400);
    p.label({24,207,432,30},(!lastStatus_&&!notice_.empty())?notice_:(input_.preedit().empty()?"拼音输入 · 轻触候选确认":input_.preedit()),18,400);
    auto&c=input_.candidates();size_t from=candidatePage_*4;
    for(size_t i=0;i<4&&from+i<c.size();++i)button({24+int(i)*110,244,102,56},c[from+i].text,"candidate",std::to_string(from+i)+":"+std::to_string(input_.generation()),i==0,true,20);
    if(c.empty()){canvas_.line(24,298,456,298,0,1);p.label({24,248,432,42},"候选词将在这里显示",18,400);}
    button({24,306,100,56},"上一组","candidate-prev","",false,candidatePage_>0,18);button({136,306,100,56},"下一组","candidate-next","",false,from+4<c.size(),18);
    auto mode=input_.mode();if(mode==InputMode::English)button({344,306,112,56},uppercase_?"ABC":"abc","shift","",uppercase_,true,18);else p.label({264,310,192,48},c.empty()?"":"候选 "+two(candidatePage_+1),16,400,Align::Right);
    const char*names[]={"九键","全拼","英文","数字","符号"};for(int i=0;i<5;++i)button({24+i*88,370,80,56},names[i],"mode",std::to_string(i),int(mode)==i,true,18);
    if(mode==InputMode::Pinyin9){
        const char* letters[]={"abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"};
        for(int i=0;i<8;++i){Rect b{24+(i%3)*148,436+(i/3)*68,136,64};p.control(b,"",Control::Key);p.label({b.x+8,b.y+4,120,28},std::to_string(i+2),22,500,Align::Center);p.label({b.x+8,b.y+33,120,26},letters[i],16,400,Align::Center);hits_.push_back({b,"key",std::to_string(i+2),true});}
    }else if(mode==InputMode::Pinyin26||mode==InputMode::English){
        const char*rows[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
        for(int row=0;row<3;++row){std::string keys=rows[row];int width=int(keys.size())*43-3,x0=24+(432-width)/2;for(size_t i=0;i<keys.size();++i){char ch=keys[i];if(mode==InputMode::English&&uppercase_)ch=char(std::toupper(static_cast<unsigned char>(ch)));button({x0+int(i)*43,436+row*68,40,64},std::string(1,ch),"key",std::string(1,ch),false,true,22);}}
    }else{
        std::string chars=mode==InputMode::Numbers?"1234567890":".,:;!?@#/-_+='\"()[]";int cols=mode==InputMode::Numbers?5:10,gap=cols==10?3:8,w=(432-(cols-1)*gap)/cols;
        for(size_t i=0;i<chars.size();++i)button({24+int(i%cols)*(w+gap),436+int(i/cols)*68,w,64},std::string(1,chars[i]),"key",std::string(1,chars[i]),false,true,22);
    }
    button({24,640,102,56},"左移","cursor","-1",false,true,18);button({134,640,102,56},"右移","cursor","1",false,true,18);button({244,640,102,56},"空格","literal"," ",false,true,18);button({354,640,102,56},"退格","delete","",false,true,18);
    button(ui::layout::pair(0,708),"取消","input-cancel");button(ui::layout::pair(1,708),"确认","input-confirm","",true);
}
void Runtime::transferPage(){
    Painter p(canvas_,fonts_,textError_,&uiAudit_);title("连接与传输");
    canvas_.circle(109,214,64,0,false,2);p.icon(Icon::Transfer,{79,184,60,60},false,3);
    canvas_.rect({314,150,128,128},0,true,2,20);p.icon(Icon::File,{352,184,52,60},true,3);
    for(int i=0;i<3;++i)canvas_.rect({201+i*29,210,17,6},0,true,1,2);
    p.label({45,295,128,32},"电脑",20,400,Align::Center);p.label({314,295,128,32},"纸间",20,400,Align::Center);
    p.section(370,"书籍导入，文件导出","连接");
    uiRow({24,424,432,92},"USB 磁盘","启用后由电脑独占存储卡","","setting","usb_disk");
    uiRow({24,536,432,92},"无线网络","配置连接，再使用系统传输","","setting","wifi");
    paragraph({24,680,432,72},"传输结束后，先在电脑安全弹出。\n书籍、字体都通过系统统一管理。",18,400,30);
    footnote(hw_.environment()=="host-simulated-hardware"?"桌面后端演示 · 设备传输仍待验证":"连接状态以系统后端返回为准");
}
}

namespace paper {
void Runtime::usbScreen(){
    canvas_.clear();hits_.clear();uiAudit_.clear();textError_.clear();
    ui::Painter p(canvas_,fonts_,textError_,&uiAudit_);
    p.header("USB 磁盘","存储 / USB",false);
    canvas_.circle(240,265,96,0,false,2);
    p.icon(ui::Icon::Usb,{204,226,72,78},false,4);
    p.label({24,400,432,44},"电脑正在使用存储卡",26,500,ui::Align::Center);
    p.paragraph({48,474,384,100},"先在电脑上安全弹出磁盘。\n然后点击下方按钮，返回纸间。",22,400,36);
    button({24,708,432,64},"安全弹出后返回","usb-exit","",true,true,22);
    p.footer("磁盘模式下，阅读和字体访问暂时停止");footerDrawn_=true;
}
}
