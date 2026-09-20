#include "maintenance.hpp"
#include "usb_virtual_disk.h"
#include "SdCardManager.hpp"
#include "power_policy.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "esp_secure_boot.h"
#include "esp_flash_encrypt.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"
#include <atomic>
#include <mutex>
#include <sys/stat.h>
#include <cstring>

namespace paper_maintenance {
namespace {
paper::Runtime* app=nullptr;
std::atomic<bool> usb{false},awake{false};
int64_t usbRequested=0;
std::mutex mutex;
std::string phase="idle", detail="已就绪", candidateHash, candidateVersion;
size_t candidateBytes=0;
std::atomic<int> progress{0};
std::atomic<int> bootState{-1};
using paper::Status;using paper::Error;
void state(const std::string&p,const std::string&d){std::lock_guard<std::mutex> l(mutex);phase=p;detail=d;}
Status fail(const std::string&d){state("failed",d);return Status::fail(Error::BackendFailure,d);}
constexpr auto imagePath="/sdcard/paper/updates/update.bin";
constexpr auto manifestPath="paper/updates/update.json";
std::string str(cJSON*j,const char*k){auto*x=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(x)?x->valuestring:"";}
Status validate() {
    candidateHash.clear();candidateVersion.clear();candidateBytes=0;
    if(esp_secure_boot_enabled()||esp_flash_encryption_enabled())return fail("安全保护已启用，已锁定本地更新");
    std::vector<uint8_t> bytes;auto st=app->store().read(manifestPath,bytes,4096);if(!st)return fail("存储卡缺少更新清单 update.json");
    auto*j=cJSON_ParseWithLength(reinterpret_cast<char*>(bytes.data()),bytes.size());
    if(!j)return fail("更新清单格式错误");
    const auto sha=str(j,"sha256"),board=str(j,"board"),layout=str(j,"layout"),version=str(j,"version");
    auto*n=cJSON_GetObjectItem(j,"size");auto*schema=cJSON_GetObjectItem(j,"schema");
    const bool valid=cJSON_IsNumber(schema)&&schema->valuedouble==1&&cJSON_IsNumber(n)&&n->valuedouble>=1024&&n->valuedouble<=5*1024*1024&&n->valuedouble==double(size_t(n->valuedouble))&&sha.size()==64&&sha.find_first_not_of("0123456789abcdef")==std::string::npos&&board=="metalio_eink4"&&layout=="metalio-16m-v1-5m"&&!version.empty()&&version.size()<32;
    size_t length=valid?size_t(n->valuedouble):0;cJSON_Delete(j);
    if(!valid)return fail("更新清单的机型、分区、大小或校验值不符");
    auto*run=esp_ota_get_running_partition();auto*next=esp_ota_get_next_update_partition(nullptr);
    if(!run||!next||next->address==run->address||next->size!=5*1024*1024||run->size!=5*1024*1024||!((run->address==0x80000&&next->address==0x580000)||(run->address==0x580000&&next->address==0x80000)))return fail("固件分区布局与预期不符，停止更新");
    struct stat sb{};if(stat(imagePath,&sb)||!S_ISREG(sb.st_mode)||size_t(sb.st_size)!=length)return fail("固件大小不符");
    FILE*f=fopen(imagePath,"rb");if(!f)return fail("无法打开固件文件");
    esp_image_header_t hdr{};esp_image_segment_header_t seg{};esp_app_desc_t desc{};
    const bool read=fread(&hdr,1,sizeof hdr,f)==sizeof hdr&&fread(&seg,1,sizeof seg,f)==sizeof seg&&fread(&desc,1,sizeof desc,f)==sizeof desc;fclose(f);
    if(!read||hdr.magic!=ESP_IMAGE_HEADER_MAGIC||hdr.chip_id!=ESP_CHIP_ID_ESP32S3||desc.magic_word!=ESP_APP_DESC_MAGIC_WORD||strnlen(desc.project_name,sizeof desc.project_name)!=7||memcmp(desc.project_name,"xiaozhi",7)||strnlen(desc.version,sizeof desc.version)!=version.size()||memcmp(desc.version,version.data(),version.size()))return fail("固件的芯片、工程或版本不符");
    std::string actual;st=paper::fileSha256(imagePath,actual,5*1024*1024);if(!st||actual!=sha)return fail("固件完整性校验失败");
    candidateHash=sha;candidateVersion=version;candidateBytes=length;state("checked","更新包校验通过："+version);return {};
}
Status install(){
    // Revalidate immediately before erase; do not trust an earlier check or filename.
    auto st=validate();if(!st)return st;
    auto*next=esp_ota_get_next_update_partition(nullptr);
    FILE*f=fopen(imagePath,"rb");if(!f)return fail("无法重新打开固件");
    esp_ota_handle_t handle=0;auto err=esp_ota_begin(next,candidateBytes,&handle);
    if(err!=ESP_OK){fclose(f);return fail(std::string("启动更新失败：")+esp_err_to_name(err));}
    state("writing","正在写入备用分区，请保持供电");progress=0;
    uint8_t buf[4096],digest[32];mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);mbedtls_sha256_starts(&sha,0);
    size_t done=0;
    while(done<candidateBytes){size_t n=fread(buf,1,std::min(sizeof buf,candidateBytes-done),f);if(!n){err=ESP_FAIL;break;}mbedtls_sha256_update(&sha,buf,n);err=esp_ota_write(handle,buf,n);if(err!=ESP_OK)break;done+=n;progress=int(done*100/candidateBytes);vTaskDelay(1);}
    fclose(f);mbedtls_sha256_finish(&sha,digest);mbedtls_sha256_free(&sha);
    char hex[65];for(int i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);
    if(err!=ESP_OK||done!=candidateBytes||candidateHash!=hex){esp_ota_abort(handle);return fail("写入或校验失败，启动分区未改变");}
    err=esp_ota_end(handle);if(err!=ESP_OK)return fail(std::string("固件验证失败：")+esp_err_to_name(err));
    // Independent raw partition readback, before changing otadata.
    mbedtls_sha256_init(&sha);mbedtls_sha256_starts(&sha,0);
    for(size_t off=0;off<candidateBytes;){size_t n=std::min(sizeof buf,candidateBytes-off);err=esp_partition_read(next,off,buf,n);if(err!=ESP_OK)break;mbedtls_sha256_update(&sha,buf,n);off+=n;vTaskDelay(1);}
    mbedtls_sha256_finish(&sha,digest);mbedtls_sha256_free(&sha);for(int i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);
    if(err!=ESP_OK||candidateHash!=hex)return fail("写回核验失败，启动分区未改变");
    err=esp_ota_set_boot_partition(next);if(err!=ESP_OK)return fail(std::string("选择启动分区失败：")+esp_err_to_name(err));
    state("ready-reboot","写回核验通过，重启后进入："+candidateVersion);return {};
}
}
void Init(paper::Runtime*r){app=r;UsbVirtualDisk::GetInstance().Init();}
bool UsbOwned(){return usb.load();}
std::string Text(){std::lock_guard<std::mutex>l(mutex);return std::string(esp_app_get_description()->version)+"\n"+(awake?"开发模式：保持唤醒":"开发模式：正常休眠")+"\n"+detail;}
std::string Json(){std::lock_guard<std::mutex>l(mutex);auto*run=esp_ota_get_running_partition();auto*boot=esp_ota_get_boot_partition();return "{\"phase\":"+paper::jsonString(phase)+",\"detail\":"+paper::jsonString(detail)+",\"progress\":"+std::to_string(progress)+",\"usb_owned\":"+(usb?"true":"false")+",\"awake\":"+(awake?"true":"false")+",\"running_address\":"+std::to_string(run?run->address:0)+",\"boot_address\":"+std::to_string(boot?boot->address:0)+",\"boot_state_before_accept\":"+std::to_string(bootState)+",\"automatic_rollback\":\"NOT_PROVEN\",\"efuse_write\":false}";}
Status Action(const std::string&a){
    if(!app)return fail("系统正在启动");
    auto&disk=UsbVirtualDisk::GetInstance();
    if(a=="maintenance-usb-exit"){
        if(disk.IsGadgetActive()||disk.IsBusy())return fail("请先在电脑上安全弹出磁盘");
        if(usb){usb=false;app->releaseUsb();}return {};
    }
    if(usb)return Status::fail(Error::Busy,"电脑正在使用存储卡");
    if(a=="maintenance-usb"){
        if(!disk.IsSupported()||disk.IsBusy()||disk.IsGadgetActive())return fail("USB 磁盘不可用或正在忙碌");
        auto st=app->prepareUsb();if(!st)return st;
        usb=true;usbRequested=esp_timer_get_time();state("usb","电脑安全弹出后，设备自动接回存储卡");disk.Toggle();return {};
    }
    if(a=="maintenance-awake"){
        if(!awake.exchange(!awake)){PowerPolicy::GetInstance().Acquire(PowerNeed::StandbyInhibit);}else PowerPolicy::GetInstance().Release(PowerNeed::StandbyInhibit);
        state("idle",awake?"已开启开发保持唤醒":"已恢复正常电源策略");return {};
    }
    if(a=="maintenance-reboot"){esp_restart();return {};}
    if(a=="maintenance-resources"||a=="maintenance-deep-resources"){
        const bool deep=a=="maintenance-deep-resources";
        if(!SdCardManager::GetInstance().IsMounted()&&!SdCardManager::GetInstance().Mount())return fail("存储卡挂载失败，未进行格式化");
        auto st=app->store().initialize();if(!st)return fail(st.message);
        state("checking","正在检查字体与输入法资源");
        paper::PackedFonts fonts(app->store(),{});
        int checked=0;progress=0;
        const int64_t started=esp_timer_get_time();
        for(int w:{400,500,700})for(int px=16;px<=40;++px){
            if(esp_timer_get_time()-started>(deep?900:120)*1000000LL)return fail("字体检查超时，存储资源未改动");
            state("checking","正在核验字体："+std::to_string(w)+" / "+std::to_string(px)+" px");
            if(!deep){
                std::string path;st=app->store().path("paper/fonts/misans-"+std::to_string(w)+"-"+std::to_string(px)+".pgf",path,false);
                if(!st)return fail(st.message);
                FILE*f=fopen(path.c_str(),"rb");uint8_t header[80]{};
                bool read=f&&fread(header,1,80,f)==80;if(f)fclose(f);struct stat sb{};
                auto u16=[&](int p){return uint32_t(header[p])|(uint32_t(header[p+1])<<8);};
                auto u32=[&](int p){return u16(p)|(u16(p+2)<<16);};
                if(!read||stat(path.c_str(),&sb)||memcmp(header,"PGF1",4)||u16(4)!=1||u16(6)!=unsigned(px)||u16(8)!=unsigned(w)||u16(10)!=2||!u32(12)||u32(12)>65536||u32(16)!=80||u32(20)!=80+uint64_t(u32(12))*24||uint64_t(u32(20))+u32(64)!=uint64_t(sb.st_size)||sb.st_size>16*1024*1024||(u32(72)&0x80000000)||paper::crc32(header,76)!=u32(76))return fail("字体规格检查失败："+std::to_string(w)+" / "+std::to_string(px));
            }
            paper::FontSpec spec{px,w,false};spec.uiOnly=!deep;
            st=fonts.validate(spec);if(!st)return fail("字体 "+std::to_string(w)+"/"+std::to_string(px)+": "+st.message);
            fonts.clear();progress=++checked*100/75;vTaskDelay(1);
        }
        for(auto file:{"paper/ime/pinyin.pim","paper/ime/nine.pim"}){std::string path;st=app->store().path(file,path,false);if(!st)return fail(std::string("缺少资源 ")+file);}
        paper::PinyinDictionary dict(app->store());paper::TextSession input(dict);
        st=input.begin("",192);if(st)st=input.mode(paper::InputMode::Pinyin26);
        for(char c:std::string("nihao"))if(st)st=input.key(c);
        if(!st||input.candidates().empty())return fail("输入法资源检查失败");
        state("resources-ok",deep?"阅读字库完整检查通过，输入法已就绪":"界面字库和阅读规格检查通过，输入法已就绪");return {};
    }
    if(a=="maintenance-check"||a=="maintenance-install"){
        paper::LeaseGuard sd,hold;auto st=sd.acquire(app->resources(),"update","firmware-update");if(!st)return st;
        st=hold.acquire(app->resources(),"awake","firmware-update");if(!st)return st;
        return a=="maintenance-check"?validate():install();
    }
    return Status::fail(Error::Unsupported,"未知维护操作");
}
bool Tick(){
    if(!usb||esp_timer_get_time()-usbRequested<2000000)return false;
    auto&d=UsbVirtualDisk::GetInstance();
    if(d.IsGadgetActive()||d.IsBusy())return false;
    usb=false;auto st=app->releaseUsb();state(st?"idle":"failed",st?"存储卡已接回，USB 调试已恢复":st.message);return true;
}
void AcceptBoot(){
    auto*p=esp_ota_get_running_partition();esp_ota_img_states_t s;
    if(p&&esp_ota_get_state_partition(p,&s)==ESP_OK){bootState=int(s);if(s==ESP_OTA_IMG_PENDING_VERIFY){auto e=esp_ota_mark_app_valid_cancel_rollback();state(e==ESP_OK?"boot-accepted":"failed",e==ESP_OK?"系统与维护界面已就绪":esp_err_to_name(e));}}
}
}
