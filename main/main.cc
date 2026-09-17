#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "system_info.h"
#include "esp_debug_cloud.h"

#define TAG "main"
#include "personal_sdk.h"

extern "C" void app_main(void)
{
    // Reserve the USB channel for framed diagnostics during this bring-up.
    esp_log_level_set("*", ESP_LOG_NONE);
    ESP_ERROR_CHECK(personal_sdk::StartTransport());
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_LOGE(TAG, "NVS requires repair; diagnostic mode will not erase it");
        return;
    }
    ESP_ERROR_CHECK(ret);

    // 调试云 UDP 日志：默认宏关闭为空操作，不影响业务

    // Launch the application
    personal_sdk::StartHardware();
}
