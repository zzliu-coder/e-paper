#pragma once
#include "paper/runtime.hpp"
namespace paper_maintenance {
void Init(paper::Runtime*);
paper::Status Action(const std::string&);
bool Tick();
bool UsbOwned();
std::string Text();
std::string Json();
void AcceptBoot();
}
