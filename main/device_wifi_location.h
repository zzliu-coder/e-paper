#pragma once

#include <string>
#include <vector>

namespace device_wifi_location {

/** One bounded nearby-AP record for the local diagnostic channel. */
struct DiagnosticAp {
    std::string ssid;
    std::string mac;
    int signal = 0;
};

/**
 * Run a local, bounded Wi-Fi scan without sending anything to the cloud.
 *
 * The existing boot reporter uses the same scanner but POSTs only from its
 * own worker.  This entry point deliberately returns the records to the SDK
 * transport so a host can inspect radio visibility without credentials.
 */
bool ScanForDiagnostic(std::vector<DiagnosticAp>& out);

/**
 * 开机 WiFi 指纹定位（fire-and-forget）。
 *
 * - 仅在已联网且非 SoftAP 配网时调度；不阻塞启动 / LVGL
 * - 后台短时扫附近 AP（≤10，按 RSSI），POST 到 console/device/location/wifi
 * - 只打印响应 JSON，不做业务落库；atomic 防重入；失败只打日志
 */
void RequestBootReport();

}  // namespace device_wifi_location
