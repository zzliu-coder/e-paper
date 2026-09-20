#include "paper/runtime.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace paper;
struct MaintenanceHardware final:Hardware {
    HostHardware host;
    BluetoothState bt;
    std::string btAction,btValue;
    BluetoothState bluetooth()const override{return bt;}
    Status bluetoothCommand(const std::string&a,const std::string&v)override{btAction=a;btValue=v;return {};}
    std::string receivedPassword;
    bool networkBusy=false;
    std::string networkMessage="尚未扫描";
    NetworkState network()const override {NetworkState state;state.busy=networkBusy;state.message=networkMessage;state.aps.push_back({"测试无线网络",-42,true});return state;}
    Status networkCommand(const std::string&a,const std::string&s,const std::string&p)override {if(a=="connect"){assert(s=="测试无线网络");receivedPassword=p;}return {};}
    bool supports(const std::string&k)const override{return k=="maintenance_status"||host.supports(k);}
    Status get(const std::string&k,std::string&v)override{
        if(k=="maintenance_status"){v="1.0.0-paper-maint3\n开发模式：保持唤醒\n存储卡已接回，USB 调试已恢复";return {};}
        return host.get(k,v);
    }
    Status set(const std::string&k,const std::string&v)override{return host.set(k,v);}
    Status action(const std::string&k)override{return host.action(k);}
    std::string environment()const override{return "host-maintenance-fixture";}
};
int main(int argc,char**argv){
    assert(argc==3);std::filesystem::create_directories(argv[2]);
    ResourceGate gate;Lease usb,sd,update,mic;
    assert(gate.acquire("sd","transfer",sd));assert(!gate.acquire("update","updater",update));assert(!gate.acquire("usb","msc",usb));gate.release(sd);
    assert(gate.acquire("update","updater",update));assert(!gate.acquire("sd","transfer",sd));assert(!gate.acquire("mic","recorder",mic));assert(!gate.acquire("usb","msc",usb));gate.release(update);
    assert(gate.acquire("usb","msc",usb));assert(!gate.acquire("sd","reader",sd));gate.release(usb);assert(gate.active().empty());
    MaintenanceHardware hw;Runtime app(argv[1],hw);auto init=app.initialize();if(!init){std::cerr<<init.message<<'\n';return 1;}
    for(auto page:{"home","settings","maintenance","library","transfer","network"}){
        auto st=app.action(page);if(!st){std::cerr<<page<<": "<<st.message<<'\n';return 2;}
        DisplayJob j;assert(app.job(j));assert(j.frame.bytes().size()==96000);
        for(auto&h:j.hits){assert(h.box.x>=0&&h.box.y>=0&&h.box.x+h.box.w<=480&&h.box.y+h.box.h<=800);}
        std::ofstream img(std::string(argv[2])+"/"+page+".pgm",std::ios::binary);img<<"P5\n480 800\n255\n";for(int y=0;y<800;++y)for(int x=0;x<480;++x)img.put(char(j.frame.pixel(x,y)*85));
        assert(app.complete(j.revision,{}));std::ofstream(std::string(argv[2])+"/"+page+".json")<<app.snapshot();
    }
    assert(!app.pollNetwork());
    hw.networkBusy=true;hw.networkMessage="正在扫描";
    assert(app.pollNetwork());assert(!app.pollNetwork()); // pending frame, no repeated draw
    DisplayJob networkJob;assert(app.job(networkJob));assert(app.complete(networkJob.revision,{}));
    assert(!app.pollNetwork());
    hw.networkBusy=false;hw.networkMessage="扫描完成，请选择网络";
    assert(app.pollNetwork());assert(app.job(networkJob));assert(app.complete(networkJob.revision,{}));
    assert(!app.pollNetwork());
    assert(app.action("network-select","0"));
    hw.networkMessage="后台状态变化";assert(!app.pollNetwork()); // Never interrupt password input.
    for(char c:std::string("test1234"))assert(app.action("key",std::string(1,c)));
    assert(app.snapshot().find("test1234")==std::string::npos);
    assert(app.action("input-confirm"));assert(hw.receivedPassword=="test1234");
    assert(app.action("network-scan"));assert(app.action("network-view"));
    assert(app.action("setting","bt_audio_mode"));
    DisplayJob btJob;assert(app.job(btJob));assert(app.complete(btJob.revision,{}));
    assert(!app.pollNetwork());
    hw.bt.available=true;hw.bt.mode=2;hw.bt.revision++;hw.bt.devices.push_back({"AABBCCDDEEFF","测试耳机"});
    assert(app.pollNetwork());assert(app.job(btJob));assert(app.complete(btJob.revision,{}));
    assert(app.action("bluetooth-mode","2"));assert(hw.btAction=="mode"&&hw.btValue=="2");
    assert(app.action("bluetooth-connect","AABBCCDDEEFF"));assert(hw.btAction=="connect"&&hw.btValue=="AABBCCDDEEFF");
    assert(app.prepareUsb());std::vector<uint8_t>b;assert(!app.store().read("paper/fonts/manifest.json",b));assert(app.releaseUsb());assert(app.action("home"));
    assert(app.action("library"));assert(app.action("open","0"));
    for(auto action:{"reader-tools","font-back","toc","back","bookmark","bookmarks","back"}){auto st=app.action(action);if(!st){std::cerr<<action<<": "<<st.message;return 4;}DisplayJob j;assert(app.job(j));assert(app.complete(j.revision,{}));}
    assert(app.action("home"));
    Store store(argv[1],gate);PackedFonts font(store,{});
    for(int w:{400,500,700})for(int px=16;px<=40;++px)for(bool uiOnly:{false,true}){FontSpec spec{px,w,false};spec.uiOnly=uiOnly;auto st=font.validate(spec);if(!st){std::cerr<<w<<'/'<<px<<' '<<uiOnly<<' '<<st.message;return 3;}font.clear();}
    FontSpec full{22,500,false},small=full;small.uiOnly=true;assert(!(full==small));
    Glyph bookTitle,reference;assert(font.glyph(0x69ad,small,bookTitle));assert(font.glyph(0x69ad,full,reference));
    assert(bookTitle.advance64==reference.advance64&&bookTitle.coverage2==reference.coverage2);
    PinyinDictionary dict(store);TextSession input(dict);assert(input.begin("",192));assert(input.mode(InputMode::Pinyin26));for(char c:std::string("nihao"))assert(input.key(c));assert(!input.candidates().empty());
    std::cout<<"PASS: resource arbitration, 6 production UI pages, secret network input, 150 exact font faces, UI-to-full glyph fallback, USB reacquire, production Pinyin. Hardware NOT_TESTED\n";
}
