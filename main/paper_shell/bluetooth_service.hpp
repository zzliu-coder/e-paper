#pragma once
#include "paper/services.hpp"
namespace paper_bluetooth {
paper::BluetoothState Snapshot();
paper::Status Command(const std::string&,const std::string& = "");
void Observe(const uint8_t*,size_t);
bool OwnsControl();
void DefaultInitialization(void (*initialize)());
paper::Status BeginLocalAudio();
void EndLocalAudio();
}
