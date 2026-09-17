#pragma once
#include "cJSON.h"
namespace inkdesk_app {
void Start();
void Open();
void Suspend();
void Input(const char* name, bool pressed);
bool Handle(const char* command, cJSON* request, cJSON* reply);
}
