// Shared external-audio-module service. Reuses the board UART and its RX owner.
#include "bluetooth_service.hpp"
#include "SimpleUart.hpp"
#include "selftest.h"
#include "power_policy.h"
#include "power_hw.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <mutex>
#include <memory>
#include <new>
namespace paper_bluetooth {namespace {
std::mutex mutex;
std::mutex transmitMutex;
paper::BluetoothState state;
std::string pending,rx;
int requestedMode=0;
int64_t deadline=0;
bool transmitting=false;
bool modeSent=false;
bool modeAcknowledged=false;
bool discardLine=false;
bool railHeld=false;
bool supervisorStarted=false;
bool receptionSession=false;
bool localAudio=false;
bool localAudioOwnsRail=false;
int64_t leaseDeadline=0;
bool addressValid(const std::string&s){return s.size()==12&&s.find_first_not_of("0123456789abcdefABCDEF")==s.npos;}
void finish(const std::string&message,bool failed=false){state.busy=false;state.message=message;pending.clear();deadline=0;leaseDeadline=esp_timer_get_time()+(failed?0:120000000);if(failed){receptionSession=false;modeAcknowledged=false;}++state.revision;}
void line(std::string text){
    while(!text.empty()&&(text.back()=='\r'||text.back()==' '))text.pop_back();
    if(text.empty())return;
    if(state.recentReplies.size()>=6)state.recentReplies.erase(state.recentReplies.begin());
    std::string diagnostic=text.substr(0,160);
    while(!diagnostic.empty()&&!paper::validUtf8(diagnostic))diagnostic.pop_back();
    state.recentReplies.push_back(diagnostic);
    for(int mode=1;mode<=3;++mode)if(text=="SET MODE "+std::to_string(mode)){
        // An unsolicited mode report must not inherit another mode's ACK.
        modeAcknowledged=false;
        state.mode=mode;state.connected=false;state.connectionKnown=false;state.address.clear();state.devices.clear();
        receptionSession=mode==3;
        if(pending=="mode"&&requestedMode==mode&&modeSent){modeAcknowledged=true;finish("蓝牙模块已确认模式 "+std::to_string(mode));}
        else{state.message="蓝牙模式已更新";++state.revision;}
        return;
    }
    if(text=="INQUIRING START"){state.devices.clear();state.message="正在扫描蓝牙设备";++state.revision;return;}
    if(text.rfind("AT+BT:",0)==0&&text.size()>=18){
        std::string address=text.substr(6,12),name=text.substr(18);
        if(!addressValid(address))return;
        if(!paper::validUtf8(name))name.clear();
        if(name.size()>96){name.resize(96);while(!name.empty()&&!paper::validUtf8(name))name.pop_back();}
        for(auto&d:state.devices)if(d.address==address)return;
        if(state.devices.size()<24){state.devices.push_back({address,name});++state.revision;}
        return;
    }
    if(text=="INQ COMPLETE"&&pending=="scan"){finish("扫描完成，请选择设备");return;}
    if(text=="CONNECTING"){state.message="正在连接蓝牙设备";++state.revision;return;}
    if(text=="CONNECT SUCCESS"){
        state.connected=true;state.connectionKnown=true;
        if(pending=="connect")finish("蓝牙已连接，声音效果待确认");
        else{state.message="蓝牙已连接，声音效果待确认";++state.revision;}
        return;
    }
    if(text=="CONNECT TIMEOUT"||text.find("DISCONNECT")!=text.npos||text.find("CONNECT FAIL")!=text.npos){
        state.connected=false;state.connectionKnown=true;state.address.clear();finish("蓝牙连接失败或已断开",true);return;
    }
    if(text=="ERROR"||text.rfind("ERROR:",0)==0){finish("蓝牙模块返回错误",true);return;}
}
struct ModeRequest{int mode;bool acquire;};
bool configureMode(const ModeRequest* r){
    std::lock_guard<std::mutex> tx(transmitMutex);
    {std::lock_guard<std::mutex>l(mutex);deadline=esp_timer_get_time()+15000000;}
    auto&uart=SimpleUart::getInstance();
    if(r->acquire){
        PowerPolicy::GetInstance().Acquire(PowerNeed::PeripheralControl);
        // Boot initializes IO directly, so the policy's rail cache may still
        // be unknown. Explicitly align it while holding PeripheralControl.
        if(power_hw_main_rail_set(true)!=0){
            {std::lock_guard<std::mutex>l(mutex);transmitting=false;
            finish("蓝牙供电未就绪",true);}
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1800));
    }
    bool ok=uart.sendString(r->mode==1?"AT+RX=2\r\n":r->mode==2?"AT+TX=1\r\n":"AT+RX=1\r\n");
    vTaskDelay(pdMS_TO_TICKS(700));
    // Some modules reboot after RX/TX selection and drop the first MODE.
    // Retry only this idempotent selection, at most three times; UART ACK is
    // still mandatory. Never infer connection success from a successful send.
    for(int attempt=0;ok&&attempt<3;++attempt){
        {std::lock_guard<std::mutex>l(mutex);if(pending!="mode")break;modeSent=true;}
        ok=uart.sendString("AT+MODE="+std::to_string(r->mode)+"\r\n");
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
    return ok;
}
void modeTask(void*arg){
    std::unique_ptr<ModeRequest>r(static_cast<ModeRequest*>(arg));
    bool ok=configureMode(r.get());
    {std::lock_guard<std::mutex>l(mutex);transmitting=false;++state.revision;if(!ok)finish("蓝牙指令发送失败",true);}
    r.reset();vTaskDelete(nullptr);
}
void supervisorTask(void*){
    // Expire disconnected control sessions even when the Bluetooth page is closed.
    for(;;){Snapshot();vTaskDelay(pdMS_TO_TICKS(1000));}
}
}
bool OwnsControl(){std::lock_guard<std::mutex>l(mutex);return railHeld;}
void DefaultInitialization(void (*initialize)()){
    std::lock_guard<std::mutex>tx(transmitMutex);
    if(!OwnsControl())initialize();
}
paper::Status BeginLocalAudio(){
    bool acquire=false;
    {
        std::lock_guard<std::mutex>l(mutex);
        if(localAudio||state.busy||transmitting)
            return paper::Status::fail(paper::Error::Busy,"音频通道正在切换，请稍后重试");
        if(railHeld&&(state.mode!=1||!modeAcknowledged||state.connected||receptionSession))
            return paper::Status::fail(paper::Error::Busy,"请先结束蓝牙音频，再使用本地录音或提示音");
        if(!SimpleUart::getInstance().isInitialized())
            return paper::Status::fail(paper::Error::Unavailable,"音频模块尚未就绪");
        localAudio=true;localAudioOwnsRail=!railHeld;acquire=localAudioOwnsRail;
        if(!acquire)return {};
        railHeld=true;transmitting=true;modeSent=false;modeAcknowledged=false;requestedMode=1;
        pending="mode";state.busy=true;state.connectionKnown=false;receptionSession=false;
        state.message="正在准备本地音频";++state.revision;
    }
    ModeRequest request{1,acquire};
    const bool sent=configureMode(&request);
    bool ready=false;
    {
        std::lock_guard<std::mutex>l(mutex);transmitting=false;
        ready=sent&&modeAcknowledged&&pending.empty()&&!state.busy&&state.mode==1;
        if(!ready)finish("本地音频模块未确认，请重试",true);
        ++state.revision;
    }
    if(!ready){EndLocalAudio();return paper::Status::fail(paper::Error::Unavailable,"本地音频模块未确认");}
    return {};
}
void EndLocalAudio(){
    std::lock_guard<std::mutex>l(mutex);
    if(!localAudio)return;
    if(localAudioOwnsRail){
        PowerPolicy::GetInstance().Release(PowerNeed::PeripheralControl);railHeld=false;
        state.mode=0;state.connected=false;state.connectionKnown=false;state.address.clear();
        state.message="本地音频已结束";++state.revision;
    }
    localAudio=false;localAudioOwnsRail=false;
}
void Observe(const uint8_t*data,size_t size){
    if(!data)return;
    std::lock_guard<std::mutex>l(mutex);
    state.rxBytes+=size;state.lastRxMs=esp_timer_get_time()/1000;
    for(size_t i=0;i<size;++i){
        if(data[i]=='\n'||data[i]=='\r'){if(!discardLine)line(rx);rx.clear();discardLine=false;}
        else if(discardLine)continue;
        else if(rx.size()<512)rx.push_back(char(data[i]));
        else{rx.clear();discardLine=true;finish("蓝牙回复过长，请重试",true);}
    }
}
paper::BluetoothState Snapshot(){
    std::lock_guard<std::mutex>l(mutex);state.available=SimpleUart::getInstance().isInitialized();
    if(state.busy&&deadline&&esp_timer_get_time()>deadline)finish("蓝牙模块响应超时，请重试",true);
    // Reception mode can connect without a recognized UART link notification.
    // Keep an acknowledged reception session powered until explicit stop/mode
    // change. Errors/disconnects still expire immediately (leaseDeadline=now).
    if(railHeld&&!state.connected&&!state.busy&&!transmitting&&!selftest::Busy()&&
       !receptionSession&&!localAudio&&esp_timer_get_time()>=leaseDeadline){
        PowerPolicy::GetInstance().Release(PowerNeed::PeripheralControl);railHeld=false;
        state.mode=0;state.connectionKnown=false;state.address.clear();state.devices.clear();++state.revision;
    }
    auto snapshot=state;snapshot.busy=snapshot.busy||transmitting;snapshot.railHeld=railHeld;snapshot.railOn=power_hw_main_rail_is_on();return snapshot;
}
paper::Status Command(const std::string&action,const std::string&value){
    auto&uart=SimpleUart::getInstance();
    if(!uart.isInitialized())return paper::Status::fail(paper::Error::Unavailable,"蓝牙模块串口未就绪");
    if(action!="mode"&&action!="scan"&&action!="connect"&&action!="stop")return paper::Status::fail(paper::Error::Invalid,"未知蓝牙操作");
    if(action=="mode"&&value!="1"&&value!="2"&&value!="3")return paper::Status::fail(paper::Error::Invalid,"蓝牙模式无效");
    if(action=="connect"&&!addressValid(value))return paper::Status::fail(paper::Error::Invalid,"蓝牙地址无效");
    std::lock_guard<std::mutex>l(mutex);
    if(selftest::Busy()||localAudio)return paper::Status::fail(paper::Error::Busy,"本地音频或硬件自检正在使用设备");
    if(state.busy||transmitting)return paper::Status::fail(paper::Error::Busy,"蓝牙任务尚未完成");
    if(action=="stop"){
        receptionSession=false;
        if(railHeld){PowerPolicy::GetInstance().Release(PowerNeed::PeripheralControl);railHeld=false;}
        state.connected=false;state.connectionKnown=false;state.mode=0;state.address.clear();state.devices.clear();finish("已结束蓝牙控制");return {};
    }
    if(action!="mode"&&(!railHeld||state.mode!=2))return paper::Status::fail(paper::Error::Unavailable,"请先切换模式二并等待模块确认");
    if(action=="connect"){
        bool found=false;for(auto&d:state.devices)if(d.address==value)found=true;
        if(!found)return paper::Status::fail(paper::Error::Invalid,"设备不在当前扫描结果中");
    }
    pending=action;state.busy=true;state.message="正在等待蓝牙模块回复";++state.revision;
    deadline=esp_timer_get_time()+(action=="mode"?15000000:30000000);
    if(action=="mode"){
        modeSent=false;modeAcknowledged=false;
        receptionSession=false;state.connectionKnown=false;
        if(!supervisorStarted){
            if(xTaskCreate(supervisorTask,"paper_bt_lease",4096,nullptr,2,nullptr)!=pdPASS){finish("蓝牙监控启动失败",true);return paper::Status::fail(paper::Error::Unavailable,state.message);}
            supervisorStarted=true;
        }
        state.connected=false;
        requestedMode=value[0]-'0';auto*r=new(std::nothrow)ModeRequest{requestedMode,!railHeld};
        if(!r){finish("蓝牙任务内存不足");return paper::Status::fail(paper::Error::Unavailable,state.message);}
        bool acquired=r->acquire;transmitting=true;railHeld=true;
        if(xTaskCreate(modeTask,"paper_bt",4096,r,3,nullptr)!=pdPASS){delete r;if(acquired)railHeld=false;transmitting=false;finish("蓝牙任务启动失败");return paper::Status::fail(paper::Error::Unavailable,state.message);}
    }else{
        if(action=="scan")state.devices.clear();else{state.address=value;state.connected=false;}
        bool ok=uart.sendString(action=="scan"?std::string("AT+INQUIRING\r\n"):"AT+CONNECT="+value+"\r\n");
        if(!ok){finish("蓝牙指令发送失败",true);return paper::Status::fail(paper::Error::Io,state.message);}
    }
    return {};
}
}
