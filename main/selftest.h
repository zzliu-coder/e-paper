#pragma once
#include "cJSON.h"
#include <cstddef>
#include <cstdint>
namespace selftest {
void Start();
bool Busy();
bool Visible();
void Hide();
void Input(const char* name, bool pressed, int x = -1, int y = -1);
void BluetoothRx(const uint8_t* bytes, size_t size);
bool Handle(const char* cmd, cJSON* request, cJSON* reply);
}
