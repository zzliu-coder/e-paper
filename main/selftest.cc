#include "selftest.h"
#include "inkdesk_app.h"
#include "personal_sdk.h"
#include "board.h"
#include "IOExpander.hpp"
#include "SimpleUart.hpp"
#include "SdCardManager.hpp"
#include "pcf8563.h"
#include "sc7a20h.h"
#include "fontpack_lvgl.h"
#include "display/lv_adapter_display.h"
#ifdef CONFIG_PAPER_CORE_APP
#include "paper_shell/network_service.hpp"
#include "paper_shell/bluetooth_service.hpp"
#endif
#include "esp_lv_adapter.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_secure_boot.h"
#include "esp_flash_encrypt.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <mutex>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace selftest {
namespace {
constexpr const char* kDir = "/sdcard/metalio/e-ink/sdk-selftest";
constexpr int kCount = 13;
std::atomic<bool> visible{false};
const char* names[kCount] = {"display", "speaker", "haptic", "inputs", "record",
    "wifi", "bluetooth", "sd", "battery", "imu", "rtc", "recovery", "soak"};
const char* titles[kCount] = {"屏幕显示", "扬声器", "震动", "按键与触摸", "录音与回放",
    "Wi-Fi 联网", "蓝牙音频", "SD 存储", "电池与充电", "姿态传感器", "时钟 RTC", "恢复检查", "稳定性测试"};
const char* prompts[kCount] = {
    "检查文字、黑白块和方向。完成后点正常或异常。",
    "播放三声半秒提示音，音量 80；请在正常桌面距离听。",
    "设备会震动三次，确认能否感觉到。",
    "20 秒内：触摸屏幕和底部左中右；按上、下、AI/BOOT、电源键。请不要长按断电。",
    "开始后倒数三秒，请说一句话。录音三秒，保存 WAV 并回放。",
    "扫描后选网络并输入密码；检测获得 IP、主动断开及重连。密码仅本次使用。",
    "让耳机或音箱进入配对模式；扫描后选设备，连接并发送测试音。",
    "写入专用测试文件、读回核对并移除，不修改你的其他文件。",
    "20 秒内拔下 USB 再插回，记录充放电读数变化。",
    "10 秒内轻轻转动设备，记录三轴变化。",
    "检查芯片时间是否有效、两秒后是否前进。失效时可输入当前时间校时。",
    "读取安全设置和当前分区。恢复原系统仍需 Mac 的完整备份与 ROM 下载入口。此页不擦写固件。",
    "设备独立运行十分钟，定期记录内存和传感器。可以点停止，日志保留。"};
struct Job { int module; int action; char text[96]; char secret[65]; };
struct Result { std::string automatic = "未测"; std::string manual = "未确认"; unsigned run = 0; };
struct InputRow { char name[24]; bool pressed; int x,y; int64_t ms; };
std::array<Result, kCount> results;
std::mutex state_mutex, log_mutex, bt_mutex;
std::atomic<bool> busy{false}, cancel{false}, open_requested{false};
std::atomic<int> requested_page{-1};
std::atomic<int> collecting{-1};
std::atomic<unsigned> input_mask{0}, input_events{0};
std::atomic<bool> home_edge{false};
unsigned next_run = 0, revision = 0;
std::string message = "请选择测试项目", log_file;
bool log_ok = false;
size_t log_bytes = 0;
QueueHandle_t jobs = nullptr;
QueueHandle_t input_rows = nullptr;
lv_obj_t *root = nullptr, *status_label = nullptr, *summary_label = nullptr;
lv_obj_t *choice = nullptr, *password = nullptr, *time_field = nullptr;
lv_obj_t *keyboard = nullptr;
int page = -1;
unsigned shown_revision = ~0u;
std::vector<std::string> wifi_names, bt_addresses;
std::string bt_partial, bt_history;
std::atomic<bool> bt_connected{false};
esp_netif_t* test_netif = nullptr;

std::string String(cJSON* json, const char* key) {
    auto* p = cJSON_GetObjectItem(json, key);
    return cJSON_IsString(p) ? p->valuestring : "";
}
void SetMessage(const std::string& text) {
    std::lock_guard<std::mutex> lock(state_mutex);
    message = text;
    ++revision;
}
bool Append(const char* event, int module, cJSON* data) {
    std::lock_guard<std::mutex> lock(log_mutex);
    auto* row = cJSON_CreateObject();
    cJSON_AddStringToObject(row, "event", event);
    cJSON_AddStringToObject(row, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(row, "device_id", personal_sdk::DeviceId());
    cJSON_AddStringToObject(row, "boot_id", personal_sdk::BootId());
    cJSON_AddNumberToObject(row, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddStringToObject(row, "module", module >= 0 ? names[module] : "session");
    cJSON_AddNumberToObject(row, "run", module >= 0 ? results[module].run : 0);
    if (data) cJSON_AddItemToObject(row, "data", cJSON_Duplicate(data, true));
    char* raw = cJSON_PrintUnformatted(row);
    bool ok = false;
    if (raw && !log_file.empty() && log_bytes + strlen(raw) < 2 * 1024 * 1024) {
        FILE* f = fopen(log_file.c_str(), "a");
        if (f) {
            ok = fprintf(f, "%s\n", raw) > 0;
            ok = fclose(f) == 0 && ok;
            if (ok) log_bytes += strlen(raw) + 1;
        }
    }
    log_ok = ok;
    cJSON_free(raw);
    cJSON_Delete(row);
    return ok;
}
void SaveResults() {
    auto* doc=cJSON_CreateObject();
    cJSON_AddStringToObject(doc,"version",esp_app_get_description()->version);
    auto* array=cJSON_AddArrayToObject(doc,"tests");
    { std::lock_guard<std::mutex> lock(state_mutex);
      for (int i=0;i<kCount;++i) {
        auto* row=cJSON_CreateObject(); cJSON_AddStringToObject(row,"module",names[i]);
        cJSON_AddStringToObject(row,"automatic",results[i].automatic.c_str());
        cJSON_AddStringToObject(row,"manual",results[i].manual.c_str());
        cJSON_AddNumberToObject(row,"run",results[i].run); cJSON_AddItemToArray(array,row);
      }
    }
    char* raw=cJSON_PrintUnformatted(doc);
    { std::lock_guard<std::mutex> lock(log_mutex);
      const std::string temp=std::string(kDir)+"/latest.tmp", final=std::string(kDir)+"/latest.json";
      FILE* f=fopen(temp.c_str(),"w");
      if (f) { bool written=raw && fputs(raw,f)>=0; written=fclose(f)==0 && written;
        if (written) rename(temp.c_str(),final.c_str()); }
    }
    cJSON_free(raw); cJSON_Delete(doc);
}
void LoadResults() {
    FILE* f=fopen((std::string(kDir)+"/latest.json").c_str(),"r");
    if (!f) return;
    std::vector<char> bytes(8193,0); size_t n=fread(bytes.data(),1,8192,f); fclose(f);
    if (n>=8192) return;
    auto* doc=cJSON_Parse(bytes.data());
    if (doc && String(doc,"version")==esp_app_get_description()->version) {
        cJSON* item=nullptr;
        cJSON_ArrayForEach(item,cJSON_GetObjectItem(doc,"tests")) {
            for (int i=0;i<kCount;++i) if (String(item,"module")==names[i]) {
                results[i].automatic=String(item,"automatic");
                results[i].manual=String(item,"manual");
                if (results[i].manual=="待你确认") results[i].manual="需重新测试";
                if (results[i].automatic=="运行中") results[i].automatic="已中断";
                auto* run=cJSON_GetObjectItem(item,"run");
                if (cJSON_IsNumber(run) && run->valueint>=0) results[i].run=run->valueint;
                next_run=std::max(next_run,results[i].run);
            }
        }
    }
    cJSON_Delete(doc);
}
cJSON* Call(const char* command, int module, cJSON* args = nullptr) {
    auto* reply = personal_sdk::DiagnosticCall(command, args);
    Append(command, module, reply);
    return reply;
}
bool ReplyPass(cJSON* reply) {
    return cJSON_IsTrue(cJSON_GetObjectItem(reply, "ok")) &&
        (String(reply, "result").empty() || String(reply, "result") == "PASS");
}
bool Delay(int ms) {
    for (int elapsed = 0; elapsed < ms && !cancel; elapsed += 100) vTaskDelay(pdMS_TO_TICKS(100));
    return !cancel;
}
bool Display(const char* text, int module) {
    auto* args = cJSON_CreateObject(); cJSON_AddStringToObject(args, "text", text);
    auto* r = Call("display.text", module, args);
    bool ok = ReplyPass(r);
    auto* id = cJSON_GetObjectItem(r, "job_id");
    int job = cJSON_IsNumber(id) ? id->valueint : -1;
    cJSON_Delete(args); cJSON_Delete(r);
    for (int n = 0; ok && n < 150 && !cancel; ++n) {
        r = personal_sdk::DiagnosticCall("job.get");
        auto* done = cJSON_GetObjectItem(r, "job_done");
        if (cJSON_IsNumber(done) && done->valueint == job) {
            auto* result = cJSON_GetObjectItem(r, "job_result");
            ok = result && result->valueint == 0;
            cJSON_Delete(r); return ok;
        }
        cJSON_Delete(r); Delay(100);
    }
    return false;
}
bool Tones(int module) {
    for (int frequency : {440, 660, 880}) {
        if (cancel) return false;
        auto* args = cJSON_CreateObject();
        cJSON_AddNumberToObject(args, "frequency_hz", frequency);
        cJSON_AddNumberToObject(args, "duration_ms", 500);
        cJSON_AddNumberToObject(args, "volume", 80);
        auto* r = Call("audio.tone", module, args);
        bool ok = ReplyPass(r); cJSON_Delete(args); cJSON_Delete(r);
        bool finished = false;
        for (int n = 0; ok && n < 40; ++n) {
            r = personal_sdk::DiagnosticCall("status");
            finished = cJSON_IsFalse(cJSON_GetObjectItem(r, "audio_tone_busy"));
            if (finished) {
                ok = cJSON_GetObjectItem(r, "audio_tone_last_result")->valueint == 0 &&
                    cJSON_GetObjectItem(r, "audio_tone_samples")->valueint == 8000;
                Append("tone_complete", module, r);
            }
            cJSON_Delete(r);
            if (finished) break;
            Delay(100);
        }
        if (!ok || !finished) return false;
        Delay(400);
    }
    return true;
}

bool WifiConnect(Job& job) {
#ifdef CONFIG_PAPER_CORE_APP
    auto accepted=paper_network::Command("connect",job.text,job.secret);
    memset(job.secret,0,sizeof job.secret);if(!accepted)return false;
    for(int i=0;i<250&&!cancel;++i){
        auto state=paper_network::Snapshot();
        if(!state.busy){auto*row=cJSON_CreateObject();cJSON_AddStringToObject(row,"result",state.message.c_str());cJSON_AddStringToObject(row,"ip",state.ip.c_str());Append("wifi_connected",5,row);cJSON_Delete(row);return state.connected;}
        Delay(100);
    }
    return false;
#else
    // The diagnostic mode owns no product Wi-Fi session; do not start cloud services.
    if (test_netif == nullptr) {
        esp_err_t e = esp_netif_init();
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
        e = esp_event_loop_create_default();
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
        test_netif = esp_netif_create_default_wifi_sta();
        if (!test_netif) return false;
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT(); config.nvs_enable = false;
        if (esp_wifi_init(&config) != ESP_OK) return false;
        if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
            esp_wifi_start() != ESP_OK) return false;
    }
    wifi_config_t config = {};
    const size_t ssid_length=strlen(job.text), password_length=strlen(job.secret);
    if (ssid_length==0 || ssid_length>32 || password_length>63) return false;
    memcpy(config.sta.ssid,job.text,ssid_length);
    memcpy(config.sta.password,job.secret,password_length);
    esp_wifi_disconnect();
    bool ok = esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK;
    memset(&config, 0, sizeof(config)); memset(job.secret, 0, sizeof(job.secret));
    for (int pass = 0; ok && pass < 2; ++pass) {
        if (esp_wifi_connect() != ESP_OK) { ok = false; break; }
        bool connected = false;
        for (int n = 0; n < 200 && !cancel; ++n) {
            wifi_ap_record_t ap = {}; esp_netif_ip_info_t ip = {};
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
                esp_netif_get_ip_info(test_netif, &ip) == ESP_OK && ip.ip.addr != 0) {
                connected = true;
                auto* row = cJSON_CreateObject();
                char address[20]; snprintf(address, sizeof(address), IPSTR, IP2STR(&ip.ip));
                cJSON_AddStringToObject(row, "ip", address);
                cJSON_AddNumberToObject(row, "rssi", ap.rssi);
                cJSON_AddNumberToObject(row, "connection_attempt", pass + 1);
                Append("wifi_connected", 5, row); cJSON_Delete(row);
                break;
            }
            Delay(100);
        }
        ok = connected;
        if (ok && pass == 0) { esp_wifi_disconnect(); Delay(1200); }
    }
    return ok;
#endif
}

void BluetoothRun(Job& job, bool& ok, bool& manual) {
    auto& uart = SimpleUart::getInstance();
    bt_connected = false;
    {
        std::lock_guard<std::mutex> lock(bt_mutex);
        bt_history.clear(); bt_partial.clear();
        if (job.action == 1) bt_addresses.clear();
    }
    if (job.action == 1) {
        ok = uart.sendString("AT+TX=1\r\n"); Delay(700);
        ok = uart.sendString("AT+MODE=2\r\n") && ok; Delay(700);
        ok = uart.sendString("AT+INQUIRING\r\n") && ok;
        Delay(10000);
        SetMessage("扫描完成，选择设备后点开始连接。无结果时让设备重新进入配对模式。");
    } else {
        std::string address = job.text;
        if (address.size() != 12 || address.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
            ok = false; SetMessage("请先扫描并选择配对目标"); return;
        }
        ok = uart.sendString("AT+CONNECT=" + address + "\r\n");
        for (int i = 0; ok && !bt_connected && i < 200 && !cancel; ++i) Delay(100);
        ok = ok && bt_connected;
        if (ok) { SetMessage("已连接，发送三声测试音；请确认目标耳机或音箱发声。"); ok = Tones(6); manual = true; }
    }
    auto* r = cJSON_CreateObject();
    { std::lock_guard<std::mutex> lock(bt_mutex);
      cJSON_AddStringToObject(r, "uart_receipt", bt_history.c_str());
      cJSON_AddNumberToObject(r, "discovered", bt_addresses.size()); }
    cJSON_AddBoolToObject(r, "connect_success", bt_connected);
    Append("bluetooth", 6, r); cJSON_Delete(r);
}

void Worker(void*) {
    Job job{};
    while (true) {
        if (xQueueReceive(jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        const int m = job.module;
        bool ok = true, manual = false;
        Append("started", m, nullptr);
        if (m == 1 || m == 4) {
            auto& uart = SimpleUart::getInstance();
            ok = uart.sendString("AT+RX=2\r\n"); Delay(700);
            ok = uart.sendString("AT+MODE=1\r\n") && ok;
            SetMessage("正在恢复本机音频模式，请稍候……");
            Delay(4000); // External audio clock recovery is slower after inquiry.
        }
        cJSON* r = nullptr;
        if (m == 0) {
            if (esp_lv_adapter_lock(2000)==ESP_OK) {
                auto* display=LVAdapterDisplay::Instance();
                ok=display && display->RefreshDiagnostic()==ESP_OK;
                esp_lv_adapter_unlock();
            } else ok=false;
            manual = true;
        } else if (m == 1) { ok = Tones(m); manual = true; }
        else if (m == 2) {
            for (int i = 0; i < 3 && !cancel; ++i) {
                r = Call("haptic.pulse", m); ok = ReplyPass(r) && ok; cJSON_Delete(r); Delay(700);
            } manual = true;
        } else if (m == 3) {
            input_mask = 0; input_events = 0; xQueueReset(input_rows); collecting = m;
            for (int i = 0; i < 20 && !cancel; ++i) {
                Delay(1000);
                InputRow event{};
                while (xQueueReceive(input_rows,&event,0)==pdTRUE) {
                    r=cJSON_CreateObject();
                    cJSON_AddStringToObject(r,"name",event.name);
                    cJSON_AddBoolToObject(r,"pressed",event.pressed);
                    cJSON_AddNumberToObject(r,"raw_x",event.x); cJSON_AddNumberToObject(r,"raw_y",event.y);
                    cJSON_AddNumberToObject(r,"event_ms",event.ms);
                    Append("input_event",m,r); cJSON_Delete(r);
                }
                r = cJSON_CreateObject();
                cJSON_AddNumberToObject(r, "mask", input_mask);
                cJSON_AddNumberToObject(r, "events", input_events);
                Append("input_progress", m, r); cJSON_Delete(r);
                SetMessage("采集输入中：" + std::to_string(20-i) + " 秒；已收到 " + std::to_string(input_events.load()) + " 个事件");
            }
            collecting = -1; ok = input_mask == 255;
        } else if (m == 4) {
            for (int i = 3; i > 0 && !cancel; --i) { SetMessage(std::to_string(i) + " 秒后录音，请说话"); Delay(1000); }
            SetMessage("录音三秒后自动回放，请说一句话");
            char path[192]; snprintf(path, sizeof(path), "%s/%s-%u.wav", kDir, personal_sdk::BootId(), results[m].run);
            r = cancel ? cJSON_CreateObject() : personal_sdk::RecordReplay(path);
            ok = String(r, "result") == "PASS"; Append("record_replay", m, r); cJSON_Delete(r); manual = true;
        } else if (m == 5) {
            if (job.action == 1) {
                r = Call("wifi.scan", m); ok = ReplyPass(r);
                std::vector<std::string> found;
                cJSON* item = nullptr;
                cJSON_ArrayForEach(item, cJSON_GetObjectItem(r, "aps")) {
                    const std::string ssid = String(item, "ssid");
                    if (!ssid.empty() && std::find(found.begin(), found.end(), ssid) == found.end()) found.push_back(ssid);
                }
                { std::lock_guard<std::mutex> lock(state_mutex); wifi_names = found; }
                cJSON_Delete(r); SetMessage("扫描完成，选择网络并输入密码，再点开始。");
            } else { ok = WifiConnect(job); SetMessage(ok ? "获取 IP 和断线重连均通过；尚未测试互联网服务。" : "连接失败或超时，请检查密码和网络。日志不保存密码。"); }
        } else if (m == 6) BluetoothRun(job, ok, manual);
        else if (m == 7) { r = Call("sd.roundtrip", m); ok = ReplyPass(r); cJSON_Delete(r); }
        else if (m == 8) {
            std::string initial; bool changed = false;
            for (int i = 0; i < 20 && !cancel; ++i) {
                r = Call("power.status", m); ok = ReplyPass(r) && ok;
                auto* battery = cJSON_GetObjectItem(r, "battery");
                bool charging = cJSON_IsTrue(cJSON_GetObjectItem(battery, "charging"));
                std::string state = charging ? "充电" : "非充电";
                if (i == 0) initial = state; else changed |= initial != state;
                SetMessage("电池：" + state + "，请拔插 USB；剩余 " + std::to_string(20-i) + " 秒");
                cJSON_Delete(r); Delay(1000);
            } ok = ok && changed;
        } else if (m == 9) {
            int first[3] = {}, maximum = 0;
            for (int i = 0; i < 50 && !cancel; ++i) {
                int a[3]; bool read = Sc7a20h::GetInstance().ReadAccelMg(a[0], a[1], a[2]); ok &= read;
                if (read) for (int axis = 0; axis < 3; ++axis) {
                    if (i == 0) first[axis] = a[axis];
                    maximum = std::max(maximum, std::abs(a[axis] - first[axis]));
                }
                if (i % 5 == 0) { r = Call("imu.read", m); cJSON_Delete(r); }
                Delay(200);
            }
            ok &= maximum > 150;
            SetMessage("最大三轴变化 " + std::to_string(maximum) + " mg；阈值 150 mg");
        } else if (m == 10) {
            if (job.action == 2) {
                struct tm value{}; int year, month, day, hour, minute, second;
                ok = sscanf(job.text, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6;
                ok = ok && year >= 2024 && year <= 2099 && month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
                if (ok) { value.tm_year=year-1900; value.tm_mon=month-1; value.tm_mday=day; value.tm_hour=hour; value.tm_min=minute; value.tm_sec=second; value.tm_isdst=-1;
                    time_t normalized = mktime(&value); ok = normalized != -1 && value.tm_mon==month-1 && value.tm_mday==day && Pcf8563::GetInstance().SetTime(value); }
            }
            struct tm a{}, b{}; bool valid_a=false, valid_b=false;
            ok = Pcf8563::GetInstance().GetTime(a, &valid_a) && ok;
            Delay(2200); ok = Pcf8563::GetInstance().GetTime(b, &valid_b) && ok;
            const double elapsed = difftime(mktime(&b), mktime(&a));
            ok = ok && valid_a && valid_b && elapsed >= 1 && elapsed <= 4;
            r = Call("rtc.status", m); cJSON_Delete(r);
        } else if (m == 11) {
            r = cJSON_CreateObject();
            cJSON_AddBoolToObject(r, "secure_boot", esp_secure_boot_enabled());
            cJSON_AddBoolToObject(r, "flash_encryption", esp_flash_encryption_enabled());
            const auto* partition = esp_ota_get_running_partition();
            cJSON_AddNumberToObject(r, "running_address", partition ? partition->address : 0);
            cJSON_AddStringToObject(r, "full_restore", "NOT_EXECUTED_REQUIRES_MAC");
            cJSON_AddStringToObject(r, "original_backup_sha256", "5b7986151b18163f9087611edfedd563c49e37c32c12d999d0e0fa3d82a1cfaf");
            ok = partition && partition->address == 0x80000 && !esp_secure_boot_enabled() && !esp_flash_encryption_enabled();
            Append("recovery_preflight", m, r); cJSON_Delete(r);
            SetMessage("本机预检完成。完整恢复未执行。连接 Mac 后使用恢复工具检查原厂 16 MiB 备份与下载入口；不要依赖当前应用恢复自身。");
        } else if (m == 12) {
            const int iterations=job.action==3 ? 480 : 120;
            const int seconds=job.action==3 ? 60 : 5;
            for (int i = 0; i < iterations && !cancel; ++i) {
                r = Call("status", m); ok = ReplyPass(r) && ok; cJSON_Delete(r);
                r = Call("imu.read", m); ok = ReplyPass(r) && ok; cJSON_Delete(r);
                SetMessage("稳定性测试：" + std::to_string(i*seconds) + "/" + std::to_string(iterations*seconds) + " 秒"); Delay(seconds*1000);
            }
        }
        memset(job.secret, 0, sizeof(job.secret));
        if (cancel) { ok = false; SetMessage("已停止；部分日志已保留，可重新测试。"); }
        const char* automatic = cancel ? "已停止" : (ok ? "PASS" : "FAIL");
        if ((m == 5 || m == 6) && job.action == 1 && ok) automatic = "扫描完成";
        if (m == 11 && ok) automatic = "仅预检通过";
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            results[m].automatic = automatic;
            if (manual) results[m].manual = "待你确认";
            if (m != 5 && m != 6 && m != 8 && m != 9 && m != 11 && !cancel)
                message = std::string(titles[m]) + "：" + automatic + (manual ? "，请确认实际效果" : "，已记录");
            ++revision;
        }
        r = cJSON_CreateObject(); cJSON_AddStringToObject(r, "automatic", automatic);
        cJSON_AddStringToObject(r, "manual", manual ? "WAITING_USER" : "NOT_REQUIRED");
        Append("finished", m, r); cJSON_Delete(r);
        SaveResults();
        collecting = -1;
        { std::lock_guard<std::mutex> lock(state_mutex); busy=false; ++revision; }
    }
}

bool Enqueue(int m, int action, const char* text = "", const char* secret = "") {
    if (!jobs || m < 0 || m >= kCount || personal_sdk::AudioBusy() || busy.exchange(true)) return false;
#ifdef CONFIG_PAPER_CORE_APP
    if (paper_bluetooth::Snapshot().busy) { busy=false; return false; }
#endif
    cancel = false;
    Job job{}; job.module=m; job.action=action;
    snprintf(job.text, sizeof(job.text), "%s", text); snprintf(job.secret, sizeof(job.secret), "%s", secret);
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        results[m].run = ++next_run; results[m].automatic = "运行中"; results[m].manual = "未确认";
        message = prompts[m]; ++revision;
    }
    SaveResults();  // A reset must retain an interrupted run, not an older PASS.
    if (xQueueSend(jobs, &job, 0) != pdTRUE) { busy=false; return false; }
    memset(job.secret, 0, sizeof(job.secret));
    return true;
}

lv_obj_t* Label(lv_obj_t* parent, const char* text, int x, int y, int w) {
    auto* label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y); lv_obj_set_width(label, w);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, fontpack_lv_font_ui(), 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_label_set_text(label, text); return label;
}
void Render(int target);
void OnButton(lv_event_t* e);
void Button(lv_obj_t* parent, const char* text, int x, int y, int w, intptr_t action) {
    auto* button = lv_button_create(parent);
    lv_obj_set_pos(button,x,y); lv_obj_set_size(button,w,54);
    lv_obj_set_style_bg_color(button,lv_color_white(),0);
    lv_obj_set_style_border_color(button,lv_color_black(),0);
    lv_obj_set_style_border_width(button,2,0);
    auto* label = Label(button,text,0,0,w-12); lv_obj_center(label);
    lv_obj_add_event_cb(button,OnButton,LV_EVENT_CLICKED,reinterpret_cast<void*>(action));
}
void Confirm(bool pass) {
    if (page < 0 || busy || results[page].manual != "待你确认") return;
    auto* row = cJSON_CreateObject();
    cJSON_AddStringToObject(row,"physical",pass ? "PASS" : "FAIL");
    cJSON_AddStringToObject(row,"source","device_touch_confirmation");
    const bool saved = Append("user_observation",page,row); cJSON_Delete(row);
    { std::lock_guard<std::mutex> lock(state_mutex);
      results[page].manual=pass ? "PASS" : "FAIL";
      message=saved ? "你的确认已保存到 SD 日志" : "确认已接收，但日志保存失败；请检查 SD 卡"; ++revision; }
    SaveResults();
}
void OnButton(lv_event_t* e) {
    intptr_t action = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    if (action == 110) { if (!busy) inkdesk_app::Open(); return; }
    if (action == 100) { if (!busy) Render(-1); return; }
    if (action == 107) { if (!busy) Render(-2); return; }
    if (action == 108) { if (!busy) Render(page==-2 ? -3 : -2); return; }
    if (action == 104) { cancel=true; return; }
    if (action >= 0 && action < kCount) { if (!busy) Render(action); return; }
    if (page < 0) return;
    if (action == 102 || action == 103) { Confirm(action==102); return; }
    std::string text, secret;
    if (page == 5 || page == 6) {
        int index = choice ? lv_dropdown_get_selected(choice) : 0;
        if (page==5) { std::lock_guard<std::mutex> lock(state_mutex); if (index < (int)wifi_names.size()) text=wifi_names[index]; }
        else { std::lock_guard<std::mutex> lock(bt_mutex); if (index < (int)bt_addresses.size()) text=bt_addresses[index]; }
        if (password) secret=lv_textarea_get_text(password);
    }
    if (page == 10 && time_field) text=lv_textarea_get_text(time_field);
    const int mode = action == 105 ? 1 : (action == 106 ? 2 : (action==109 ? 3 : 0));
    if (Enqueue(page,mode,text.c_str(),secret.c_str())) {
        if (password) lv_textarea_set_text(password,"");
        std::fill(secret.begin(),secret.end(),'\0');
    }
}
void Focus(lv_event_t* e) {
    if (keyboard) { lv_keyboard_set_textarea(keyboard,static_cast<lv_obj_t*>(lv_event_get_target(e))); lv_obj_remove_flag(keyboard,LV_OBJ_FLAG_HIDDEN); }
}
void KeyboardDone(lv_event_t* e) {
    if (lv_event_get_code(e)==LV_EVENT_READY || lv_event_get_code(e)==LV_EVENT_CANCEL)
        lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
}
void Render(int target) {
    if(auto*d=LVAdapterDisplay::Instance();d&&d->IsPaperPresenting()){requested_page=target;open_requested=true;return;}
    if (busy) return;
    visible=true;
    page=target; status_label=summary_label=choice=password=time_field=keyboard=nullptr;
    auto* previous=lv_screen_active();
    root=lv_obj_create(nullptr); lv_obj_set_style_bg_color(root,lv_color_white(),0);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(root);
    if (previous && previous!=root) lv_obj_delete(previous);
    Label(root,target<0 ? "Metalio 硬件自检" : titles[target],18,18,444);
    Label(root,esp_app_get_description()->version,18,58,444);
    if (target==-2 || target==-3) {
        const int begin=target==-2 ? 0 : 7;
        std::lock_guard<std::mutex> lock(state_mutex);
        for (int i=begin;i<std::min(begin+7,kCount);++i) {
            const std::string text=std::string(titles[i])+"："+results[i].automatic+" / "+results[i].manual;
            Label(root,text.c_str(),18,110+(i-begin)*72,444);
        }
        Button(root,"翻页",18,645,214,108); Button(root,"返回",244,645,214,100);
        return;
    } else if (target<0) {
        for (int i=0;i<kCount;++i) Button(root,titles[i],18+(i%2)*226,108+(i/2)*65,214,i);
        Button(root,"验收记录",244,498,214,107);
        Button(root,"返回纸间 InkDesk",18,563,444,110);
        status_label=Label(root,"",18,625,444);
        summary_label=Label(root,"",18,715,444);
    } else {
        Label(root,prompts[target],18,100,444);
        summary_label=Label(root,"",18,215,444);
        status_label=Label(root,"",18,270,444);
        if (target==0) {
            for (int i=0;i<4;++i) {
                auto* block=lv_obj_create(root); lv_obj_set_pos(block,18+i*110,440);
                lv_obj_set_size(block,104,80); lv_obj_set_style_radius(block,0,0);
                lv_obj_set_style_bg_color(block,i%2 ? lv_color_white() : lv_color_black(),0);
                lv_obj_set_style_bg_opa(block,LV_OPA_COVER,0);
                lv_obj_set_style_border_color(block,lv_color_black(),0); lv_obj_set_style_border_width(block,2,0);
            }
            Label(root,"0123456789  ABC  测试",18,530,444);
        }
        if (target==5 || target==6) {
            choice=lv_dropdown_create(root); lv_obj_set_pos(choice,18,430); lv_obj_set_size(choice,300,50);
            lv_obj_set_style_text_font(choice,fontpack_lv_font_ui(),0);
            lv_dropdown_set_options(choice,"先扫描再选择");
            Button(root,"扫描",330,430,130,105);
            if (target==5) {
                password=lv_textarea_create(root); lv_obj_set_pos(password,18,490); lv_obj_set_size(password,444,55);
                lv_textarea_set_one_line(password,true); lv_textarea_set_password_mode(password,true);
                lv_textarea_set_max_length(password,63); lv_textarea_set_placeholder_text(password,"Wi-Fi password");
                lv_obj_add_event_cb(password,Focus,LV_EVENT_FOCUSED,nullptr);
            }
        }
        if (target==10) {
            time_field=lv_textarea_create(root); lv_obj_set_pos(time_field,18,450); lv_obj_set_size(time_field,444,55);
            lv_textarea_set_one_line(time_field,true); lv_textarea_set_max_length(time_field,19);
            lv_textarea_set_placeholder_text(time_field,"2026-09-16 12:00:00");
            lv_obj_add_event_cb(time_field,Focus,LV_EVENT_FOCUSED,nullptr);
            Button(root,"写入时间并检验",18,515,444,106);
        }
        if (target==12) Button(root,"运行 8 小时（可停止）",18,510,444,109);
        Button(root,"开始",18,580,214,101); Button(root,"停止",244,580,214,104);
        Button(root,"效果正常",18,645,214,102); Button(root,"效果异常",244,645,214,103);
        Button(root,"返回自检首页",18,710,444,100);
        if (target==5 || target==10) {
            keyboard=lv_keyboard_create(root); lv_obj_set_size(keyboard,480,300);
            lv_obj_align(keyboard,LV_ALIGN_BOTTOM_MID,0,0); lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_event_cb(keyboard,KeyboardDone,LV_EVENT_ALL,nullptr);
        }
    }
    shown_revision=~0u;
}
void Tick(lv_timer_t*) {
    if(auto*d=LVAdapterDisplay::Instance();d&&d->IsPaperPresenting())return;
    if (!busy && open_requested.exchange(false)) Render(requested_page);
    if (!busy && home_edge.exchange(false) && visible) Render(-1);
    if (!visible) return;
    if (!root || !lv_obj_is_valid(root) || lv_screen_active()!=root) return;
    if (!status_label || !lv_obj_is_valid(status_label) || !summary_label || !lv_obj_is_valid(summary_label)) return;
    std::lock_guard<std::mutex> lock(state_mutex);
    if (shown_revision==revision) return;
    shown_revision=revision;
    lv_label_set_text(status_label,message.c_str());
    std::string summary;
    if (page>=0) summary="自动："+results[page].automatic+"\n人工："+results[page].manual;
    else summary="测试日志：SD/sdk-selftest\n底部 HOME 可回到自检入口";
    { std::lock_guard<std::mutex> logs(log_mutex); if (!log_ok) summary+="\n日志未保存：检查 SD 或空间"; }
    lv_label_set_text(summary_label,summary.c_str());
    if (choice && !busy) {
        std::string options;
        if (page==5) { for (const auto& s:wifi_names) { if (!options.empty()) options+='\n'; options+=s; } }
        else { std::lock_guard<std::mutex> bt(bt_mutex); for (const auto& s:bt_addresses) { if (!options.empty()) options+='\n'; options+=s; } }
        if (!options.empty()) lv_dropdown_set_options(choice,options.c_str());
    }
}
}

