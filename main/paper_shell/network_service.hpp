#pragma once
#include "paper/services.hpp"
namespace paper_network {
paper::NetworkState Snapshot();
std::string SavedSsid(); // diagnostic metadata only; never exports credentials
bool WantsRadio();
paper::Status Command(const std::string&,const std::string&,const std::string&);
}
