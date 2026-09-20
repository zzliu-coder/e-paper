#include "paper/services.hpp"
#include <sstream>
#include <iomanip>
namespace paper {
    const std::vector<SettingDef>&settingDefinitions() {
        static const std::vector<SettingDef>defs= {
            {"epub_engine","电子书排版","显示",{"crossmux","local"},"crossmux",false},
            {
                "reader_px","阅读字号","显示", {
                    "16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32","33","34","35","36","37","38","39","40"
                }
                ,"26",false
            }
            , {
                "reader_weight","阅读字重","显示", {
                    "400","500","700"
                }
                ,"400",false
            }
            , {
                "reader_gray","阅读显示","显示", {
                    "mono","gray4"
                }
                ,"mono",false
            }
            , {
                "margin","正文页边","显示", {
                    "20","24","28","32"
                }
                ,"24",false
            }
            , {
                "line_gap","行距增加","显示", {
                    "8","10","12","14","16"
                }
                ,"14",false
            }
            , {
                "book_header","阅读页眉","显示", {"compact","book"},"compact",false
            }
            , {
                "keyboard","中文键盘","输入", {
                    "9","26"
                }
                ,"9",false
            }
            , {
                "language","界面语言","输入", {
                    "zh-CN"
                }
                ,"zh-CN",false
            }
            , {
                "haptic","触摸震动","硬件", {
                    "off","on"
                }
                ,"on",true
            }
            , {
                "cpu_idle","空闲频率","电源", {
                    "80","160","240"
                }
                ,"240",true
            }
            , {
                "standby_s","自动待机","电源", {
                    "180","600","1800"
                }
                ,"180",true
            }
            , {
                "shutdown_s","待机关机","电源", {
                    "0","180","600","1800"
                }
                ,"180",true
            }
            , {
                "network_grace_s","网络宽限","电源", {
                    "30","60","120"
                }
                ,"60",true
            }
            , {
                "wifi","无线网络","连接", {
                    "off","on"
                }
                ,"off",true
            }
            , {
                "usb_disk","USB磁盘","连接", {
                    "off","on"
                }
                ,"off",true
            }
            , {
                "bt_audio_mode","蓝牙音频","硬件", {
                    "0","1","2"
                }
                ,"1",true
            }
            , {
                "cellular","蜂窝网络","硬件", {
                    "off","on"
                }
                ,"off",true
            }
        };
        return defs;
    }
    Status Settings::persist(const std::map<std::string,std::string>&v) {
        std::ostringstream o;
        o<<"PAPERSET1\n";
        // Older firmware rejects unknown keys in PAPERSET1. New display-only
        // preferences have their own record so an app-only rollback still boots.
        for(auto&x:v)if(x.first!="book_header")o<<std::quoted(x.first)<<' '<<std::quoted(x.second)<<'\n';
        return store_.saveRecord("settings",o.str());
    }
    Status Settings::load() {
        values_.clear();
        for(auto&d:settingDefinitions())values_[d.key]=d.defaultValue;
        std::string payload;
        auto st=store_.loadRecord("settings",payload);
        if(st.code!=Error::NotFound) {
            if(!st)return st;
            std::istringstream in(payload);
            std::string magic,key,val;
            if(!(in>>magic)||magic!="PAPERSET1")return Status::fail(Error::Corrupt,"设置记录无效，未覆盖");
            std::set<std::string>seen;
            while(in>>std::quoted(key)>>std::quoted(val)) {
                auto d=std::find_if(settingDefinitions().begin(),settingDefinitions().end(),[&](auto&x){return x.key==key;});
                if(d==settingDefinitions().end()||!seen.insert(key).second||std::find(d->values.begin(),d->values.end(),val)==d->values.end())return Status::fail(Error::Corrupt,"设置项无效，未覆盖");
                values_[key]=val;
            }
            if(!in.eof())return Status::fail(Error::Corrupt,"设置记录未读完整");
            // Existing installations keep their locator semantics until the user
            // selects the new renderer. Both record namespaces remain recoverable.
            if(!seen.count("epub_engine"))values_["epub_engine"]="local";
        }
        std::string header;
        st=store_.loadRecord("reader-header",header);
        if(st){if(header!="book"&&header!="compact")return Status::fail(Error::Corrupt,"阅读页眉配置无效");values_["book_header"]=header;}
        else if(st.code!=Error::NotFound)return st;
        // Existing hardware persistence remains authoritative. Boot does not silently
        // turn Wi-Fi, USB disk, cellular, or microphones on based on stale app settings.
        for(auto&d:settingDefinitions())if(d.hardware&&hw_.supports(d.key)) {
            std::string v;
            st=hw_.get(d.key,v);
            if(st)values_[d.key]=v;
        }
        return {
        };
    }
    bool Settings::available(const std::string&key)const {
        for(auto&d:settingDefinitions())if(d.key==key)return !d.hardware||(hw_.supports(key));
        return false;
    }
    std::string Settings::get(const std::string&key)const {
        auto i=values_.find(key);
        return i==values_.end()?"":i->second;
    }
    Status Settings::apply(const std::string&key,const std::string&value) {
        auto d=std::find_if(settingDefinitions().begin(),settingDefinitions().end(),[&](auto&x){return x.key==key;});
        last_= {
            key,value,get(key),"pending",""
        };
        auto fail=[&](Status st) {
            last_.phase="failed";
            last_.error=st.message;
            return st;
        };
        if(d==settingDefinitions().end()||std::find(d->values.begin(),d->values.end(),value)==d->values.end())return fail(Status::fail(Error::Invalid,"设置选项无效"));
        if(!available(key))return fail(Status::fail(Error::Unavailable,"当前硬件后端未提供此功能"));
        if(key=="reader_gray"&&value=="gray4"&&!hw_.supports("gray4_verified"))return fail(Status::fail(Error::Unavailable,"本机灰阶尚未完成验证"));
        if(get(key)==value) {
            last_.phase="applied";
            return {
            };
        }
        auto old=get(key);
        auto next=values_;
        next[key]=value;
        if(d->hardware) {
            auto st=hw_.set(key,value);
            if(!st)return fail(st);
            std::string readback;
            st=hw_.get(key,readback);
            if(!st||readback!=value) {
                auto rollback=hw_.set(key,old);
                last_.phase=rollback?"failed":"recovery-required";
                last_.error="硬件读回未确认；"+std::string(rollback?"已恢复旧值":"恢复失败");
                return Status::fail(Error::BackendFailure,last_.error);
            }
            // Hardware backend already persists its own settings; do not save an
            // additional app record while USB has exclusive SD ownership.
            values_=std::move(next);
            last_.applied=value;
            last_.phase="applied";
            return {
            };
        }
        auto st=key=="book_header"?store_.saveRecord("reader-header",value):persist(next);
        if(!st)return fail(st);
        values_=std::move(next);
        last_.applied=value;
        last_.phase="applied";
        return {
        };
    }
    HostHardware::HostHardware() {
        for(auto&d:settingDefinitions())if(d.hardware)state_[d.key]=d.defaultValue;
    }
    bool HostHardware::supports(const std::string&k)const {
        return k=="gray4_verified"||state_.count(k);
    }
    Status HostHardware::get(const std::string&k,std::string&v) {
        if(!state_.count(k))return Status::fail(Error::Unavailable,"模拟硬件无此项");
        v=state_[k];
        return {
        };
    }
    Status HostHardware::set(const std::string&k,const std::string&v) {
        if(!supports(k))return Status::fail(Error::Unavailable,"模拟硬件无此项");
        if(reject_==k) {
            reject_.clear();
            return Status::fail(Error::BackendFailure,"测试注入：拒绝修改");
        }
        state_[k]=v;
        return {
        };
    }
    Status HostHardware::action(const std::string&n) {
        return Status::fail(Error::Unsupported,"桌面环境不执行硬件动作："+n);
    }
    const std::vector<AppDescriptor>&registeredApps() {
        static const std::vector<AppDescriptor>a= {
            {
                "reader","阅读",true
            }
            , {
                "settings","设置",true
            }
        };
        return a;
    }
    Status future::validate(const TranscriptEvent&e) {
        if(!e.session||!e.segment||e.endMs<e.startMs||e.text.size()>8192||!validUtf8(e.text))return Status::fail(Error::Invalid,"转写事件无效");
        if(e.kind==TranscriptKind::Gap&&!e.text.empty())return Status::fail(Error::Invalid,"缺口事件不能伪造文字");
        return {
        };
    }
}
