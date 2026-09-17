#pragma once
#include <esp_err.h>
#include "cJSON.h"
namespace personal_sdk {
esp_err_t StartTransport();
void StartHardware();
void TouchSample(int count, int x, int y);
cJSON* DiagnosticCall(const char* command, cJSON* args = nullptr);
const char* DeviceId();
const char* BootId();
bool AudioBusy();
cJSON* RecordReplay(const char* path);
}
