#pragma once
#include "power_policy.h"
inline bool power_hw_main_rail_is_on(){return powerRefs>0;}
inline int power_hw_main_rail_set(bool){return 0;}
