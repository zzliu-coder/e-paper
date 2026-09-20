#include "metalio_hardware.hpp"
#include "haptic_feedback.h"
#include "power_policy.h"
#include "maintenance.hpp"
#include "usb_virtual_disk.h"
#include "network_service.hpp"
#include "bluetooth_service.hpp"
namespace paper {
    BluetoothState MetalioHardware::bluetooth()const{return paper_bluetooth::Snapshot();}
    Status MetalioHardware::bluetoothCommand(const std::string&a,const std::string&v){return paper_bluetooth::Command(a,v);}
    NetworkState MetalioHardware::network()const {return paper_network::Snapshot();}
    Status MetalioHardware::networkCommand(const std::string&a,const std::string&s,const std::string&p){return paper_network::Command(a,s,p);}
    bool MetalioHardware::supports(const std::string&k)const {
#ifdef CONFIG_PAPER_GRAY_EXPERIMENT
        if(k=="gray4_experiment")return true;
#endif
        if(k=="wifi")return true;
        if(k=="bt_audio_mode")return bluetooth().available;
#ifdef CONFIG_PAPER_GRAY_VERIFIED
        if(k=="gray4_verified")return true;
#endif
        return k=="haptic"||k=="cpu_idle"||k=="standby_s"||k=="shutdown_s"||k=="network_grace_s"||(k=="usb_disk"&&UsbVirtualDisk::GetInstance().IsSupported())||hooks_.count(k);
    }
    Status MetalioHardware::get(const std::string&k,std::string&v) {
        auto&p=PowerPolicy::GetInstance();
        if(k=="haptic")v=HapticIsEnabled()?"on":"off";
        else if(k=="wifi")v=network().connected?"on":"off";
        else if(k=="bt_audio_mode")v=std::to_string(bluetooth().mode);
        else if(k=="usb_disk")v=paper_maintenance::UsbOwned()?"on":"off";
        else if(k=="cpu_idle")v=std::to_string(p.GetIdleCpuMhz());
        else if(k=="standby_s")v=std::to_string(p.GetUserIdleToStandbySec());
        else if(k=="shutdown_s")v=std::to_string(p.GetStandbyToShutdownSec());
        else if(k=="network_grace_s")v=std::to_string(p.GetNetGraceSec());
        else {
            auto it=hooks_.find(k);
            return it!=hooks_.end()&&it->second.get?it->second.get(v):Status::fail(Error::Unavailable,"硬件读取未绑定");
        }
        return {
        };
    }
    Status MetalioHardware::set(const std::string&k,const std::string&v) {
        auto&p=PowerPolicy::GetInstance();
        if(k=="haptic")HapticSetEnabled(v=="on");
        else if(k=="bt_audio_mode")return bluetoothCommand("mode",v);
        else if(k=="usb_disk")return paper_maintenance::Action(v=="on"?"maintenance-usb":"maintenance-usb-exit");
        else if(k=="cpu_idle")p.SetIdleCpuMhz(std::stoi(v));
        else if(k=="standby_s")p.SetUserIdleToStandbySec(std::stoi(v));
        else if(k=="shutdown_s")p.SetStandbyToShutdownSec(std::stoi(v));
        else if(k=="network_grace_s")p.SetNetGraceSec(std::stoi(v));
        else {
            auto it=hooks_.find(k);
            return it!=hooks_.end()&&it->second.set?it->second.set(v):Status::fail(Error::Unavailable,"硬件设置未绑定");
        }
        return {
        };
    }
    Status MetalioHardware::action(const std::string&k) {
        if(k.rfind("maintenance-",0)==0&&action_)return action_(k);
        if(k=="poweroff") {
            PowerPolicy::GetInstance().RequestPowerOff();
            return {
            };
        }
        return Status::fail(Error::Unsupported,"硬件动作未绑定");
    }
}
