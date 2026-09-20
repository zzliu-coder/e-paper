#pragma once
#include "paper/services.hpp"
namespace paper_network {
paper::NetworkState Snapshot();
bool WantsRadio();
paper::Status Command(const std::string&,const std::string&,const std::string&);
}