bool Busy() { return busy; }
bool Visible() { return visible; }
void Hide() { visible=false; }
void Start() {
    mkdir(kDir,0755);
    LoadResults();
    log_file=std::string(kDir)+"/"+personal_sdk::BootId()+".jsonl";
    Append("boot",-1,nullptr);
    jobs=xQueueCreate(1,sizeof(Job));
    input_rows=xQueueCreate(64,sizeof(InputRow));
    if (!jobs || !input_rows || xTaskCreate(Worker,"sdk_selftest",16384,nullptr,3,nullptr)!=pdPASS) { SetMessage("自检任务启动失败"); return; }
    if (esp_lv_adapter_lock(2000)==ESP_OK) { lv_timer_create(Tick,300,nullptr); esp_lv_adapter_unlock(); }
}
void Input(const char* name, bool pressed, int x, int y) {
    if (!name) return;
    if (visible && pressed && !strcmp(name,"vk_home") && collecting!=3) home_edge=true;
    if (collecting!=3) return;
    InputRow row{}; snprintf(row.name,sizeof(row.name),"%s",name);
    row.pressed=pressed; row.x=x; row.y=y; row.ms=esp_timer_get_time()/1000;
    if (input_rows) xQueueSend(input_rows,&row,0);
    if (!pressed) return;
    const char* keys[]={"screen","vk_home","vk_next","vk_prev","volume_up","volume_down","boot","power"};
    for (int i=0;i<8;++i) if (!strcmp(name,keys[i])) input_mask.fetch_or(1u<<i);
    input_events++;
    (void)x; (void)y;
}
void BluetoothRx(const uint8_t* bytes, size_t size) {
    std::lock_guard<std::mutex> lock(bt_mutex);
    for (size_t i=0;i<size;++i) {
        char c=static_cast<char>(bytes[i]);
        if (c=='\n' || c=='\r') {
            if (bt_partial.find("CONNECT SUCCESS")!=std::string::npos) bt_connected=true;
            if (bt_partial.rfind("AT+BT:",0)==0 && bt_partial.size()>=18) {
                std::string address=bt_partial.substr(6,12);
                if (address.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos &&
                    bt_addresses.size()<12 && std::find(bt_addresses.begin(),bt_addresses.end(),address)==bt_addresses.end()) bt_addresses.push_back(address);
            }
            if (bt_history.size()+bt_partial.size()<1800) bt_history+=bt_partial+"\n";
            bt_partial.clear();
        } else if (bt_partial.size()<180 && c>=32 && c<127) bt_partial+=c;
    }
}
bool Handle(const char* cmd,cJSON* request,cJSON* reply) {
    if (strncmp(cmd,"selftest.",9)) return false;
    if (!strcmp(cmd,"selftest.open")) {
        requested_page=-1;
        const std::string module=String(request,"module");
        for (int i=0;i<kCount;++i) if (module==names[i]) requested_page=i;
        open_requested=true;
    }
    else if (!strcmp(cmd,"selftest.status")) {
        std::lock_guard<std::mutex> lock(state_mutex);
        cJSON_AddBoolToObject(reply,"busy",busy);
        cJSON_AddStringToObject(reply,"message",message.c_str());
        auto* array=cJSON_AddArrayToObject(reply,"tests");
        for (int i=0;i<kCount;++i) { auto* row=cJSON_CreateObject();
            cJSON_AddStringToObject(row,"module",names[i]); cJSON_AddNumberToObject(row,"run",results[i].run);
            cJSON_AddStringToObject(row,"automatic",results[i].automatic.c_str());
            cJSON_AddStringToObject(row,"manual",results[i].manual.c_str()); cJSON_AddItemToArray(array,row); }
        std::lock_guard<std::mutex> logs(log_mutex);
        cJSON_AddStringToObject(reply,"log_file",log_file.c_str()); cJSON_AddBoolToObject(reply,"log_saved",log_ok);
        cJSON_AddNumberToObject(reply,"log_bytes",log_bytes);
    } else if (!strcmp(cmd,"selftest.run")) {
        if(auto*d=LVAdapterDisplay::Instance();d&&d->IsPaperPresenting()){cJSON_AddStringToObject(reply,"error","display_busy");return true;}
        std::string module=String(request,"module");
        auto found=std::find_if(std::begin(names),std::end(names),[&](const char* n){return module==n;});
        if (found==std::end(names)) cJSON_AddStringToObject(reply,"error","unknown_test");
        else {
            int m=found-std::begin(names);
            if (!Enqueue(m,(m==5 || m==6) ? 1 : 0)) cJSON_AddStringToObject(reply,"error","selftest_busy");
        }
    } else if (!strcmp(cmd,"selftest.logs")) {
        auto* array=cJSON_AddArrayToObject(reply,"files");
        DIR* dir=opendir(kDir); if (dir) {
            int count=0; dirent* entry;
            while ((entry=readdir(dir)) && count<24) {
                std::string name=entry->d_name;
                if (name.size()>6 && name.substr(name.size()-6)==".jsonl") { cJSON_AddItemToArray(array,cJSON_CreateString(name.c_str())); ++count; }
            } closedir(dir);
        }
    } else if (!strcmp(cmd,"selftest.log.read")) {
        std::string name=String(request,"file");
        auto* offset=cJSON_GetObjectItem(request,"offset");
        if (name.empty() || name.find_first_not_of("0123456789abcdef.jsonl")!=std::string::npos ||
            name.find("..")!=std::string::npos || !cJSON_IsNumber(offset) || offset->valuedouble<0 ||
            offset->valuedouble>2*1024*1024 || offset->valuedouble!=offset->valueint) {
            cJSON_AddStringToObject(reply,"error","invalid_log_range");
        } else {
            std::lock_guard<std::mutex> lock(log_mutex);
            FILE* f=fopen((std::string(kDir)+"/"+name).c_str(),"rb");
            if (!f) cJSON_AddStringToObject(reply,"error","log_not_found");
            else { fseek(f,offset->valueint,SEEK_SET); unsigned char buf[768]; size_t count=fread(buf,1,sizeof(buf),f);
                std::string hex; static const char chars[]="0123456789abcdef";
                for (size_t i=0;i<count;++i) { hex+=chars[buf[i]>>4]; hex+=chars[buf[i]&15]; }
                cJSON_AddStringToObject(reply,"hex",hex.c_str()); cJSON_AddNumberToObject(reply,"bytes",count);
                cJSON_AddBoolToObject(reply,"eof",feof(f)); fclose(f); }
        }
    } else cJSON_AddStringToObject(reply,"error","unknown_selftest_command");
    return true;
}
}
