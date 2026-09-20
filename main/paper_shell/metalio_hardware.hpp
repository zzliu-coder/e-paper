#pragma once
#include "paper/services.hpp"
#include <functional>
namespace paper {
    // Device-specific services can register real read/write hooks. Unregistered
    // settings remain unavailable; there is no success-returning fallback.
    struct SettingHooks {
        std::function<Status(std::string&)>get;
        std::function<Status(const std::string&)>set;
    };
    class MetalioHardware final:public Hardware {
        std::map<std::string,SettingHooks>hooks_;
        std::function<Status(const std::string&)> action_;
        public: void bindAction(std::function<Status(const std::string&)> fn) {action_=std::move(fn);}
        public: void bind(std::string key,SettingHooks hooks) {
            hooks_[std::move(key)]=std::move(hooks);
        }
        bool supports(const std::string&)const override;
        Status get(const std::string&,std::string&)override;
        Status set(const std::string&,const std::string&)override;
        Status action(const std::string&)override;
        NetworkState network()const override;
        BluetoothState bluetooth()const override;
        Status bluetoothCommand(const std::string&,const std::string&)override;
        Status networkCommand(const std::string&,const std::string&,const std::string&)override;
        std::string environment()const override {
            return "Metalio-E-Ink4-target";
        }
    };
}
