#pragma once
#include "cJSON.h"
namespace inkdesk_app {
void Start();
void Open();
void Suspend();
void Input(const char* name, bool pressed);
#ifdef CONFIG_PAPER_CORE_APP
// Called once per physical down edge; true consumes the entire gesture.
bool KeyboardTouchDown(int x, int y);
#endif
bool Handle(const char* command, cJSON* request, cJSON* reply);
}
