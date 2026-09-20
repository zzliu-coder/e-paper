#pragma once
#include "storage.hpp"
#include <set>
namespace paper {
    struct SettingDef {
        std::string key,label,category;
        std::vector<std::string>values;
        std::string defaultValue;
        bool hardware=false;
    };
    const std::vector<SettingDef>&settingDefinitions();
    struct NetworkAccessPoint { std::string ssid; int signal=0; bool secured=true; };
    struct NetworkState { bool busy=false,connected=false; std::string message="尚未扫描",ssid,ip; std::vector<NetworkAccessPoint> aps; };
    struct BluetoothDevice {std::string address,name;};
    struct BluetoothState {bool available=false,busy=false,connected=false;int mode=0;uint64_t revision=0;std::string message="尚未连接",address;std::vector<BluetoothDevice>devices;
        bool connectionKnown=false,railHeld=false,railOn=false;
        uint64_t rxBytes=0,lastRxMs=0;
        std::vector<std::string> recentReplies;
    };
    class Hardware {
        public:virtual ~Hardware()=default;
        virtual bool supports(const std::string&key)const=0;
        virtual Status get(const std::string&key,std::string&value)=0;
        virtual Status set(const std::string&key,const std::string&value)=0;
        virtual Status action(const std::string&name)=0;
        virtual std::string environment()const=0;
        virtual NetworkState network()const {return {};}
        virtual BluetoothState bluetooth()const {return {};}
        virtual Status bluetoothCommand(const std::string&,const std::string& = "") {return Status::fail(Error::Unavailable,"蓝牙服务尚未接入");}
        virtual Status networkCommand(const std::string&,const std::string& = "",const std::string& = "") {return Status::fail(Error::Unavailable,"网络服务尚未接入");}
    };
    struct SettingState {
        std::string key,requested,applied,phase="idle",error;
    };
    class Settings {
        Store&store_;
        Hardware&hw_;
        std::map<std::string,std::string>values_;
        SettingState last_;
        Status persist(const std::map<std::string,std::string>&);
        public:Settings(Store&s,Hardware&h):store_(s),hw_(h) {
        }
        Status load();
        Status apply(const std::string&,const std::string&);
        std::string get(const std::string&)const;
        bool available(const std::string&)const;
        const SettingState&last()const {
            return last_;
        }
    };
    // Hardware substitution is confined to the desktop executable. It never claims
    // a register write happened. The firmware uses MetalioHardware, not this class.
    class HostHardware final:public Hardware {
        std::map<std::string,std::string>state_;
        std::string reject_;
        public:HostHardware();
        bool supports(const std::string&)const override;
        Status get(const std::string&,std::string&)override;
        Status set(const std::string&,const std::string&)override;
        Status action(const std::string&)override;
        std::string environment()const override {
            return "host-simulated-hardware";
        }
        void rejectNext(const std::string&key) {
            reject_=key;
        }
    };
    struct AppDescriptor {
        std::string id,label;
        bool available=true;
    };
    const std::vector<AppDescriptor>&registeredApps();
    namespace future {
        struct AudioFormat {
            uint32_t sampleRate=16000;
            uint16_t channels=1,bits=16;
        };
        struct AudioFrame {
            uint64_t session=0,sequence=0,startSample=0;
            const int16_t*samples=nullptr;
            size_t count=0;
        };
        enum class TranscriptKind {
            Partial,Final,Revision,Gap
        };
        struct TranscriptEvent {
            uint64_t session=0,sequence=0,segment=0,revision=0,startMs=0,endMs=0;
            TranscriptKind kind=TranscriptKind::Partial;
            std::string text;
        };
        enum class RecordingPhase { Unavailable, Idle, Recording, Finalizing, Complete, Failed };
        struct RecordingSnapshot { uint64_t session=0, committedSamples=0; RecordingPhase phase=RecordingPhase::Unavailable; std::string error; };
        class RecordingPort {
            public:virtual ~RecordingPort()=default;
            virtual bool available()const=0;
            virtual Status start(const AudioFormat&,uint64_t&session)=0;
            virtual Status stop(uint64_t)=0;
            virtual Status snapshot(uint64_t,RecordingSnapshot&)=0;
            virtual Status cancel(uint64_t)=0;
        };
        class TranscriptionPort {
            public:virtual ~TranscriptionPort()=default;
            virtual bool available()const=0;
            virtual Status connect(uint64_t session)=0;
            virtual Status push(const AudioFrame&)=0;
            virtual Status finish(uint64_t)=0;
            // Non-blocking event drain; NotFound when a live backend has no new event.
            virtual Status nextEvent(TranscriptEvent&)=0;
            virtual Status cancel(uint64_t)=0;
        };
        class DisabledRecording final:public RecordingPort {
            public:bool available()const override {
                return false;
            }
            Status start(const AudioFormat&,uint64_t&session)override {
                session=0;
                return Status::fail(Error::Unsupported,"录音功能尚未接入");
            }
            Status snapshot(uint64_t,RecordingSnapshot&out)override { out={};return Status::fail(Error::Unsupported,"录音功能尚未接入"); }
            Status stop(uint64_t)override {
                return Status::fail(Error::Unsupported,"录音功能尚未接入");
            }
            Status cancel(uint64_t)override {
                return {
                };
            }
        };
        class DisabledTranscription final:public TranscriptionPort {
            public:bool available()const override {
                return false;
            }
            Status connect(uint64_t)override {
                return Status::fail(Error::Unsupported,"实时转写尚未接入");
            }
            Status push(const AudioFrame&)override {
                return Status::fail(Error::Unsupported,"实时转写尚未接入");
            }
            Status nextEvent(TranscriptEvent&out)override {out={};return Status::fail(Error::Unsupported,"实时转写尚未接入");}
            Status finish(uint64_t)override {
                return Status::fail(Error::Unsupported,"实时转写尚未接入");
            }
            Status cancel(uint64_t)override {
                return {
                };
            }
        };
        Status validate(const TranscriptEvent&);
    }
}
