#include "paper/runtime.hpp"
#include <cstdio>
#include <sstream>
namespace paper {
    namespace {
        std::string basename(const std::string&s) {
            auto n=s.rfind('/');
            return n==s.npos?s:s.substr(n+1);
        }
        std::string parent(const std::string&s) {
            auto n=s.rfind('/');
            return n==s.npos?"books":s.substr(0,n);
        }
        bool indexValue(const std::string&s,size_t&n) {
            if(s.empty()||s.size()>10||s.find_first_not_of("0123456789")!=s.npos)return false;
            auto x=strtoull(s.c_str(),nullptr,10);
            if(x>10000000)return false;
            n=size_t(x);
            return true;
        }
    }
    Runtime::Runtime(const std::string&root,Hardware&h,Budget b,std::function<std::unique_ptr<DocumentSource>(Store&)>source) :store_(root,gate_),fonts_(store_,b),fontCatalog_(store_,b),dictionary_(store_,b.allowTestAssets),input_(dictionary_),hw_(h),settings_(store_,h),reader_(store_,fonts_,source?source(store_):std::make_unique<LocalDocument>(store_,b)),transfers_(store_) {
    }
    Status Runtime::initialize() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        reader_.setGrayAllowed(hw_.supports("gray4_verified"));
        auto s=store_.initialize();
        if(s)s=settings_.load();
        if(s)s=reader_.setRichEnabled(settings_.get("epub_engine")=="crossmux");
        if(s)s=reader_.initializeStyles({{std::stoi(settings_.get("reader_px")),std::stoi(settings_.get("reader_weight")),settings_.get("reader_gray")=="gray4"},std::stoi(settings_.get("margin")),std::stoi(settings_.get("reader_px"))+std::stoi(settings_.get("line_gap"))});
        if(s)s=dictionary_.loadLearning();
        if(s){
            std::string recent,absolute;
            auto read=store_.loadRecord("recent-book",recent);
            if(read&&recent.size()<=192&&recent.rfind("books/",0)==0&&validUtf8(recent)&&store_.path(recent,absolute,false))selectedFile_=recent;
        }
        lastStatus_=s;
        notice_=s?"":s.message;
        ++revision_;
        auto d=draw();
        return s?d:s;
    }
    Status Runtime::scan() {
        auto st=store_.list(folder_,files_,1024);
        if(!st)return st;
        files_.erase(std::remove_if(files_.begin(),files_.end(),[](auto&f){if(f.directory)return false;auto p=f.name.rfind('.');if(p==f.name.npos)return true;auto e=f.name.substr(p);for(char&c:e)c=char(std::tolower(uint8_t(c)));return e!=".epub"&&e!=".txt";}),files_.end());
        return {
        };
    }
    std::string Runtime::effectiveSetting(const std::string&key)const {
        const auto style=reader_.opened()?reader_.style():reader_.defaultStyle();
        if(key=="reader_px")return std::to_string(style.font.px);
        if(key=="reader_weight")return std::to_string(style.font.weight);
        if(key=="reader_gray")return style.font.gray?"gray4":"mono";
        if(key=="margin")return std::to_string(style.margin);
        if(key=="line_gap")return std::to_string(style.lineHeight-style.font.px);
        return settings_.get(key);
    }
    Status Runtime::beginInput(const std::string&purpose,const std::string&initial,size_t limit,bool secret) {
        auto st=input_.begin(initial,limit,secret);
        if(!st)return st;
        st=gate_.acquire("awake","text-input",inputLease_);
        if(!st) {
            input_.cancel();
            gate_.release(inputLease_);
            return st;
        }
        if(!secret)input_.mode(settings_.get("keyboard")=="26"?InputMode::Pinyin26:InputMode::Pinyin9);
        inputPurpose_=purpose;
        returnScreen_=screen_;
        screen_=Screen::Input;
        ++inputEpoch_;
        candidatePage_=0;
        return {
        };
    }
    Status Runtime::draw() {
        const auto activeStyle=reader_.style();
        fonts_.preferReader(reader_.opened()?&activeStyle.font:nullptr);
        canvas_.textDots(settings_.get("text_render")=="dots");
        // The USB hand-off page is composed while SD fonts are still readable.
        // Reuse that exact canvas while the host owns SD; never reload a font here.
        if(gate_.held("usb")){frameReady_=true;return {};}
        canvas_.clear();
        footerDrawn_=false;
        hits_.clear();
        uiAudit_.clear();
        textError_.clear();
        switch(screen_) {
            case Screen::Home:home();
            break;
            case Screen::Library:library();
            break;
            case Screen::Reading:reading();
            break;
            case Screen::Toc:case Screen::Bookmarks:case Screen::SearchResults:menus();
            break;
            case Screen::Settings:case Screen::Setting:case Screen::SettingsList:settingsPage();
            break;
            case Screen::Input:keyboard();
            break;
            case Screen::Transfer:transferPage();
            break;
            case Screen::Network:networkPage();break;
            case Screen::Bluetooth:bluetoothPage();break;
            case Screen::Maintenance: {
                title("开发与维护");
                std::string state;hw_.get("maintenance_status",state);
                paragraph({24,112,432,120},state,18,400,28);
                button({24,248,432,56},"USB 磁盘 / 安全交接","maintenance-usb");
                button({24,316,432,56},"检查字体与存储资源","maintenance-resources");
                button({24,384,432,56},"检查固件更新包","maintenance-check");
                button({24,452,432,56},"安装已检查的固件","maintenance-install");
                button({24,520,432,56},"开发保持唤醒 / 切换","maintenance-awake");
                button({24,588,432,56},"重启设备","maintenance-reboot");
                footnote("MiSans · 本地开发 · 保留恢复通道");
                break;
            }
            case Screen::ReaderFonts:case Screen::ReaderFontFamilies:case Screen::ReaderFontValues:case Screen::ReaderFontInbox:readerFontPage();break;
        }
        if(!footerDrawn_&&screen_!=Screen::Reading&&screen_!=Screen::Input&&!notice_.empty())footnote(notice_);
        frameReady_=true;
        return textError_.empty()?Status {
        }
        :Status::fail(Error::ResourceMissing,textError_);
    }
    Status Runtime::run(const std::string&a,const std::string&v) {
        if(a=="text-render")return settings_.apply("text_render",v);
        if(a=="glyph-lookahead"){
            if(v!="on"&&v!="off")return Status::fail(Error::Invalid,"字形预备模式无效");
            glyphLookahead_=v=="on";glyphPreparationError_.clear();glyphPrepared_=0;
            reader_.resetPreparation();fonts_.clearGlyphCache();return {};
        }
        if(a=="font-io-mode"){
            if(v!="batch"&&v!="legacy")return Status::fail(Error::Invalid,"字体读取模式无效");
            return fonts_.setBatchEnabled(v=="batch");
        }
        if(a=="font-verify-mode"){
            if(v!="single"&&v!="legacy")return Status::fail(Error::Invalid,"字体校验模式无效");
            return fonts_.setSinglePassVerification(v=="single");
        }
        if(a=="header-mode"){
            auto st=settings_.apply("book_header",v);if(!st)return st;
            fonts_.clear();return {};
        }
        size_t n=0;
        if(a=="maintenance"){auto st=reader_.close();if(!st)return st;screen_=Screen::Maintenance;return {};}
        if(a.rfind("maintenance-",0)==0)return hw_.action(a);
        if(a.rfind("font-",0)==0)return readerFontAction(a,v);
        if(a=="home") {
            if(input_.active())input_.cancel();
            gate_.release(inputLease_);
            auto st=reader_.close();
            if(!st)return st;
            screen_=Screen::Home;
            return {
            };
        }
        if(a=="back") {
            if(readerFontScreen())return readerFontAction("font-back","");
            if(screen_==Screen::Input) {
                input_.cancel();
                gate_.release(inputLease_);
                screen_=returnScreen_;
                return {
                };
            }
            if(screen_==Screen::Reading) {
                auto st=reader_.close();
                if(!st)return st;
                screen_=Screen::Library;
                return scan();
            }
            if(screen_==Screen::Library&&folder_!="books") {
                folder_=parent(folder_);
                listPage_=0;
                return scan();
            }
            if(screen_==Screen::Toc||screen_==Screen::Bookmarks||screen_==Screen::SearchResults) {
                screen_=Screen::Reading;
                return {
                };
            }
            if(screen_==Screen::SettingsList){screen_=Screen::Settings;settingCategory_.clear();listPage_=0;return {};}
            if(screen_==Screen::Setting) {
                screen_=settingCategory_.empty()?Screen::Settings:Screen::SettingsList;
                listPage_=0;
                return {
                };
            }
            auto st=reader_.close();
            if(!st)return st;
            screen_=Screen::Home;
            return {
            };
        }
        if(a=="library") {
            screen_=Screen::Library;
            folder_="books";
            listPage_=0;
            return scan();
        }
        if(a=="refresh") {
            full_=true;
            return screen_==Screen::Library?scan():Status {
            };
        }
        if(a=="folder"||a=="open") {
            if(!indexValue(v,n)||n>=files_.size())return Status::fail(Error::Invalid,"书籍选择已失效");
            auto path=folder_+"/"+files_[n].name;
            if(a=="folder") {
                if(!files_[n].directory)return Status::fail(Error::Invalid,"目标不是目录");
                folder_=path;
                listPage_=0;
                return scan();
            }
            // Library navigation retains the live reader. Re-selecting that
            // exact book must preserve its frame/locator and avoid reparsing.
            // SD handoff and resource mutations close the reader separately.
            if(reader_.opened()&&path==selectedFile_){screen_=Screen::Reading;return {};}
            auto st=reader_.open(path);
            if(st){selectedFile_=path;screen_=Screen::Reading;auto saved=store_.saveRecord("recent-book",path);if(!saved)notice_="本次可阅读；最近阅读记录未保存";}
            return st;
        }
        if(a=="continue"){
            if(selectedFile_.empty())return Status::fail(Error::NotFound,"尚无最近阅读");
            // Settings/home retain the open reader and its exact page. USB
            // handoff, rename and engine changes close it before mutation.
            if(reader_.opened()){screen_=Screen::Reading;return {};}
            auto st=reader_.open(selectedFile_);if(st)screen_=Screen::Reading;return st;
        }
        if(a=="next"||a=="prev"){
            if(screen_==Screen::Reading)return a=="next"?reader_.next():reader_.previous();
            if(screen_==Screen::Network)return run(a=="next"?"network-next":"network-prev","");
            if(screen_==Screen::Library||screen_==Screen::SettingsList||screen_==Screen::Toc||screen_==Screen::Bookmarks||screen_==Screen::SearchResults)return run(a=="next"?"list-next":"list-prev","");
            return {}; // Physical page keys do not move a hidden reader.
        }
        if(a=="reader-tools")return readerFontAction("font-menu","");
        if(a=="toc") {
            screen_=Screen::Toc;
            listPage_=0;
            return {
            };
        }
        if(a=="bookmark") {
            auto st=reader_.bookmark();
            if(st)notice_="已保存书签";
            return st;
        }
        if(a=="bookmarks") {
            screen_=Screen::Bookmarks;
            listPage_=0;
            return {
            };
        }
        if(a=="toc-go"||a=="bookmark-go"||a=="search-go") {
            if(!indexValue(v,n))return Status::fail(Error::Invalid,"索引无效");
            Status st;
            if(a=="toc-go")st=reader_.jump(n);
            else if(a=="bookmark-go") {
                if(n>=reader_.bookmarks().size())return Status::fail(Error::Invalid,"书签不存在");
                st=reader_.go(reader_.bookmarks()[n].location);
            }
            else {
                if(n>=search_.size())return Status::fail(Error::Invalid,"搜索结果不存在");
                st=reader_.go(search_[n]);
            }
            if(st)screen_=Screen::Reading;
            return st;
        }
        if(a=="follow")return reader_.follow(v);
        if(a=="link-back")return reader_.returnLink();
        if(a=="settings-category") {
            if(v!="display"&&v!="input"&&v!="device"&&v!="network")return Status::fail(Error::Invalid,"未知设置分组");
            settingCategory_=v;screen_=Screen::SettingsList;listPage_=0;notice_.clear();return {};
        }
        if(a=="network") {screen_=Screen::Network;listPage_=0;return {};}
        if(a=="bluetooth"){screen_=Screen::Bluetooth;listPage_=0;return {};}
        if(a=="bluetooth-mode"||a=="bluetooth-scan"||a=="bluetooth-connect"||a=="bluetooth-stop"){
            screen_=Screen::Bluetooth;return hw_.bluetoothCommand(a.substr(10),v);
        }
        if(a=="bluetooth-next"){if((listPage_+1)*4<drawnBluetooth_.devices.size())++listPage_;return {};}
        if(a=="bluetooth-prev"){if(listPage_)--listPage_;return {};}
        if(a=="network-view")return {};
        if(a=="network-scan"||a=="network-off"||a=="network-saved") {
            screen_=Screen::Network;return hw_.networkCommand(a.substr(8));
        }
        if(a=="network-next"){if((listPage_+1)*4<networkAps_.size())++listPage_;return {};}
        if(a=="network-prev"){if(listPage_)--listPage_;return {};}
        if(a=="network-select"){
            if(hw_.network().busy)return Status::fail(Error::Busy,"网络任务正在进行");
            size_t n=0;try{n=std::stoul(v);}catch(...){return Status::fail(Error::Invalid,"无效网络选项");}
            if(n>=networkAps_.size())return Status::fail(Error::Invalid,"网络列表已变化，请重新选择");
            networkSsid_=networkAps_[n].ssid;
            if(!networkAps_[n].secured)return hw_.networkCommand("connect",networkSsid_);
            return beginInput("wifi-password","",63,true);
        }
        if(a=="settings") {
            settingCategory_.clear();notice_.clear();
            screen_=Screen::Settings;
            listPage_=0;
            return {
            };
        }
        if(a=="setting") {
            if(v=="wifi"){screen_=Screen::Network;listPage_=0;return {};}
            if(v=="bt_audio_mode"){screen_=Screen::Bluetooth;listPage_=0;return {};}
            if(std::none_of(settingDefinitions().begin(),settingDefinitions().end(),[&](auto&d){return d.key==v;}))return Status::fail(Error::Invalid,"设置不存在");
            settingKey_=v;
            screen_=Screen::Setting;
            return {
            };
        }
        if(a=="set") {
            if(settingKey_=="epub_engine"){
                if(v!="crossmux"&&v!="local")return Status::fail(Error::Invalid,"排版选项无效");
                auto st=reader_.close();if(!st)return st;
                st=settings_.apply(settingKey_,v);if(!st)return st;
                return reader_.setRichEnabled(v=="crossmux");
            }
            if(settingKey_=="usb_disk"&&v=="on") {
                return hw_.action("maintenance-usb");
            }
            const bool layoutChange=settingKey_.find("reader_")==0||settingKey_=="margin"||settingKey_=="line_gap";
            if(layoutChange) {
                auto def=std::find_if(settingDefinitions().begin(),settingDefinitions().end(),[&](auto&d){return d.key==settingKey_;});
                if(def==settingDefinitions().end()||std::find(def->values.begin(),def->values.end(),v)==def->values.end())
                    return Status::fail(Error::Invalid,"设置选项无效");
                auto style=reader_.opened()?reader_.style():reader_.defaultStyle();
                if(settingKey_=="reader_px") {
                    style.lineHeight=std::min(64,std::stoi(v)+(style.lineHeight-style.font.px));
                    style.font.px=std::stoi(v);
                } else if(settingKey_=="reader_weight") style.font.weight=std::stoi(v);
                else if(settingKey_=="reader_gray") {
                    if(v=="gray4"&&!hw_.supports("gray4_verified"))return Status::fail(Error::Unavailable,"灰阶尚未通过设备启用条件");
                    style.font.gray=v=="gray4";
                } else if(settingKey_=="margin") style.margin=std::stoi(v);
                else if(settingKey_=="line_gap") style.lineHeight=style.font.px+std::stoi(v);
                if(reader_.opened())return reader_.applyStyle(style,reader_.hasFontOverride()?ReaderStyleScope::Book:ReaderStyleScope::Default);
                return reader_.applyDefaultStyle(style);
            }
            return settings_.apply(settingKey_,v);
        }
        if(a=="usb-exit") {
            return hw_.action("maintenance-usb-exit");
        }
        if(a=="transfer") {
            auto st=reader_.close();
            if(!st)return st;
            screen_=Screen::Transfer;
            return {
            };
        }
        if(a=="list-prev") {
            if(listPage_)--listPage_;
            return {
            };
        }
        if(a=="list-next") {
            size_t count=screen_==Screen::Library?files_.size():screen_==Screen::SettingsList?settingIndices().size():screen_==Screen::Settings?settingDefinitions().size():screen_==Screen::Toc?reader_.metadata().toc.size():screen_==Screen::Bookmarks?reader_.bookmarks().size():search_.size();
            size_t per=screen_==Screen::Library?6:(screen_==Screen::Settings||screen_==Screen::SettingsList)?6:8;
            if((listPage_+1)*per<count)++listPage_;
            return {
            };
        }
        if(a=="search")return beginInput("search","",192);
        if(a=="rename") {
            if(selectedFile_.empty())return Status::fail(Error::NotFound,"尚未选择书籍");
            auto st=reader_.close();
            return st?beginInput("rename",basename(selectedFile_),192):st;
        }
        if(a=="shift"){if(!input_.active()||input_.mode()!=InputMode::English)return Status::fail(Error::Conflict,"请先切换到英文输入");uppercase_=!uppercase_;return {};}
        if(a=="key") {
            if(v.size()!=1)return Status::fail(Error::Invalid,"按键无效");
            candidatePage_=0;
            return input_.key(v[0]);
        }
        if(a=="literal")return input_.literal(v);
        if(a=="mode") {
            if(!indexValue(v,n)||n>4)return Status::fail(Error::Invalid,"输入模式无效");
            candidatePage_=0;
            return input_.mode(InputMode(n));
        }
        if(a=="delete") {
            candidatePage_=0;
            return input_.backspace();
        }
        if(a=="cursor") {
            if(v!="-1"&&v!="1")return Status::fail(Error::Invalid,"光标方向无效");
            return input_.move(v=="-1"?-1:1);
        }
        if(a=="candidate") {
            auto p=v.find(':');
            size_t g;
            if(p==v.npos||!indexValue(v.substr(0,p),n)||!indexValue(v.substr(p+1),g))return Status::fail(Error::Invalid,"候选身份无效");
            candidatePage_=0;
            return input_.choose(n,g);
        }
        if(a=="candidate-prev") {
            if(candidatePage_)--candidatePage_;
            return {
            };
        }
        if(a=="candidate-next") {
            if((candidatePage_+1)*4<input_.candidates().size())++candidatePage_;
            return {
            };
        }
        if(a=="input-cancel") {
            input_.cancel();
            gate_.release(inputLease_);
            screen_=returnScreen_;
            return {
            };
        }
        if(a=="input-confirm") {
            if(!input_.active())return Status::fail(Error::Conflict,"没有输入会话");
            if(!input_.preedit().empty())return Status::fail(Error::Busy,"请先选择候选词");
            std::string result=input_.visible();
            Status st;
            if(inputPurpose_=="wifi-password"){
                st=input_.confirm(result);if(!st)return st;
                gate_.release(inputLease_);screen_=Screen::Network;
                st=hw_.networkCommand("connect",networkSsid_,result);
                std::fill(result.begin(),result.end(),'\0');return st;
            }
            if(inputPurpose_=="rename") {
                std::string clean;
                st=safeRelative(result,clean);
                if(st&&result.find('/')!=result.npos)st=Status::fail(Error::Invalid,"文件名不能包含路径");
                if(st)st=store_.renameFile(selectedFile_,parent(selectedFile_)+"/"+clean);
                if(!st)return st;
                input_.confirm(result);
                gate_.release(inputLease_);
                selectedFile_=parent(selectedFile_)+"/"+clean;
                screen_=Screen::Library;
                {auto saved=store_.saveRecord("recent-book",selectedFile_);if(!saved)notice_="改名成功；最近阅读记录未保存";}
                return scan();
            }
            st=reader_.find(result,search_);
            if(!st)return st;
            input_.confirm(result);
            gate_.release(inputLease_);
            screen_=Screen::SearchResults;
            listPage_=0;
            return {
            };
        }
        return Status::fail(Error::Unsupported,"未知操作");
    }
    Status Runtime::action(const std::string&a,const std::string&v) {
        std::lock_guard<std::recursive_mutex>lock(mutex_);
        if(a.size()>48||v.size()>8192)return Status::fail(Error::TooLarge,"操作参数过长");
        if(gate_.held("usb")&&a!="usb-exit"&&a!="maintenance-usb-exit")return Status::fail(Error::Busy,"电脑正在使用SD，请先安全弹出");
        if(screen_==Screen::Input&&(a=="mode"||a=="shift"||a=="input-cancel"||a=="input-confirm"||a=="back"||a=="home"))++inputEpoch_;
        auto st=run(a,v);
        lastStatus_=st;
        if(!st)notice_=st.message;
        else if(notice_==textError_)notice_.clear();
        full_=!hasPresented_||a=="refresh"||lastGray_!=outputGray();
        ++revision_;
        auto dr=draw();
        return st?dr:st;
    }
    Status Runtime::tap(int x,int y,uint64_t observed) {
        std::lock_guard<std::recursive_mutex>lock(mutex_);
        if(observed!=presented_||presented_!=revision_||!hasPresented_)return Status::fail(Error::Conflict,"画面已经变化，等待刷新完成");
        for(auto&h:hits_)if(h.box.contains(x,y)) {
            if(!h.enabled)return Status::fail(Error::Unavailable,"此选项暂不可用");
            auto act=h.action,val=h.value;
            return action(act,val);
        }
        return {
        };
    }
    Status Runtime::inputBatch(uint64_t epoch,const std::vector<std::pair<std::string,std::string>>&keys){
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if(screen_!=Screen::Input||!input_.active()||epoch!=inputEpoch_||gate_.held("usb"))return Status::fail(Error::Conflict,"输入布局已变化，请重新输入");
        if(keys.empty()||keys.size()>32)return Status::fail(Error::TooLarge,"输入队列超出上限");
        for(const auto&k:keys)if((k.first!="key"&&k.first!="delete"&&k.first!="cursor"&&k.first!="literal")||k.second.size()>4)return Status::fail(Error::Invalid,"无效连续输入");
        Status st;
        // Keep later correction keys (especially backspace) after a limit/error.
        // Report the first failure, but never silently discard the batch tail.
        for(const auto&k:keys){auto one=run(k.first,k.second);if(!one&&st)st=one;}
        lastStatus_=st;if(!st)notice_=st.message;
        full_=!hasPresented_||lastGray_!=outputGray();++revision_;
        auto drawn=draw();return st?drawn:st;
    }
    bool Runtime::pollNetwork(){
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if(!hasPresented_||revision_!=presented_||gate_.held("usb"))return false;
        if(screen_==Screen::Bluetooth){
            auto now=hw_.bluetooth();
            if(now.revision==drawnBluetooth_.revision)return false;
            // Do not refresh the panel for each discovery packet. Publish the final batch.
            if(now.busy&&drawnBluetooth_.busy)return false;
            full_=false;++revision_;lastStatus_=draw();return true;
        }
        if(screen_!=Screen::Network)return false;
        auto now=hw_.network();
        bool changed=now.busy!=drawnNetwork_.busy||now.connected!=drawnNetwork_.connected||
            now.message!=drawnNetwork_.message||now.ssid!=drawnNetwork_.ssid||now.ip!=drawnNetwork_.ip||now.aps.size()!=drawnNetwork_.aps.size();
        if(!changed)for(size_t i=0;i<now.aps.size();++i){
            auto&a=now.aps[i];auto&b=drawnNetwork_.aps[i];
            if(a.ssid!=b.ssid||a.signal!=b.signal||a.secured!=b.secured){changed=true;break;}
        }
        if(!changed)return false;
        full_=false;++revision_;lastStatus_=draw();return true;
    }
    Status Runtime::job(DisplayJob&j)const {
        std::lock_guard<std::recursive_mutex>lock(mutex_);
        if(!frameReady_)return Status::fail(Error::NotFound,"没有新画面");
        j.revision=revision_;
        j.inputEpoch=screen_==Screen::Input?inputEpoch_:0;
        j.full=full_;
        j.gray=outputGray();
        j.frame=canvas_;
        j.hits=hits_;
        return {
        };
    }
    Status Runtime::complete(uint64_t rev,Status s) {
        std::lock_guard<std::recursive_mutex>lock(mutex_);
        if(rev!=revision_)return Status::fail(Error::Conflict,"提交的画面已过期");
        lastStatus_=s;
        if(s) {
            presented_=rev;
            hasPresented_=true;
            lastGray_=outputGray();
            frameReady_=false;
        }
        else {
            hasPresented_=false;
            notice_=s.message;
            frameReady_=false;
        }
        return s;
    }
    Status Runtime::suspend() {
        std::lock_guard<std::recursive_mutex>lock(mutex_);
        auto st=gate_.allowSleep();
        if(!st)return st;
        if(input_.active())return Status::fail(Error::Busy,"输入尚未确认");
        st=reader_.close();
        if(st) {
            fonts_.releaseMemory();
            hasPresented_=false;
            screen_=Screen::Home;
        }
        return st;
    }
    Status Runtime::resume() {
        std::lock_guard<std::recursive_mutex>lock(mutex_);
        full_=true;
        ++revision_;
        return draw();
    }
    Status Runtime::prepareUsb() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if(input_.active())return Status::fail(Error::Busy,"请先结束输入");
        auto st=reader_.close();if(!st)return st;
        fonts_.clear();
        st=gate_.acquire("usb","system-transfer",usbLease_);
        if(!st)return st;
        return {};
    }
    Status Runtime::releaseUsb() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        gate_.release(usbLease_);fonts_.clear();
        auto st=store_.initialize();
        if(st)st=settings_.load();
        screen_=Screen::Maintenance;full_=true;++revision_;
        draw();return st;
    }
    bool Runtime::prepareIdle(const std::function<bool()>&stop){
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if(!glyphLookahead_||screen_!=Screen::Reading||!reader_.opened()||!glyphPreparationError_.empty())return false;
        size_t n=0;auto st=reader_.prepareAdjacentGlyphs(24,stop,n);glyphPrepared_+=n;
        if(!st)glyphPreparationError_=st.message;
        return n>0||!st;
    }
    std::string Runtime::snapshot()const {
        std::lock_guard<std::recursive_mutex>lock(mutex_);
        std::ostringstream o;
        // Experimental state never updates the stored reader style.
        o<<"{\"version\":\"paper-glyph-3.0\",\"environment\":"<<jsonString(hw_.environment())<<",\"screen\":"<<int(screen_)<<",\"revision\":"<<revision_<<",\"presented\":"<<presented_<<",\"error\":"<<jsonString(lastStatus_.message)<<",\"font_error\":"<<jsonString(textError_)<<",\"notice\":"<<jsonString(notice_)<<",\"glyph_cache_bytes\":"<<fonts_.cacheBytes()<<",\"voice\":{\"recording\":false,\"transcription\":false},\"reader\":{\"open\":"<<(reader_.opened()?"true":"false")<<",\"engine\":"<<jsonString(reader_.metadata().engine)<<",\"chapter\":"<<reader_.location().chapter<<",\"offset\":"<<reader_.location().offset<<",\"page_hint\":"<<(reader_.location().pageHint==SIZE_MAX?-1:int(reader_.location().pageHint))<<readerFontSnapshot()<<"},\"settings\":{";
        bool first=true;
        for(auto&d:settingDefinitions()) {
            if(!first)o<<',';
            first=false;
            std::string value=effectiveSetting(d.key);
            o<<jsonString(d.key)<<":"<<jsonString(value);
        }
        o<<"},\"frame_crc\":"<<canvas_.checksum()<<",\"input\":{\"active\":"<<(input_.active()?"true":"false")<<",\"visible\":"<<jsonString(input_.visible())<<",\"preedit\":"<<jsonString(input_.preedit())<<",\"generation\":"<<input_.generation()<<",\"candidates\":[";
        first=true;
        for(auto&c:input_.candidates()) {
            if(!first)o<<',';
            first=false;
            o<<jsonString(c.text);
        }
        o<<"]},\"hits\":[";
        first=true;
        for(auto&h:hits_) {
            if(!first)o<<',';
            first=false;
            o<<"{\"box\":["<<h.box.x<<','<<h.box.y<<','<<h.box.w<<','<<h.box.h<<"],\"action\":"<<jsonString(h.action)<<",\"value\":"<<jsonString(h.value)<<",\"enabled\":"<<(h.enabled?"true":"false")<<'}';
        }
        o<<"],\"font_cache\":"<<fonts_.statistics()<<",\"glyph_lookahead\":{\"enabled\":"<<(glyphLookahead_?"true":"false")<<",\"prepared\":"<<glyphPrepared_<<",\"error\":"<<jsonString(glyphPreparationError_)<<"}";
        o<<",\"reading_content\":"<<reader_.contentDiagnostics();
#ifdef PAPER_UI_AUDIT
        o<<",\"ui_profile\":\"PAPER-GLYPH-3\",\"ui_nodes\":[";first=true;
        for(const auto&n:uiAudit_){if(!first)o<<',';first=false;o<<"{\"kind\":"<<jsonString(n.kind)<<",\"box\":["<<n.box.x<<','<<n.box.y<<','<<n.box.w<<','<<n.box.h<<"],\"radius\":"<<n.radius<<",\"px\":"<<n.px<<",\"enabled\":"<<(n.enabled?"true":"false")<<",\"interactive\":"<<(n.interactive?"true":"false")<<",\"elided\":"<<(n.elided?"true":"false")<<'}';}o<<']';
#endif
        o<<'}';
        return o.str();
    }
}
