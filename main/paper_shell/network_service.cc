#include "network_service.hpp"
#include "wifi_station.h"
#include "wifi_scan_lease.h"
#include "power_policy.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <mutex>
#include <cstring>
#include <algorithm>
#include <new>
#include <atomic>
namespace paper_network {
namespace {
std::mutex mutex;
paper::NetworkState state;
std::atomic<bool> wantsRadio{false};
struct Request {std::string action,ssid,password;};
void finish(const std::string& message){std::lock_guard<std::mutex> lock(mutex);state.busy=false;state.message=message;}
struct ScanLease {bool held=WifiScanLease::TryAcquire("paper-network");~ScanLease(){if(held)WifiScanLease::Release();}};
esp_err_t remember(const Request&r){
    nvs_handle_t h;auto err=nvs_open("paper_wifi",NVS_READWRITE,&h);if(err!=ESP_OK)return err;
    err=nvs_set_str(h,"ssid",r.ssid.c_str());if(err==ESP_OK)err=nvs_set_str(h,"password",r.password.c_str());if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);return err;
}
bool saved(Request&r){
    nvs_handle_t h;if(nvs_open("paper_wifi",NVS_READONLY,&h)!=ESP_OK)return false;
    char ssid[33]={},password[65]={};size_t a=sizeof ssid,b=sizeof password;
    bool ok=nvs_get_str(h,"ssid",ssid,&a)==ESP_OK&&nvs_get_str(h,"password",password,&b)==ESP_OK;
    nvs_close(h);if(ok){r.ssid=ssid;r.password=password;}memset(password,0,sizeof password);return ok;
}
void work(Request&r){
    ScanLease lease;if(!lease.held){finish("其他任务正在使用无线网络，请稍后重试");return;}
    PowerNeedHold power(PowerNeed::UiKeepNet);
    auto&station=WifiStation::GetInstance();
    if(r.action=="off"){station.PauseForLp();std::lock_guard<std::mutex> l(mutex);state.connected=false;state.busy=false;state.ip.clear();state.message="无线网络已关闭";return;}
    auto err=station.StartManual();if(err!=ESP_OK){finish(std::string("无线初始化失败：")+esp_err_to_name(err));return;}
    if(r.action=="scan"){
        wifi_scan_config_t config={};config.scan_time.active.min=30;config.scan_time.active.max=100;
        err=esp_wifi_scan_start(&config,true);
        if(err!=ESP_OK){esp_wifi_clear_ap_list();finish(std::string("扫描失败：")+esp_err_to_name(err));return;}
        wifi_ap_record_t records[24]={};uint16_t count=24;
        err=esp_wifi_scan_get_ap_records(&count,records);
        if(err!=ESP_OK){esp_wifi_clear_ap_list();finish("读取扫描结果失败");return;}
        std::vector<paper::NetworkAccessPoint> aps;
        for(unsigned i=0;i<count;++i){std::string ssid((char*)records[i].ssid,strnlen((char*)records[i].ssid,32));if(ssid.empty())continue;
            if(std::none_of(aps.begin(),aps.end(),[&](auto&a){return a.ssid==ssid;}))aps.push_back({ssid,records[i].rssi,records[i].authmode!=WIFI_AUTH_OPEN});}
        std::lock_guard<std::mutex> l(mutex);state.aps=std::move(aps);state.busy=false;state.message="扫描完成，请选择网络";return;
    }
    if(r.action=="saved"&&!saved(r)){finish("尚无已保存网络，请先扫描并连接");return;}
    if(r.ssid.empty()||r.ssid.size()>32||r.password.size()>63){finish("保存的网络信息无效，请重新输入");return;}
    wifi_config_t config={};memcpy(config.sta.ssid,r.ssid.data(),r.ssid.size());memcpy(config.sta.password,r.password.data(),r.password.size());
    esp_wifi_disconnect();vTaskDelay(pdMS_TO_TICKS(80));
    station.ClearDisconnectReason();
    err=esp_wifi_set_config(WIFI_IF_STA,&config);memset(config.sta.password,0,sizeof config.sta.password);
    if(err==ESP_OK)err=esp_wifi_connect();
    bool connected=false;const auto deadline=esp_timer_get_time()+20000000;
    while(err==ESP_OK&&esp_timer_get_time()<deadline){
        wifi_ap_record_t ap={};esp_netif_ip_info_t ip={};auto*net=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if(station.IsConnected()&&esp_wifi_sta_get_ap_info(&ap)==ESP_OK&&r.ssid==std::string((char*)ap.ssid,strnlen((char*)ap.ssid,32))&&net&&esp_netif_get_ip_info(net,&ip)==ESP_OK&&ip.ip.addr){
            char address[16];esp_ip4addr_ntoa(&ip.ip,address,sizeof address);std::lock_guard<std::mutex> l(mutex);state.connected=true;state.ssid=r.ssid;state.ip=address;connected=true;break;}
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if(!connected){
        const auto reason=station.LastDisconnectReason();
        wifi_ap_record_t current={};const bool associated=esp_wifi_sta_get_ap_info(&current)==ESP_OK;
        esp_wifi_disconnect();
        std::string message;
        if(err!=ESP_OK)message=std::string("连接失败：")+esp_err_to_name(err);
        else if(reason==WIFI_REASON_NO_AP_FOUND)message="未找到已保存热点，请检查热点是否开启及距离";
        else if(reason==WIFI_REASON_AUTH_FAIL||reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT||reason==WIFI_REASON_HANDSHAKE_TIMEOUT)message="热点认证失败，请检查密码或热点安全设置";
        else if(associated)message="已连接热点，但未取得 IP，请检查路由器地址分配";
        else message="无线连接未完成，请重试或重新选择热点";
        finish(message+"（原因 "+std::to_string(reason)+"）");return;
    }
    err=remember(r);finish(err==ESP_OK?"已连接，网络信息已保存":"已连接，但保存失败，请重试");
}
void worker(void*arg){auto*r=static_cast<Request*>(arg);work(*r);std::fill(r->password.begin(),r->password.end(),'\0');delete r;vTaskDelete(nullptr);}
}
paper::NetworkState Snapshot(){
    std::lock_guard<std::mutex> lock(mutex);auto out=state;
    if(out.connected&&!WifiStation::GetInstance().IsConnected()){out.connected=false;out.ip.clear();if(!out.busy)out.message="连接已断开，可重新连接";}return out;
}
bool WantsRadio(){return wantsRadio.load();}
std::string SavedSsid(){
    nvs_handle_t h;if(nvs_open("paper_wifi",NVS_READONLY,&h)!=ESP_OK)return {};
    char ssid[33]={};size_t size=sizeof ssid;
    const bool ok=nvs_get_str(h,"ssid",ssid,&size)==ESP_OK;nvs_close(h);
    return ok?std::string(ssid,strnlen(ssid,32)):std::string();
}
paper::Status Command(const std::string&a,const std::string&s,const std::string&p){
    if(a!="scan"&&a!="connect"&&a!="saved"&&a!="off")return paper::Status::fail(paper::Error::Invalid,"未知网络操作");
    if(a=="connect"&&(s.empty()||s.size()>32||p.size()>63||(!p.empty()&&p.size()<8)||s.find('\0')!=s.npos||p.find('\0')!=p.npos))return paper::Status::fail(paper::Error::Invalid,"网络名称或密码长度无效");
    std::lock_guard<std::mutex> lock(mutex);if(state.busy)return paper::Status::fail(paper::Error::Busy,"网络任务正在进行，请稍后查看");
    auto*r=new(std::nothrow)Request{a,s,p};if(!r)return paper::Status::fail(paper::Error::Unavailable,"网络任务内存不足");
    state.busy=true;state.message=a=="scan"?"正在扫描，请稍后点查看结果":"正在处理网络请求";
    wantsRadio=a!="off";
    if(a!="scan"){state.connected=false;state.ip.clear();}
    if(xTaskCreate(worker,"paper_network",8192,r,3,nullptr)!=pdPASS){delete r;state.busy=false;return paper::Status::fail(paper::Error::Unavailable,"网络任务启动失败");}return {};
}
}
