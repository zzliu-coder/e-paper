#include "paper/runtime.hpp"
#include <sstream>
namespace paper {
namespace {
std::string specJson(const ReaderStyle&s){std::ostringstream o;
o<<"{\"family\":"<<jsonString(s.font.family)<<",\"revision\":"<<jsonString(s.font.revision)<<",\"px\":"<<s.font.px<<",\"weight\":"<<s.font.weight<<",\"gray\":"<<(s.font.gray?"true":"false")<<",\"fallback\":"<<(s.font.allowFallback?"true":"false")<<",\"margin\":"<<s.margin<<",\"line_height\":"<<s.lineHeight<<'}';
return o.str();
}
bool parseInt(const std::string&s,int&n){if(s.empty()||s.size()>4||s.find_first_not_of("0123456789")!=s.npos)return false;
n=std::stoi(s);
return true;
}
}
bool Runtime::readerFontScreen()const{return screen_==Screen::ReaderFonts||screen_==Screen::ReaderFontFamilies||screen_==Screen::ReaderFontValues||screen_==Screen::ReaderFontInbox;
}
std::string Runtime::readerFontName(const std::string& id) {
    if(id=="misans")return "MiSans";
    for (const auto& family : fontFamilies_) {
        if (family.id != id) continue;
        // External family names are arbitrary user text. A compact system font
        // may lack some characters; show the exact ASCII ID instead of breaking
        // the whole settings screen or borrowing the reader font for UI text.
        std::vector<uint32_t> missing;
        if (fonts_.coverage(FontSpec{22,500,false},family.name,missing)) return family.name;
        return family.id;
    }
    return id == "misans" ? "MiSans" : id;
}
std::string Runtime::readerFontSnapshot()const{
    std::ostringstream o;
    o<<",\"style\":"<<specJson(reader_.style())<<",\"default_style\":"<<specJson(reader_.defaultStyle())<<",\"draft\":"<<specJson(fontDraft_)<<",\"has_override\":"<<(reader_.hasFontOverride()?"true":"false")<<",\"layout_revision\":"<<reader_.layoutRevision()<<",\"font_warning\":"<<jsonString(reader_.fontWarning())<<",\"preview_error\":"<<jsonString(fontPreviewError_)<<",\"fallback_glyphs\":"<<reader_.fallbackCodepoints().size()<<",\"ui_family\":\"misans\",\"families\":[";
    bool first=true;
    for(auto&f:fontFamilies_){if(!first)o<<',';
    first=false;
    o<<"{\"id\":"<<jsonString(f.id)<<",\"name\":"<<jsonString(f.name)<<",\"revision\":"<<jsonString(f.revision)<<",\"available\":"<<(f.available?"true":"false")<<",\"error\":"<<jsonString(f.error)<<",\"faces\":[";
    bool next=true;
    for(auto&a:f.faces){if(!next)o<<',';
    next=false;
    o<<"["<<a.px<<','<<a.weight<<"]";
    }o<<"]}";
    }o<<']';
    return o.str();
}
void Runtime::readerFontPage(){
    ui::Painter p(canvas_,fonts_,textError_,&uiAudit_);
    if(screen_==Screen::ReaderFonts){
        title("字体与排版");
        uiRow({24,124,432,64},"正文字体","",readerFontName(fontDraft_.font.family),"font-families");
        const std::string values[]={std::to_string(fontDraft_.font.px),fontDraft_.font.weight==400?"常规":fontDraft_.font.weight==500?"中等":"粗体",std::to_string(fontDraft_.lineHeight)};
        const char*labels[]={"字号 / px","字重","行高 / px"};const char*fields[]={"px","weight","line"};
        for(int i=0;i<3;++i){Rect b{24+i*148,212,136,84};canvas_.rect(b,0,false,ui::token::Outline,ui::token::PanelRadius);p.label({b.x+12,b.y+10,112,24},labels[i],16,400);p.label({b.x+12,b.y+38,112,34},values[i],26,500);hits_.push_back({b,"font-field",fields[i],true});}
        p.section(322,"正文预览","预览");canvas_.line(24,357,456,357,0,1);
        Canvas sample(480,800);auto st=reader_.previewStyle(fontDraft_,sample,{24,376,432,144});fontPreviewError_=st?"":st.message;
        if(st){for(int y=376;y<520;++y)for(int x=24;x<456;++x)canvas_.pixel(x,y,sample.pixel(x,y));}
        else p.paragraph({24,378,432,132},"此组合暂不可用\n"+st.message,20,400,32);
        canvas_.line(24,532,456,532,0,1);
        uiRow({24,544,432,64},"少量生僻字补字","",fontDraft_.font.allowFallback?"开启":"关闭","font-fallback",fontDraft_.font.allowFallback?"off":"on");
        p.label({24,610,432,26},reader_.hasFontOverride()?"本书使用独立配置":"本书当前跟随阅读默认",16,400);
        button({24,644,210,64},"仅这本书","font-apply","book",true,bool(st),22);
        button({246,644,210,64},"设为阅读默认","font-apply","default",false,bool(st),22);
        p.control({24,718,432,56},"恢复跟随默认",ui::Control::Quiet);hits_.push_back({{24,718,432,56},"font-follow","",true});
        footnote(notice_.empty()?"返回不保存预览 · 菜单字体保持不变":notice_);
    }else if(screen_==Screen::ReaderFontFamilies){
        title("选择正文字体");p.section(120,"已安装的字体",std::to_string(fontFamilies_.size())+" FAMILIES");
        size_t begin=fontListPage_*5;
        for(size_t i=0;i<5&&begin+i<fontFamilies_.size();++i){auto&f=fontFamilies_[begin+i];bool ok=f.available&&f.has(fontDraft_.font.px,fontDraft_.font.weight);bool chosen=f.id==fontDraft_.font.family&&(f.id=="misans"||f.revision==fontDraft_.font.revision);int y=164+int(i)*84;
            p.row({24,y,432,84},readerFontName(f.id),ok?(f.id=="misans"?"系统字体 · 存放于存储卡":"外置字体 · 已通过资源校验"):"当前字号或字重未提供",chosen?"已选":"",chosen,ok,false);
            hits_.push_back({{24,y,432,84},"font-family",f.id,ok});
        }
        pageFooter(fontListPage_,(fontFamilies_.size()+4)/5,"font",true);
        button({24,708,432,64},"导入字体资源包","font-inbox","",false,true,22);footnote(notice_.empty()?"点击字体，返回正文预览":notice_);
    }else if(screen_==Screen::ReaderFontValues){
        bool size=fontField_=="px",weight=fontField_=="weight";title(size?"选择字号":weight?"选择字重":"选择行高");
        std::vector<int>values;if(size)for(int n=16;n<=40;++n)values.push_back(n);else if(weight)values={400,500,700};else for(int n=fontDraft_.font.px+4;n<=64;++n)values.push_back(n);
        int selected=size?fontDraft_.font.px:weight?fontDraft_.font.weight:fontDraft_.lineHeight;
        p.section(120,"当前选择",std::to_string(selected)+(weight?"":" px"));
        const int top=values.size()>30?160:200;
        for(size_t i=0;i<values.size();++i){int n=values[i];bool ok=true;
            if(size||weight){ok=false;for(auto&f:fontFamilies_)if(f.id==fontDraft_.font.family&&f.has(size?n:fontDraft_.font.px,weight?n:fontDraft_.font.weight))ok=true;}
            Rect b=ui::layout::grid(int(i),top,weight?3:5,56,8);
            button(b,weight?(n==400?"常规":n==500?"中等":"粗体"):std::to_string(n),"font-value",std::to_string(n),selected==n,ok,22);
        }
        if(size){canvas_.line(24,552,456,552,0,1);p.paragraph({24,576,432,88},"带框的字号可以选择。\n无框的字号尚未提供对应资源。",20,400,32);p.label({24,690,432,32},"原生像素 · 不缩放字形",18,400);}
        else if(weight)p.paragraph({24,348,432,124},"所选字重来自字体文件。\n书中强调可使用合成加粗；\n不会改变系统界面字体。",20,400,34);
        footnote("选择后返回预览 · 返回取消本次选择");
    }else if(screen_==Screen::ReaderFontInbox){
        title("导入阅读字体");p.section(122,"待安装资源",".PFR");size_t begin=fontListPage_*5;
        for(size_t i=0;i<5&&begin+i<fontInbox_.size();++i){auto&f=fontInbox_[begin+i];uiRow({24,168+int(i)*84,432,84},f.name,"点击校验并安装","","font-install",f.path);}
        if(fontInbox_.empty())p.empty(ui::Icon::File,"还没有字体资源包","在电脑转换字体，通过系统传输导入。\n每个字体只需安装一次。",230);
        pageFooter(fontListPage_,(fontInbox_.size()+4)/5,"font",true);button({24,708,432,64},"重新检查资源","font-inbox","",false,true,22);footnote(notice_.empty()?"资源目录：paper/font-inbox":notice_);
    }
}
Status Runtime::readerFontAction(const std::string&a,const std::string&v){
    if(a=="font-menu"){
        if(!reader_.opened())return Status::fail(Error::Conflict,"请先打开一本书");
        auto st=fontCatalog_.list(fontFamilies_);
        if(!st)return st;
        fontDraft_=reader_.style();
        fontPreviewError_.clear();
        notice_=reader_.fontWarning();
        screen_=Screen::ReaderFonts;
        return {};
    }
    if(a=="font-install"){
        if(!readerFontScreen())return Status::fail(Error::Conflict,"请先进入字体管理");
        ReaderFontFamily installed;
        auto st=fontCatalog_.install(v,installed);
        if(!st)return st;
        st=fontCatalog_.list(fontFamilies_);
        if(!st)return st;
        notice_="已安装 "+readerFontName(installed.id)+"；选择后应用";
        return {};
    }
    if(!readerFontScreen())return Status::fail(Error::Conflict,"字体设置页面未打开");
    if(a=="font-back"){
        if(screen_==Screen::ReaderFonts){screen_=Screen::Reading;
        notice_.clear();
        }
        else if(screen_==Screen::ReaderFontInbox)screen_=Screen::ReaderFontFamilies;
        else screen_=Screen::ReaderFonts;
        return {};
    }
    if(a=="font-families"||a=="font-refresh"){
        if(a=="font-refresh")fonts_.clear();
        auto st=fontCatalog_.list(fontFamilies_);
        if(!st)return st;
        screen_=Screen::ReaderFontFamilies;
        fontListPage_=0;
        notice_.clear();
        return {};
    }
    if(a=="font-inbox"){
        auto st=store_.list("paper/font-inbox",fontInbox_,128);
        if(!st)return st;
        fontInbox_.erase(std::remove_if(fontInbox_.begin(),fontInbox_.end(),[](auto&f){return f.directory||f.name.size()<4||f.name.substr(f.name.size()-4)!=".pfr";}),fontInbox_.end());
        screen_=Screen::ReaderFontInbox;
        fontListPage_=0;
        return {};
    }
    if(a=="font-page"){
        const auto count=screen_==Screen::ReaderFontInbox?fontInbox_.size():fontFamilies_.size();
        if(v=="prev"){if(fontListPage_)--fontListPage_;
        }else if(v=="next"){if((fontListPage_+1)*5<count)++fontListPage_;
        }else return Status::fail(Error::Invalid,"分页无效");
        return {};
    }
    if(a=="font-family"){
        auto it=std::find_if(fontFamilies_.begin(),fontFamilies_.end(),[&](auto&f){return f.id==v;});
        if(it==fontFamilies_.end()||!it->available||!it->has(fontDraft_.font.px,fontDraft_.font.weight))return Status::fail(Error::ResourceMissing,"字体缺少当前字号或字重");
        auto candidate=fontDraft_;
        candidate.font.family=it->id;
        candidate.font.revision=it->revision;
        auto st=fonts_.validate(candidate.font);
        if(!st)return st;
        fontDraft_=candidate;
        screen_=Screen::ReaderFonts;
        notice_.clear();
        return {};
    }
    if(a=="font-field"){
        if(v!="px"&&v!="weight"&&v!="line")return Status::fail(Error::Invalid,"未知字体字段");
        fontField_=v;
        screen_=Screen::ReaderFontValues;
        return {};
    }
    if(a=="font-value"){
        int n=0;
        if(!parseInt(v,n))return Status::fail(Error::Invalid,"数值无效");
        auto candidate=fontDraft_;
        if(fontField_=="px"){candidate.font.px=n;
        candidate.lineHeight=std::min(64,n+(fontDraft_.lineHeight-fontDraft_.font.px));
        }
        else if(fontField_=="weight")candidate.font.weight=n;
        else if(fontField_=="line")candidate.lineHeight=n;
        else return Status::fail(Error::Invalid,"请先选择字段");
        if(!validReaderStyle(candidate))return Status::fail(Error::Invalid,"字体规格越界");
        auto st=fonts_.validate(candidate.font);
        if(!st)return st;
        fontDraft_=candidate;
        screen_=Screen::ReaderFonts;
        notice_.clear();
        return {};
    }
    if(a=="font-fallback"){
        if(v!="on"&&v!="off")return Status::fail(Error::Invalid,"补字选项无效");
        fontDraft_.font.allowFallback=v=="on";
        notice_.clear();
        return {};
    }
    if(a=="font-apply"){
        if(v!="book"&&v!="default")return Status::fail(Error::Invalid,"请选择仅本书或阅读默认");
        // revalidate bytes immediately before application; stale previews are insufficient
        auto st=fonts_.revalidate(fontDraft_.font);
        if(st)st=reader_.applyStyle(fontDraft_,v=="book"?ReaderStyleScope::Book:ReaderStyleScope::Default);
        if(st){notice_=v=="book"?"已保存本书字体；系统界面不变":"已保存阅读默认；其他独立书籍不变";
        fontDraft_=reader_.style();
        }return st;
    }
    if(a=="font-follow"){
        auto st=fonts_.revalidate(reader_.defaultStyle().font);
        if(st)st=reader_.applyStyle(reader_.defaultStyle(),ReaderStyleScope::FollowDefault);
        if(st){fontDraft_=reader_.style();
        notice_="本书已恢复跟随默认";
        }return st;
    }
    return Status::fail(Error::Unsupported,"未知字体操作");
}
}
