#include "personal_sdk.h"
#include "selftest.h"
#include "inkdesk_app.h"
#include "book_transfer.h"

#include "board.h"
#include "dual_network_board.h"
#include "IOExpander.hpp"
#include "SdCardManager.hpp"
#include "SimpleUart.hpp"
#include "audio_codec.h"
#include "display/lv_adapter_display.h"
#include "bq27220_gauge.h"
#include "config.h"
#include "device_wifi_location.h"
#include "pcf8563.h"
#include "sc7a20h.h"
#include "wifi_station.h"
#include "esp_wifi.h"
#ifdef CONFIG_PAPER_CORE_APP
#include "paper_shell/network_service.hpp"
#include "paper_shell/bluetooth_service.hpp"
#endif
#include "usb_virtual_disk.h"

#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

#include <mbedtls/sha256.h>

namespace personal_sdk {
namespace {

constexpr size_t kMaxInputName = 24;
constexpr size_t kMaxInputSource = 16;
constexpr int kInputPollPeriodMs = 20;
constexpr int kAudioToneMinMs = 50;
constexpr int kAudioToneMaxMs = 500;
constexpr int kAudioToneMinHz = 200;
constexpr int kAudioToneMaxHz = 2000;
constexpr int kHapticActualMs = 35;  // 板级 VIBRATION_MOTOR_PULSE_MS
constexpr int kAudioSampleMs = 120;
constexpr int kAudioSampleMaxSamples = 3200;
constexpr int kSceneWidth = 800;
constexpr int kSceneHeight = 480;
constexpr size_t kSceneFrameBytes = kSceneWidth * kSceneHeight / 8;
constexpr size_t kSceneMaxRects = 24;
constexpr int64_t kSceneMinRefreshUs = 1500000;

enum class InputKind : uint8_t {
    Touch,
    Key,
    Signal,
};

struct InputEvent {
    InputKind kind = InputKind::Touch;
    int count = 0;
    int x = 0;
    int y = 0;
    int raw_level = -1;
    bool pressed = false;
    char name[kMaxInputName] = {};
    char source[kMaxInputSource] = {};
};

struct Job {
    unsigned id = 0;
    char text[257] = {};
};

struct ToneArgs {
    int duration_ms = 150;
    int frequency_hz = 440;
    int volume = 80;
};

QueueHandle_t input_events = nullptr;
QueueHandle_t jobs = nullptr;
std::atomic<bool> ready{false};
std::atomic<unsigned> dropped{0};
std::atomic<unsigned> input_overflow{0},tx_timeout{0},frame_oversize{0},encode_failure{0},task_failure{0};
std::atomic<unsigned> job_id{0};
std::atomic<unsigned> job_done{0};
std::atomic<int> job_result{0};
std::atomic<bool> touch_reader_seen{false};
std::atomic<unsigned> touch_read_errors{0};
std::atomic<bool> audio_started{false};
std::atomic<bool> audio_tone_busy{false};
std::atomic<unsigned> audio_tone_count{0};
std::atomic<int> audio_tone_samples{0};
std::atomic<int> audio_tone_last_result{ESP_ERR_NOT_FINISHED};
std::atomic<bool> audio_sample_busy{false};
std::atomic<unsigned> audio_sample_count{0};
std::atomic<unsigned> haptic_pulse_count{0};
// The 800x480 1bpp framebuffer is 48000 bytes, allocated in PSRAM after board init.
uint8_t* scene_frame = nullptr;
std::mutex scene_mutex;
std::string scene_version = "boot";
std::atomic<unsigned> scene_revision{0};
std::atomic<int64_t> scene_last_refresh_us{0};
char device[13] = {};
char boot[17] = {};
unsigned seq = 0;

void Num(cJSON* j, const char* key, double value) {
    cJSON_AddNumberToObject(j, key, value);
}

void Str(cJSON* j, const char* key, const char* value) {
    cJSON_AddStringToObject(j, key, value != nullptr ? value : "");
}

void CopyText(char* dst, size_t dst_size, const char* src) {
    if (dst == nullptr || dst_size == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, dst_size, "%s", src);
}

const char* TouchRegion(int x, int y) {
    // 盖板键当前由 CST816S 的原始坐标命中；保留原始坐标，避免把猜测的
    // 坐标转换当成硬件事实。允许一个小容差，便于把校准差异记录下来。
    constexpr int kTolerance = 100;
    if (std::abs(x - TOUCH_VK_HOME_X) <= kTolerance &&
        std::abs(y - TOUCH_VK_HOME_Y) <= kTolerance) {
        return "vk_home";
    }
    if (std::abs(x - TOUCH_VK_PREV_X) <= kTolerance &&
        std::abs(y - TOUCH_VK_PREV_Y) <= kTolerance) {
        return "vk_prev";
    }
    if (std::abs(x - TOUCH_VK_NEXT_X) <= kTolerance &&
        std::abs(y - TOUCH_VK_NEXT_Y) <= kTolerance) {
        return "vk_next";
    }
    return "screen";
}

void QueueInput(const InputEvent& event) {
    if (input_events == nullptr || xQueueSend(input_events, &event, 0) != pdTRUE) {
        input_overflow++;
        dropped++;
    }
}

void QueueDigitalEvent(const char* name, const char* source, int raw_level, bool pressed,
                       InputKind kind = InputKind::Key) {
    if (kind == InputKind::Key) selftest::Input(name, pressed);
    if (kind == InputKind::Key) inkdesk_app::Input(name, pressed);
    InputEvent event{};
    event.kind = kind;
    event.raw_level = raw_level;
    event.pressed = pressed;
    CopyText(event.name, sizeof(event.name), name);
    CopyText(event.source, sizeof(event.source), source);
    QueueInput(event);
}

void Send(cJSON* json) {
    if (json == nullptr) {
        return;
    }
    Num(json, "protocol", 1);
    Num(json, "seq", ++seq);
    Num(json, "uptime_ms", esp_timer_get_time() / 1000);
    Num(json, "dropped", dropped.load());
    auto* losses=cJSON_AddObjectToObject(json,"losses");
    Num(losses,"input_overflow",input_overflow.load());Num(losses,"tx_timeout",tx_timeout.load());
    Num(losses,"frame_oversize",frame_oversize.load());Num(losses,"encode_failure",encode_failure.load());Num(losses,"task_failure",task_failure.load());
    Str(json, "boot_id", boot);
    Str(json, "device_id", device);

    char* raw = cJSON_PrintUnformatted(json);
    if (raw != nullptr) {
        // Send is called only by the single Transport task. Keep the bounded
        // wire buffer off its stack: RX + TX stack frames exceeded 8192 bytes.
        static char frame[4096];
        int n = std::snprintf(frame, sizeof(frame), "ML1 %s\n", raw);
        if (n > 0 && n < static_cast<int>(sizeof(frame))) {
            if (usb_serial_jtag_write_bytes(frame, n, pdMS_TO_TICKS(20)) != n) {
                tx_timeout++;
                dropped++;
            }
        } else {
            frame_oversize++;
            dropped++;
        }
        cJSON_free(raw);
    } else {
        encode_failure++;
        dropped++;
    }
    cJSON_Delete(json);
}

void AddBool(cJSON* json, const char* key, bool value) {
    cJSON_AddBoolToObject(json, key, value ? 1 : 0);
}

void AddResult(cJSON* json, bool pass) {
    Str(json, "result", pass ? "PASS" : "NOT_PROVEN");
}

void AddOptionalNumber(cJSON* json, const char* key, bool valid, double value) {
    if (valid) {
        Num(json, key, value);
    } else {
        cJSON_AddNullToObject(json, key);
    }
}

bool JsonInt(cJSON* object, const char* key, int* out) {
    if (object == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    auto* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        floor(value->valuedouble) != value->valuedouble ||
        value->valuedouble < static_cast<double>(INT_MIN) ||
        value->valuedouble > static_cast<double>(INT_MAX)) {
        return false;
    }
    *out = static_cast<int>(value->valuedouble);
    return true;
}

std::string Sha256Hex(const uint8_t* data, size_t size) {
    uint8_t digest[32] = {};
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    if (mbedtls_sha256_starts(&context, 0) != 0 ||
        mbedtls_sha256_update(&context, data, size) != 0 ||
        mbedtls_sha256_finish(&context, digest) != 0) {
        mbedtls_sha256_free(&context);
        return {};
    }
    mbedtls_sha256_free(&context);
    char hex[65] = {};
    for (size_t i = 0; i < sizeof(digest); ++i) {
        std::snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", digest[i]);
    }
    return hex;
}

bool IsExactObjectKeys(cJSON* object, const char* const* keys, size_t key_count) {
    if (!cJSON_IsObject(object)) {
        return false;
    }
    for (cJSON* child = object->child; child != nullptr; child = child->next) {
        bool found = false;
        for (size_t i = 0; i < key_count; ++i) {
            if (child->string != nullptr && std::strcmp(child->string, keys[i]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    for (size_t i = 0; i < key_count; ++i) {
        if (cJSON_GetObjectItemCaseSensitive(object, keys[i]) == nullptr) {
            return false;
        }
    }
    return true;
}

bool BuildSceneFrame(cJSON* scene, uint8_t* frame, size_t frame_size, std::string* version) {
    static const char* const kSceneKeys[] = {"version", "rects"};
    if (frame == nullptr || frame_size != kSceneFrameBytes || version == nullptr ||
        !IsExactObjectKeys(scene, kSceneKeys, sizeof(kSceneKeys) / sizeof(kSceneKeys[0]))) {
        return false;
    }
    auto* version_item = cJSON_GetObjectItemCaseSensitive(scene, "version");
    if (!cJSON_IsString(version_item) || version_item->valuestring == nullptr) {
        return false;
    }
    const size_t version_len = std::strlen(version_item->valuestring);
    if (version_len < 1 || version_len > 48) {
        return false;
    }
    for (size_t i = 0; i < version_len; ++i) {
        const unsigned char c = static_cast<unsigned char>(version_item->valuestring[i]);
        if (c < 0x20 || c > 0x7e) {
            return false;
        }
    }
    auto* rects = cJSON_GetObjectItemCaseSensitive(scene, "rects");
    if (!cJSON_IsArray(rects) || cJSON_GetArraySize(rects) < 0 ||
        static_cast<size_t>(cJSON_GetArraySize(rects)) > kSceneMaxRects) {
        return false;
    }

    std::memset(frame, 0xff, frame_size);
    for (int i = 0; i < cJSON_GetArraySize(rects); ++i) {
        auto* rect = cJSON_GetArrayItem(rects, i);
        static const char* const kRectKeys[] = {"x", "y", "w", "h", "black"};
        if (!IsExactObjectKeys(rect, kRectKeys, sizeof(kRectKeys) / sizeof(kRectKeys[0]))) {
            return false;
        }
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        if (!JsonInt(rect, "x", &x) || !JsonInt(rect, "y", &y) ||
            !JsonInt(rect, "w", &w) || !JsonInt(rect, "h", &h)) {
            return false;
        }
        auto* black_item = cJSON_GetObjectItemCaseSensitive(rect, "black");
        if (!cJSON_IsBool(black_item) || x < 0 || y < 0 || w <= 0 || h <= 0 ||
            x >= kSceneWidth || y >= kSceneHeight || w > kSceneWidth - x ||
            h > kSceneHeight - y) {
            return false;
        }
        const bool black = cJSON_IsTrue(black_item);
        for (int yy = y; yy < y + h; ++yy) {
            uint8_t* row = frame + static_cast<size_t>(yy) * (kSceneWidth / 8);
            for (int xx = x; xx < x + w; ++xx) {
                const uint8_t mask = static_cast<uint8_t>(0x80u >> (xx % 8));
                if (black) {
                    row[xx / 8] &= static_cast<uint8_t>(~mask);
                } else {
                    row[xx / 8] |= mask;
                }
            }
        }
    }
    *version = version_item->valuestring;
    return true;
}

bool ApplySceneToDisplay(cJSON* scene) {
    esp_err_t lock_error = esp_lv_adapter_lock(2000);
    if (lock_error != ESP_OK) {
        return false;
    }
    bool success = true;
    lv_obj_t* screen = lv_screen_active();
    if (screen == nullptr) {
        success = false;
    } else {
        lv_obj_clean(screen);
        lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        auto* rects = cJSON_GetObjectItemCaseSensitive(scene, "rects");
        for (int i = 0; success && i < cJSON_GetArraySize(rects); ++i) {
            auto* item = cJSON_GetArrayItem(rects, i);
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
            JsonInt(item, "x", &x);
            JsonInt(item, "y", &y);
            JsonInt(item, "w", &w);
            JsonInt(item, "h", &h);
            auto* obj = lv_obj_create(screen);
            if (obj == nullptr) {
                success = false;
                break;
            }
            lv_obj_remove_style_all(obj);
            lv_obj_set_pos(obj, x, y);
            lv_obj_set_size(obj, w, h);
            lv_obj_set_style_bg_color(obj,
                                      cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "black"))
                                          ? lv_color_black()
                                          : lv_color_white(),
                                      0);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_clear_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        }
        lv_obj_invalidate(screen);
    }
    esp_lv_adapter_unlock();
    return success;
}

void AddSceneStatus(cJSON* reply) {
    std::string version;
    std::string digest;
    unsigned revision = 0;
    {
        std::lock_guard<std::mutex> lock(scene_mutex);
        version = scene_version;
        revision = scene_revision.load();
        if (scene_frame != nullptr) {
            digest = Sha256Hex(scene_frame, kSceneFrameBytes);
        }
    }
    Str(reply, "scene_version", version.c_str());
    Num(reply, "scene_revision", revision);
    Str(reply, "frame_format", "800x480-1bpp-msb-row-major");
    Str(reply, "frame_sha256", digest.c_str());
    Num(reply, "frame_bytes", kSceneFrameBytes);
    AddResult(reply, scene_frame != nullptr);
}

bool AddFrameRead(cJSON* reply, cJSON* request) {
    int offset = 0;
    int length = 0;
    if (!JsonInt(request, "offset", &offset) || !JsonInt(request, "length", &length) ||
        offset < 0 || length <= 0 || length > 512 ||
        offset > static_cast<int>(kSceneFrameBytes) - length) {
        AddResult(reply, false);
        Str(reply, "error", "invalid_frame_range");
        return false;
    }
    std::array<uint8_t, 512> bytes{};
    unsigned revision = 0;
    {
        std::lock_guard<std::mutex> lock(scene_mutex);
        if (scene_frame == nullptr) {
            AddResult(reply, false);
            Str(reply, "error", "frame_unavailable");
            return false;
        }
        std::memcpy(bytes.data(), scene_frame + offset, static_cast<size_t>(length));
        revision = scene_revision.load();
    }
    std::string hex;
    hex.reserve(static_cast<size_t>(length) * 2);
    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < length; ++i) {
        const uint8_t b = bytes[static_cast<size_t>(i)];
        hex.push_back(kHex[b >> 4]);
        hex.push_back(kHex[b & 0x0f]);
    }
    Num(reply, "offset", offset);
    Num(reply, "length", length);
    Num(reply, "scene_revision", revision);
    Str(reply, "hex", hex.c_str());
    AddResult(reply, true);
    return true;
}

bool ReadExpanderLevel(IOExpander::Pin pin, int* level) {
    if (level == nullptr) {
        return false;
    }
    *level = -1;
    auto& io = IOExpander::getInstance();
    if (!io.isInitialized()) {
        return false;
    }
    uint8_t value = 0;
    if (io.getLevel(pin, &value) != ESP_OK) {
        return false;
    }
    *level = value;
    return true;
}

struct TrackedLevel {
    bool valid = false;
    int raw = -1;
};

void TrackLevel(const char* name, const char* source, int raw, bool active_low,
                TrackedLevel* previous, InputKind kind = InputKind::Key) {
    if (previous == nullptr || raw < 0) {
        return;
    }
    const bool pressed = active_low ? (raw == 0) : (raw != 0);
    if (!previous->valid) {
        previous->valid = true;
        previous->raw = raw;
        return;
    }
    if (previous->raw == raw) {
        return;
    }
    previous->raw = raw;
    QueueDigitalEvent(name, source, raw, pressed, kind);
}

void InputPoll(void*) {
    TrackedLevel boot_level;
    TrackedLevel power_level;
    TrackedLevel volume_down;
    TrackedLevel volume_up;
    TrackedLevel accel_int;

    while (true) {
        const int boot_raw = gpio_get_level(BOOT_BUTTON_GPIO);
        const int power_raw = gpio_get_level(POWER_BUTTON_GPIO);
        TrackLevel("boot", "gpio", boot_raw, true, &boot_level);
        TrackLevel("power", "gpio", power_raw, true, &power_level);

        int volume_down_raw = -1;
        int volume_up_raw = -1;
        int accel_int_raw = -1;
        (void)ReadExpanderLevel(IOExpander::Pin::VOLUME_DOWN, &volume_down_raw);
        (void)ReadExpanderLevel(IOExpander::Pin::VOLUME_UP, &volume_up_raw);
        (void)ReadExpanderLevel(IOExpander::Pin::ACCEL_INT, &accel_int_raw);
        TrackLevel("volume_down", "tca9555", volume_down_raw, true, &volume_down);
        TrackLevel("volume_up", "tca9555", volume_up_raw, true, &volume_up);
        TrackLevel("accel_int", "tca9555", accel_int_raw, false, &accel_int, InputKind::Signal);
        vTaskDelay(pdMS_TO_TICKS(kInputPollPeriodMs));
    }
}

AudioCodec* GetAudioCodecSafe() {
    if (!ready.load()) {
        return nullptr;
    }
    return Board::GetInstance().GetAudioCodec();
}

class LocalAudioLease {
public:
    LocalAudioLease()=default;
    LocalAudioLease(const LocalAudioLease&)=delete;
    LocalAudioLease& operator=(const LocalAudioLease&)=delete;
#ifdef CONFIG_PAPER_CORE_APP
    paper::Status status=paper_bluetooth::BeginLocalAudio();
    ~LocalAudioLease(){if(status)paper_bluetooth::EndLocalAudio();}
    bool ready()const{return bool(status);}
#else
    bool ready()const{return true;}
#endif
};

void AudioToneTask(void* arg) {
    auto* tone = static_cast<ToneArgs*>(arg);
    bool success = false;
    if (tone != nullptr) {
        AudioCodec* codec = GetAudioCodecSafe();
        if (codec != nullptr) {
        LocalAudioLease route;
        if (route.ready()) {
            bool expected = false;
            if (audio_started.compare_exchange_strong(expected, true)) {
                // 只在第一次明确请求 tone 时启动 I2S；不会在开机自动发声。
                codec->Start();
            }
            const int previous_volume = codec->output_volume();
            const bool previous_output = codec->output_enabled();
            auto& io = IOExpander::getInstance();
            uint8_t previous_pa = 0;
            const bool pa_ok = io.getLevel(IOExpander::Pin::PA, &previous_pa) == ESP_OK &&
                io.setLevel(IOExpander::Pin::PA, true) == ESP_OK;
            vTaskDelay(pdMS_TO_TICKS(60));
            codec->SetDiagnosticVolume(tone->volume);
            codec->EnableOutput(true);
            const int samples = AUDIO_OUTPUT_SAMPLE_RATE * tone->duration_ms / 1000;
            std::vector<int16_t> pcm(static_cast<size_t>(samples));
            constexpr double kPi = 3.14159265358979323846;
            for (int i = 0; i < samples; ++i) {
                const double phase = 2.0 * kPi * tone->frequency_hz * i /
                                     AUDIO_OUTPUT_SAMPLE_RATE;
                const double fade = std::min(1.0, std::min(i, samples - 1 - i) /
                    (AUDIO_OUTPUT_SAMPLE_RATE * 0.02));
                pcm[static_cast<size_t>(i)] = static_cast<int16_t>(12000.0 * fade * std::sin(phase));
            }
            // BTAudioCodec 的底层写入使用有界大小的单次 PCM；若外部时钟未准备好，
            // 该任务可能等待，但 USB transport 仍保持响应，便于诊断后复位。
            audio_tone_samples = codec->OutputDataChecked(pcm);
            success = pa_ok && audio_tone_samples.load() == samples;
            // Allow the bounded DMA tail to reach the external codec.
            vTaskDelay(pdMS_TO_TICKS(120));
            codec->EnableOutput(previous_output);
            codec->SetDiagnosticVolume(previous_volume);
            if (pa_ok) io.setLevel(IOExpander::Pin::PA, previous_pa != 0);
        }
        }
        delete tone;
    }
    audio_tone_last_result = success ? ESP_OK : ESP_FAIL;
    audio_tone_busy = false;
    if (success) {
        audio_tone_count++;
    }
    vTaskDelete(nullptr);
}

void AddInputSnapshot(cJSON* reply) {
    auto* inputs = cJSON_AddArrayToObject(reply, "inputs");
    auto add_gpio = [inputs](const char* name, int gpio, int raw, bool active_low) {
        auto* item = cJSON_CreateObject();
        Str(item, "name", name);
        Str(item, "source", "gpio");
        Num(item, "gpio", gpio);
        if (std::strcmp(name, "boot") == 0) {
            Str(item, "role", "boot_or_ai_ptt_candidate");
        }
        Num(item, "active_level", active_low ? 0 : 1);
        Num(item, "level", raw);
        AddBool(item, "pressed", active_low ? raw == 0 : raw != 0);
        cJSON_AddItemToArray(inputs, item);
    };
    add_gpio("boot", static_cast<int>(BOOT_BUTTON_GPIO), gpio_get_level(BOOT_BUTTON_GPIO), true);
    add_gpio("power", static_cast<int>(POWER_BUTTON_GPIO), gpio_get_level(POWER_BUTTON_GPIO), true);

    auto* expander = cJSON_AddObjectToObject(reply, "tca9555");
    const bool io_ready = IOExpander::getInstance().isInitialized();
    AddBool(expander, "initialized", io_ready);
    auto add_expander = [expander](const char* name, IOExpander::Pin pin, bool active_low) {
        auto* item = cJSON_CreateObject();
        int raw = -1;
        const bool valid = ReadExpanderLevel(pin, &raw);
        Str(item, "name", name);
        Str(item, "source", "tca9555");
        if (valid) {
            Num(item, "level", raw);
            AddBool(item, "pressed", active_low ? raw == 0 : raw != 0);
        } else {
            cJSON_AddNullToObject(item, "level");
            cJSON_AddNullToObject(item, "pressed");
        }
        AddBool(item, "read_ok", valid);
        cJSON_AddItemToObject(expander, name, item);
    };
    add_expander("volume_down", IOExpander::Pin::VOLUME_DOWN, true);
    add_expander("volume_up", IOExpander::Pin::VOLUME_UP, true);
    add_expander("accel_int", IOExpander::Pin::ACCEL_INT, false);

    AddBool(reply, "touch_reader_seen", touch_reader_seen.load());
    Num(reply, "touch_read_errors", touch_read_errors.load());
    AddResult(reply, ready.load() && touch_reader_seen.load());
}

void AddInventory(cJSON* reply) {
    AddResult(reply, ready.load());
    Str(reply, "probe_mode", "read-only-unless-explicit-test");
    auto* screen = cJSON_AddObjectToObject(reply, "screen");
    Str(screen, "driver", "ssd1677");
    Num(screen, "width", DISPLAY_WIDTH);
    Num(screen, "height", DISPLAY_HEIGHT);
    Str(screen, "physical_refresh", "NOT_PROVEN");

    auto* ai_key = cJSON_AddObjectToObject(reply, "ai_key");
    Str(ai_key, "candidate", "boot_gpio");
    Num(ai_key, "gpio", BOOT_BUTTON_GPIO);
    Str(ai_key, "runtime_role", "official_board_maps_boot_long_press_to_ptt");
    Str(ai_key, "physical_result", "NOT_PROVEN");

    auto* io = cJSON_AddObjectToObject(reply, "io_expander");
    AddBool(io, "present", IOExpander::getInstance().isInitialized());
    Str(io, "chip", "tca9555");
    Str(io, "i2c_role", "buttons-power-routing-touch-reset");

    auto* touch = cJSON_AddObjectToObject(reply, "touch");
    AddBool(touch, "reader_seen", touch_reader_seen.load());
    Num(touch, "read_errors", touch_read_errors.load());
    Str(touch, "controller", "cst816s");
    Str(touch, "runtime_result", touch_reader_seen.load() ? "PASS" : "NOT_PROVEN");

    auto* imu = cJSON_AddObjectToObject(reply, "imu");
    const auto& accel = Sc7a20h::GetInstance();
    AddBool(imu, "present", accel.IsReady());
    Str(imu, "driver", "sc7a20h-compatible");
    Num(imu, "address", Sc7a20h::kDefaultAddr);
    Num(imu, "who_am_i", accel.LastWhoAmI());
    Str(imu, "sample_result", "NOT_PROVEN");

    auto* battery = cJSON_AddObjectToObject(reply, "battery");
    AddBool(battery, "gauge_present", Bq27220Gauge::GetInstance().IsReady());
    Str(battery, "gauge", "bq27220");
    Str(battery, "runtime_result", "NOT_PROVEN");

    auto* rtc = cJSON_AddObjectToObject(reply, "rtc");
    AddBool(rtc, "present", Pcf8563::GetInstance().IsReady());
    Str(rtc, "driver", "pcf8563");
    Str(rtc, "runtime_result", "NOT_PROVEN");

    auto* audio = cJSON_AddObjectToObject(reply, "audio");
    AddBool(audio, "codec_api", true);
    Str(audio, "codec", "btaudio-i2s");
    AddBool(audio, "tone_test_available", true);
    AddBool(audio, "mic_test_available", true);
    Str(audio, "sample_command", "audio.sample");
    Str(audio, "physical_result", "NOT_PROVEN");

    auto* haptic = cJSON_AddObjectToObject(reply, "haptic");
    AddBool(haptic, "api", true);
    Num(haptic, "gpio", VIBRATION_MOTOR_GPIO);
    Num(haptic, "pulse_ms", kHapticActualMs);
    Str(haptic, "physical_result", "NOT_PROVEN");

    auto* sd = cJSON_AddObjectToObject(reply, "sd");
    auto& card = SdCardManager::GetInstance();
    AddBool(sd, "mounted", card.IsMounted());
    AddBool(sd, "card_handle", card.HasCard());
    Str(sd, "mount_point", card.GetMountPoint());
    AddBool(sd, "roundtrip_test_available", true);
    Str(sd, "roundtrip_result", "NOT_PROVEN");

    auto* bt = cJSON_AddObjectToObject(reply, "bluetooth");
    AddBool(bt, "external_uart_initialized", SimpleUart::getInstance().isInitialized());
    Str(bt, "transport", "external-at-uart");
    Str(bt, "pairing_result", "NOT_PROVEN");

    auto* network = cJSON_AddObjectToObject(reply, "network");
    auto& board = static_cast<DualNetworkBoard&>(Board::GetInstance());
    const bool wifi_selected = board.GetNetworkType() == NetworkType::WIFI;
    Str(network, "selected", wifi_selected ? "wifi" : "4g");
    AddBool(network, "wifi_status_available", true);
    AddBool(network, "scan_test_available", true);
    Str(network, "scan_result", "NOT_PROVEN");
}

void AddPowerStatus(cJSON* reply) {
    auto* battery = cJSON_AddObjectToObject(reply, "battery");
    auto& gauge = Bq27220Gauge::GetInstance();
    AddBool(battery, "gauge_ready", gauge.IsReady());
    uint16_t voltage = 0;
    int16_t current = 0;
    const bool voltage_ok = gauge.ReadVoltageMv(voltage);
    const bool current_ok = gauge.ReadCurrentMa(current);
    AddOptionalNumber(battery, "voltage_mv", voltage_ok, voltage);
    AddOptionalNumber(battery, "current_ma", current_ok, current);
    int level = 0;
    bool charging = false;
    bool discharging = false;
    const bool level_ok = ready.load() &&
                          Board::GetInstance().GetBatteryLevel(level, charging, discharging);
    AddOptionalNumber(battery, "level_pct", level_ok, level);
    if (level_ok) {
        AddBool(battery, "charging", charging);
        AddBool(battery, "discharging", discharging);
    } else {
        cJSON_AddNullToObject(battery, "charging");
        cJSON_AddNullToObject(battery, "discharging");
    }
    AddResult(reply, voltage_ok || level_ok);
}

void AddImuProbe(cJSON* reply) {
    auto& imu = Sc7a20h::GetInstance();
    AddBool(reply, "ack", imu.IsReady());
    Num(reply, "address", Sc7a20h::kDefaultAddr);
    Num(reply, "who_am_i", imu.LastWhoAmI());
    Str(reply, "driver", "sc7a20h-compatible");
    AddResult(reply, imu.IsReady());
}

void AddImuSample(cJSON* reply) {
    int ax = 0;
    int ay = 0;
    int az = 0;
    float pitch = 0.0f;
    float roll = 0.0f;
    auto& imu = Sc7a20h::GetInstance();
    const bool ok = ready.load() && imu.ReadAccelMg(ax, ay, az) &&
                    imu.ReadPitchRollDeg(pitch, roll);
    if (ok) {
        Num(reply, "ax_mg", ax);
        Num(reply, "ay_mg", ay);
        Num(reply, "az_mg", az);
        Num(reply, "pitch_deg", pitch);
        Num(reply, "roll_deg", roll);
    }
    AddResult(reply, ok);
}

void AddAudioInfo(cJSON* reply) {
    AudioCodec* codec = GetAudioCodecSafe();
    if (codec == nullptr) {
        AddResult(reply, false);
        return;
    }
    AddBool(reply, "codec_created", true);
    AddBool(reply, "duplex", codec->duplex());
    AddBool(reply, "input_reference", codec->input_reference());
    Num(reply, "input_sample_rate", codec->input_sample_rate());
    Num(reply, "output_sample_rate", codec->output_sample_rate());
    Num(reply, "input_channels", codec->input_channels());
    Num(reply, "output_channels", codec->output_channels());
    Num(reply, "output_volume", codec->output_volume());
    AddBool(reply, "input_enabled", codec->input_enabled());
    AddBool(reply, "output_enabled", codec->output_enabled());
    AddBool(reply, "started", audio_started.load());
    AddResult(reply, true);
}

void AddAudioSample(cJSON* reply) {
    AudioCodec* codec = GetAudioCodecSafe();
    if (codec == nullptr) {
        Str(reply, "sample_result", "NOT_PROVEN");
        AddResult(reply, false);
        return;
    }

    bool expected = false;
    if (!audio_sample_busy.compare_exchange_strong(expected, true)) {
        Str(reply, "sample_result", "NOT_PROVEN");
        Str(reply, "error", "audio_sample_busy");
        AddResult(reply, false);
        return;
    }
    LocalAudioLease route;
    if(!route.ready()){
        audio_sample_busy=false;
        Str(reply,"error","本地音频通道未就绪或正在使用蓝牙");
        AddResult(reply,false);return;
    }

    // Preserve the caller's enable state.  A diagnostic sample must not leave
    // the normal application muted or listening after it returns.
    const bool was_input_enabled = codec->input_enabled();
    const bool was_output_enabled = codec->output_enabled();
    if (!audio_started.load()) {
        codec->Start();
        audio_started = true;
    }
    codec->EnableInput(true);
    codec->EnableOutput(false);

    int sample_rate = codec->input_sample_rate();
    if (sample_rate <= 0) {
        sample_rate = AUDIO_INPUT_SAMPLE_RATE;
    }
    int samples = sample_rate * kAudioSampleMs / 1000;
    if (samples < 1) {
        samples = 1;
    }
    if (samples > kAudioSampleMaxSamples) {
        samples = kAudioSampleMaxSamples;
    }

    std::vector<int16_t> pcm(static_cast<size_t>(samples));
    const int received = codec->InputDataChecked(pcm);
    const bool input_ok = received == samples;
    int peak = 0;
    int nonzero = 0;
    uint64_t sum_abs = 0;
    for (const int16_t sample : pcm) {
        const int value = sample;
        const int abs_value = value == INT16_MIN ? INT16_MAX : std::abs(value);
        if (abs_value > peak) {
            peak = abs_value;
        }
        if (abs_value != 0) {
            ++nonzero;
        }
        sum_abs += static_cast<uint64_t>(abs_value);
    }

    codec->EnableInput(was_input_enabled);
    codec->EnableOutput(was_output_enabled);
    audio_sample_busy = false;
    audio_sample_count++;

    AddBool(reply, "input_data_ok", input_ok);
    Num(reply, "sample_rate", sample_rate);
    Num(reply, "samples_requested", samples);
    Num(reply, "samples_observed", std::max(0,received));
    Num(reply, "peak_abs", peak);
    Num(reply, "nonzero_samples", input_ok ? nonzero : 0);
    Num(reply, "mean_abs", input_ok && samples > 0
                               ? static_cast<double>(sum_abs) / samples
                               : 0);
    Str(reply, "sample_result", input_ok ? "PASS" : "NOT_PROVEN");
    Str(reply, "physical_effect", "NOT_PROVEN");
    AddResult(reply, input_ok);
}

void AddWifiStatus(cJSON* reply) {
    auto& board = static_cast<DualNetworkBoard&>(Board::GetInstance());
    const bool wifi_selected = board.GetNetworkType() == NetworkType::WIFI;
    auto* wifi = cJSON_AddObjectToObject(reply, "wifi");
    AddBool(wifi, "selected", wifi_selected);
    if (!wifi_selected) {
        Str(wifi, "state", "inactive-network-selection");
        Str(wifi, "result", "NOT_PROVEN");
        AddResult(reply, true);
        return;
    }
    auto& station = WifiStation::GetInstance();
#ifdef CONFIG_PAPER_CORE_APP
    auto snapshot=paper_network::Snapshot();
    AddBool(wifi,"connected",snapshot.connected);AddBool(wifi,"link_in_progress",snapshot.busy);
    AddBool(wifi,"lp_paused",station.IsLpPaused());Str(wifi,"ssid",snapshot.connected?snapshot.ssid.c_str():"");Str(wifi,"ip",snapshot.ip.c_str());
    wifi_ap_record_t ap={};
    if(snapshot.connected&&esp_wifi_sta_get_ap_info(&ap)==ESP_OK){Num(wifi,"rssi_dbm",ap.rssi);Num(wifi,"channel",ap.primary);}
    else{cJSON_AddNullToObject(wifi,"rssi_dbm");cJSON_AddNullToObject(wifi,"channel");}
    Str(wifi,"scan",snapshot.message.c_str());AddResult(reply,true);return;
#endif
    AddBool(wifi, "connected", station.IsConnected());
    AddBool(wifi, "link_in_progress", station.IsLinkInProgress());
    AddBool(wifi, "lp_paused", station.IsLpPaused());
    Str(wifi, "ssid", station.GetSsid().c_str());
    Str(wifi, "ip", station.GetIpAddress().c_str());
    if (station.IsConnected()) {
        Num(wifi, "rssi_dbm", station.GetRssi());
        Num(wifi, "channel", station.GetChannel());
    } else {
        cJSON_AddNullToObject(wifi, "rssi_dbm");
        cJSON_AddNullToObject(wifi, "channel");
    }
    Str(wifi, "scan", "NOT_PROVEN");
    AddResult(reply, true);
}

void AddWifiScan(cJSON* reply) {
    std::vector<device_wifi_location::DiagnosticAp> aps;
    const bool scan_ok = device_wifi_location::ScanForDiagnostic(aps);
    auto* records = cJSON_AddArrayToObject(reply, "aps");
    for (const auto& ap : aps) {
        auto* item = cJSON_CreateObject();
        if (item == nullptr) {
            continue;
        }
        Str(item, "ssid", ap.ssid.c_str());
        Str(item, "mac", ap.mac.c_str());
        Num(item, "signal_dbm", ap.signal);
        cJSON_AddItemToArray(records, item);
    }
    Num(reply, "count", static_cast<double>(aps.size()));
    AddBool(reply, "scan_ok", scan_ok);
    // The command itself was handled successfully even when the radio could
    // not scan; capability result stays separate from protocol success.
    Str(reply, "scan_result", scan_ok ? "PASS" : "NOT_PROVEN");
    AddResult(reply, scan_ok);
}

void AddSdStatus(cJSON* reply) {
    auto& sd = SdCardManager::GetInstance();
    AddBool(reply, "mounted", sd.IsMounted());
    AddBool(reply, "card_handle", sd.HasCard());
    Str(reply, "mount_point", sd.GetMountPoint());
    Str(reply, "write_roundtrip", "NOT_RUN");
    AddResult(reply, sd.IsMounted());
}

void AddSdRoundtrip(cJSON* reply) {
    auto& sd = SdCardManager::GetInstance();
    const char* path = SD_APP_ROOT "/.metalio-sdk-roundtrip-v1.bin";
    Str(reply, "test_path", path);
    if (!sd.IsMounted() || !sd.HasCard()) {
        Str(reply, "write_roundtrip", "NOT_RUN");
        Str(reply, "reason", "sd_not_mounted");
        AddResult(reply, false);
        return;
    }

    static constexpr char kPayload[] = "metalio-sdk-sd-roundtrip-v1\\n";
    constexpr size_t kPayloadSize = sizeof(kPayload) - 1;
    // The path is private to this diagnostic.  Remove a stale copy from an
    // interrupted previous run, then always remove our own file on exit.
    (void)unlink(path);
    bool write_ok = false;
    bool read_ok = false;
    bool match = false;
    bool remove_ok = false;
    size_t written = 0;
    size_t read = 0;

    FILE* out = std::fopen(path, "wb");
    if (out != nullptr) {
        written = std::fwrite(kPayload, 1, kPayloadSize, out);
        const int close_err = std::fclose(out);
        write_ok = written == kPayloadSize && close_err == 0;
    }

    char buffer[kPayloadSize] = {};
    if (write_ok) {
        FILE* in = std::fopen(path, "rb");
        if (in != nullptr) {
            read = std::fread(buffer, 1, kPayloadSize, in);
            const int close_err = std::fclose(in);
            read_ok = read == kPayloadSize && close_err == 0;
            match = read_ok && std::memcmp(buffer, kPayload, kPayloadSize) == 0;
        }
    }
    remove_ok = unlink(path) == 0 || errno == ENOENT;

    AddBool(reply, "write_ok", write_ok);
    AddBool(reply, "read_ok", read_ok);
    AddBool(reply, "content_match", match);
    AddBool(reply, "remove_ok", remove_ok);
    Num(reply, "bytes_written", written);
    Num(reply, "bytes_read", read);
    const bool pass = write_ok && read_ok && match && remove_ok;
    Str(reply, "write_roundtrip", pass ? "PASS" : "NOT_PROVEN");
    AddResult(reply, pass);
}

void AddBluetoothInfo(cJSON* reply) {
    AddBool(reply, "uart_initialized", SimpleUart::getInstance().isInitialized());
    Num(reply, "tx_gpio", BT_AUDIO_TX_PIN);
    Num(reply, "rx_gpio", BT_AUDIO_RX_PIN);
    Str(reply, "transport", "external-at-uart");
    Str(reply, "pairing", "NOT_RUN");
    AddResult(reply, SimpleUart::getInstance().isInitialized());
}

void AddRtcStatus(cJSON* reply) {
    auto& rtc = Pcf8563::GetInstance();
    AddBool(reply, "ready", rtc.IsReady());
    struct tm now = {};
    bool valid = false;
    const bool ok = rtc.IsReady() && rtc.GetTime(now, &valid);
    if (ok) {
        Num(reply, "year", now.tm_year + 1900);
        Num(reply, "month", now.tm_mon + 1);
        Num(reply, "day", now.tm_mday);
        Num(reply, "hour", now.tm_hour);
        Num(reply, "minute", now.tm_min);
        Num(reply, "second", now.tm_sec);
        AddBool(reply, "time_valid", valid);
    }
    AddResult(reply, ok);
}

void Request(const char* line, cJSON** local_reply = nullptr) {
    cJSON* req = cJSON_Parse(line != nullptr ? line : "");
    cJSON* reply = cJSON_CreateObject();
    cJSON* id = req != nullptr ? cJSON_GetObjectItemCaseSensitive(req, "id") : nullptr;
    cJSON* protocol = req != nullptr ? cJSON_GetObjectItemCaseSensitive(req, "protocol") : nullptr;
    cJSON* cmd = req != nullptr ? cJSON_GetObjectItemCaseSensitive(req, "cmd") : nullptr;
    if (cJSON_IsNumber(id)) {
        Num(reply, "id", id->valuedouble);
    }

    const char* err = nullptr;
    if (!cJSON_IsObject(req) || !cJSON_IsNumber(id) || !std::isfinite(id->valuedouble) ||
        id->valuedouble < 0 || id->valuedouble > 2147483647 || floor(id->valuedouble) != id->valuedouble ||
        !cJSON_IsNumber(protocol) || protocol->valuedouble != 1 || !cJSON_IsString(cmd)) {
        err = "invalid_request";
    } else if (inkdesk_app::Handle(cmd->valuestring, req, reply)) {
        auto* error=cJSON_GetObjectItem(reply,"error");
        if (cJSON_IsString(error)) err=error->valuestring;
    } else if (selftest::Handle(cmd->valuestring, req, reply)) {
        if (cJSON_IsString(cJSON_GetObjectItem(reply, "error"))) {
            err = cJSON_GetObjectItem(reply, "error")->valuestring;
        }
    } else if (local_reply == nullptr && selftest::Busy() &&
               std::strcmp(cmd->valuestring, "hello") && std::strcmp(cmd->valuestring, "ping") &&
               std::strcmp(cmd->valuestring, "status") && std::strcmp(cmd->valuestring, "job.get")) {
        err = "selftest_busy";
    } else if (book_transfer::Handle(cmd->valuestring,req,reply)) {
        auto* error=cJSON_GetObjectItem(reply,"error");
        if(cJSON_IsString(error))err=error->valuestring;
    } else if (!std::strcmp(cmd->valuestring, "storage.usb")) {
        // Host-side SD provisioning path. It only requests the asynchronous
        // USB/SD state; it never formats, erases, or writes files.
        auto* enabled = cJSON_GetObjectItemCaseSensitive(req, "enabled");
        if (!cJSON_IsBool(enabled)) {
            err = "invalid_enabled";
        } else {
            auto& vd = UsbVirtualDisk::GetInstance();
            vd.Init();
            AddBool(reply, "supported", vd.IsSupported());
            const bool desired = cJSON_IsTrue(enabled);
            if (!vd.IsSupported()) {
                err = "usb_storage_unsupported";
            } else if (desired != vd.IsGadgetActive() && !vd.IsBusy()) {
                vd.Toggle();
            }
            AddBool(reply, "requested", desired);
            AddBool(reply, "active", vd.IsGadgetActive());
            AddBool(reply, "busy", vd.IsBusy());
            Num(reply, "hint", static_cast<int>(vd.GetUiHint()));
        }
    } else if (!std::strcmp(cmd->valuestring, "hello")) {
        Str(reply, "product", "metalio-personal-sdk");
        Str(reply, "board", "metalio_eink4");
        Str(reply, "version", esp_app_get_description()->version);
        Str(reply, "idf", esp_get_idf_version());
        Str(reply, "mode", "official-diagnostic-hardware-probe");
        Str(reply, "app_version", esp_app_get_description()->version);
        auto* caps = cJSON_AddArrayToObject(reply, "commands");
        const char* commands[] = {
            "hello", "ping", "status", "display.text", "job.get", "inventory",
            "input.snapshot", "power.status", "imu.probe", "imu.read", "haptic.pulse",
            "audio.info", "audio.sample", "audio.tone", "wifi.status", "wifi.scan", "sd.status",
            "sd.roundtrip", "bt.info", "rtc.status", "rtc.set", "scene.set", "scene.get", "frame.read",
            "selftest.status", "selftest.open", "selftest.run", "selftest.logs", "selftest.log.read",
            "storage.usb",
#ifdef CONFIG_PAPER_CORE_APP
            "paper.status", "paper.open", "paper.tap", "paper.command", "paper.transfer", "paper.maintenance", "paper.file", "paper.network", "paper.bluetooth",
            "inkdesk.status", "inkdesk.open", "inkdesk.tap", "inkdesk.ui.open",
#else
            "inkdesk.status", "inkdesk.open", "inkdesk.tap", "inkdesk.key", "inkdesk.ui.open", "inkdesk.ui.frame", "inkdesk.fontlab.log", "inkdesk.fontbench.frame", "inkdesk.fontbench.open", "inkdesk.fontbench.config", "inkdesk.fontbench.control", "inkdesk.fontbench.state", "inkdesk.fontbench.lock", "inkdesk.fontbench.log", "inkdesk.fontlab4.page", "inkdesk.fontlab4.log",
            "book.begin", "book.chunk", "book.commit", "book.abort", "book.read",
#endif
        };
        for (const char* capability : commands) {
            cJSON_AddItemToArray(caps, cJSON_CreateString(capability));
        }
        AddBool(reply, "board_ready", ready.load());
        Str(reply, "hardware_probe", "paper-maintenance-1");
        AddBool(reply, "efuse_write", false);
        AddBool(reply, "flash_write", false);
        AddBool(reply, "sd_package_ota", true);
    } else if (!std::strcmp(cmd->valuestring, "ping")) {
        // Keep the reply small and side-effect free.
    } else if (!std::strcmp(cmd->valuestring, "status") ||
               !std::strcmp(cmd->valuestring, "job.get")) {
        AddBool(reply, "board_ready", ready.load());
        Num(reply, "heap_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        Num(reply, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        Num(reply, "usb_stack_free_min_bytes", uxTaskGetStackHighWaterMark(nullptr));
        Num(reply, "job_id", job_id.load());
        Num(reply, "job_done", job_done.load());
        Num(reply, "job_result", job_result.load());
        AddBool(reply, "touch_reader_seen", touch_reader_seen.load());
        Num(reply, "touch_read_errors", touch_read_errors.load());
        Num(reply, "input_queue_depth",
            input_events != nullptr ? uxQueueMessagesWaiting(input_events) : 0);
        AddBool(reply, "audio_started", audio_started.load());
        AddBool(reply, "audio_tone_busy", audio_tone_busy.load());
        Num(reply, "audio_tone_count", audio_tone_count.load());
        Num(reply, "audio_tone_last_result", audio_tone_last_result.load());
        Num(reply, "audio_tone_samples", audio_tone_samples.load());
        Num(reply, "haptic_pulse_count", haptic_pulse_count.load());
        Num(reply, "scene_revision", scene_revision.load());
        AddBool(reply, "selftest_busy", selftest::Busy());
        Str(reply, "sdk_phase", "hardware-probe");
    } else if (!std::strcmp(cmd->valuestring, "display.text")) {
        inkdesk_app::Suspend();
        auto* text = cJSON_GetObjectItemCaseSensitive(req, "text");
        if (!ready.load()) {
            err = "hardware_initializing";
        } else if (!cJSON_IsString(text) || std::strlen(text->valuestring) > 256) {
            err = "invalid_text";
        } else if (job_id.load() != job_done.load()) {
            err = "busy";
        } else {
            Job job{};
            job.id = job_id.load() + 1;
            std::strcpy(job.text, text->valuestring);
            job_id = job.id;
            job_result = ESP_ERR_NOT_FINISHED;
            if (xQueueSend(jobs, &job, 0) != pdTRUE) {
                job_id = job_done.load();
                err = "busy";
            } else {
                Num(reply, "job_id", job.id);
                Str(reply, "state", "accepted");
            }
        }
    } else if (!std::strcmp(cmd->valuestring, "scene.set")) {
        inkdesk_app::Suspend();
        auto* scene = cJSON_GetObjectItemCaseSensitive(req, "scene");
        std::vector<uint8_t> frame(kSceneFrameBytes);
        std::string version;
        if (!ready.load()) {
            err = "hardware_initializing";
        } else if (!BuildSceneFrame(scene, frame.data(), frame.size(), &version)) {
            err = "invalid_scene";
        } else {
            const int64_t now = esp_timer_get_time();
            const int64_t previous = scene_last_refresh_us.load();
            if (previous > 0 && now - previous < kSceneMinRefreshUs) {
                err = "refresh_rate_limited";
            } else if (!ApplySceneToDisplay(scene)) {
                err = "display_unavailable";
            } else {
                const std::string digest = Sha256Hex(frame.data(), frame.size());
                unsigned revision = 0;
                {
                    std::lock_guard<std::mutex> lock(scene_mutex);
                    std::memcpy(scene_frame, frame.data(), kSceneFrameBytes);
                    scene_version = version;
                    revision = scene_revision.fetch_add(1) + 1;
                }
                scene_last_refresh_us = now;
                Str(reply, "scene_version", version.c_str());
                Num(reply, "scene_revision", revision);
                Str(reply, "frame_sha256", digest.c_str());
                Num(reply, "frame_bytes", frame.size());
                Str(reply, "physical_effect", "NOT_PROVEN");
                Str(reply, "state", "accepted");
            }
        }
    } else if (!std::strcmp(cmd->valuestring, "scene.get")) {
        AddSceneStatus(reply);
    } else if (!std::strcmp(cmd->valuestring, "frame.read")) {
        if (!AddFrameRead(reply, req)) {
            const auto* frame_error = cJSON_GetObjectItemCaseSensitive(reply, "error");
            err = cJSON_IsString(frame_error) ? frame_error->valuestring : "frame_read_failed";
        }
    } else if (!std::strcmp(cmd->valuestring, "inventory")) {
        if (!ready.load()) {
            err = "hardware_initializing";
        } else {
            AddInventory(reply);
        }
    } else if (!std::strcmp(cmd->valuestring, "input.snapshot")) {
        if (!ready.load()) {
            err = "hardware_initializing";
        } else {
            AddInputSnapshot(reply);
        }
    } else if (!std::strcmp(cmd->valuestring, "power.status")) {
        AddPowerStatus(reply);
    } else if (!std::strcmp(cmd->valuestring, "imu.probe")) {
        AddImuProbe(reply);
    } else if (!std::strcmp(cmd->valuestring, "imu.read")) {
        AddImuSample(reply);
    } else if (!std::strcmp(cmd->valuestring, "haptic.pulse")) {
        if (!ready.load()) {
            err = "hardware_initializing";
        } else {
            auto* duration = cJSON_GetObjectItemCaseSensitive(req, "duration_ms");
            if (duration != nullptr &&
                (!cJSON_IsNumber(duration) || duration->valuedouble < 10 ||
                 duration->valuedouble > 250 || floor(duration->valuedouble) != duration->valuedouble)) {
                err = "invalid_duration";
            } else {
                Board::GetInstance().PulseVibration();
                haptic_pulse_count++;
                Num(reply, "requested_duration_ms",
                    duration != nullptr ? duration->valuedouble : kHapticActualMs);
                Num(reply, "actual_duration_ms", kHapticActualMs);
                Str(reply, "physical_effect", "NOT_PROVEN");
                Str(reply, "state", "accepted");
            }
        }
    } else if (!std::strcmp(cmd->valuestring, "audio.info")) {
        if (!ready.load()) {
            err = "hardware_initializing";
        } else {
            AddAudioInfo(reply);
        }
    } else if (!std::strcmp(cmd->valuestring, "audio.sample")) {
        if (!ready.load()) {
            err = "hardware_initializing";
        } else if (audio_tone_busy.load()) {
            err = "audio_tone_busy";
        } else {
            AddAudioSample(reply);
        }
    } else if (!std::strcmp(cmd->valuestring, "audio.tone")) {
        if (!ready.load()) {
            err = "hardware_initializing";
        } else if (audio_tone_busy.load()) {
            err = "busy";
        } else {
            int duration_ms = 150;
            int frequency_hz = 440;
            int volume = 80;
            if (cJSON_GetObjectItemCaseSensitive(req, "volume") != nullptr &&
                (!JsonInt(req, "volume", &volume) || volume < 0 || volume > 90)) {
                err = "invalid_volume";
            }
            auto* duration = cJSON_GetObjectItemCaseSensitive(req, "duration_ms");
            auto* frequency = cJSON_GetObjectItemCaseSensitive(req, "frequency_hz");
            if (duration != nullptr) {
                if (!cJSON_IsNumber(duration) || !std::isfinite(duration->valuedouble) ||
                    duration->valuedouble < kAudioToneMinMs ||
                    duration->valuedouble > kAudioToneMaxMs ||
                    floor(duration->valuedouble) != duration->valuedouble) {
                    err = "invalid_duration";
                } else {
                    duration_ms = static_cast<int>(duration->valuedouble);
                }
            }
            if (err == nullptr && frequency != nullptr) {
                if (!cJSON_IsNumber(frequency) || !std::isfinite(frequency->valuedouble) ||
                    frequency->valuedouble < kAudioToneMinHz ||
                    frequency->valuedouble > kAudioToneMaxHz ||
                    floor(frequency->valuedouble) != frequency->valuedouble) {
                    err = "invalid_frequency";
                } else {
                    frequency_hz = static_cast<int>(frequency->valuedouble);
                }
            }
            if (err == nullptr) {
                auto* args = new (std::nothrow) ToneArgs{duration_ms, frequency_hz, volume};
                if (args == nullptr) {
                    err = "no_memory";
                } else {
                    audio_tone_busy = true;
                    audio_tone_last_result = ESP_ERR_NOT_FINISHED;
                    audio_tone_samples = 0;
                    if (xTaskCreate(AudioToneTask, "sdk_tone", 4096, args, 4, nullptr) != pdPASS) {
                        audio_tone_busy = false;
                        delete args;
                        err = "no_task";
                    } else {
                        Num(reply, "duration_ms", duration_ms);
                        Num(reply, "frequency_hz", frequency_hz);
                        Num(reply, "volume", volume);
                        Str(reply, "physical_effect", "NOT_PROVEN");
                        Str(reply, "state", "accepted");
                    }
                }
            }
        }
    } else if (!std::strcmp(cmd->valuestring, "wifi.status")) {
        if (!ready.load()) {
            err = "hardware_initializing";
        } else {
            AddWifiStatus(reply);
        }
    } else if (!std::strcmp(cmd->valuestring, "wifi.scan")) {
        if (!ready.load()) {
            err = "hardware_initializing";
        } else {
            AddWifiScan(reply);
        }
    } else if (!std::strcmp(cmd->valuestring, "sd.status")) {
        AddSdStatus(reply);
    } else if (!std::strcmp(cmd->valuestring, "sd.roundtrip")) {
        AddSdRoundtrip(reply);
    } else if (!std::strcmp(cmd->valuestring, "bt.info")) {
        AddBluetoothInfo(reply);
    } else if (!std::strcmp(cmd->valuestring, "rtc.status")) {
        AddRtcStatus(reply);
    } else if (!std::strcmp(cmd->valuestring, "rtc.set")) {
        // Epoch supplied by the local developer host; device timezone determines
        // the wall clock stored in the RTC, matching the official RTC driver.
        auto* value=cJSON_GetObjectItemCaseSensitive(req,"epoch");
        if(!cJSON_IsNumber(value)||!std::isfinite(value->valuedouble)||
           value->valuedouble<946684800||value->valuedouble>=4102444800.0||
           std::floor(value->valuedouble)!=value->valuedouble){err="invalid_epoch";}
        else{
            time_t epoch=static_cast<time_t>(value->valuedouble);struct tm local{};
            if(!localtime_r(&epoch,&local)||!Pcf8563::GetInstance().SetTime(local))err="rtc_write_failed";
            else if(!Pcf8563::GetInstance().ApplyRtcToSystem())err="rtc_apply_failed";
            else AddRtcStatus(reply);
        }
    } else {
        err = "unsupported_command";
    }

    AddBool(reply, "ok", err == nullptr);
    if (err != nullptr) {
        Str(reply, "error", err);
    }
    if (req != nullptr) {
        cJSON_Delete(req);
    }
    if (local_reply != nullptr) *local_reply = reply;
    else Send(reply);
}

void Transport(void*) {
    // Exactly one Transport task owns this input buffer.
    static char line[4097];
    size_t used = 0;
    bool discard = false;
    int64_t heartbeat_us = 0;
    for (;;) {
        char buf[128];
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(10));
        for (int i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                if (discard) {
                    auto* json = cJSON_CreateObject();
                    Str(json, "event", "protocol.error");
                    Str(json, "error", "oversize_line");
                    Send(json);
                } else {
                    line[used] = '\0';
                    Request(line);
                }
                used = 0;
                discard = false;
            } else if (!discard) {
                if (used == sizeof(line) - 1) {
                    discard = true;
                    used = 0;
                } else {
                    line[used++] = buf[i];
                }
            }
        }

        InputEvent event{};
        for (int i = 0; i < 12 && input_events != nullptr &&
                          xQueueReceive(input_events, &event, 0) == pdTRUE; ++i) {
            auto* json = cJSON_CreateObject();
            if (event.kind == InputKind::Touch) {
                Str(json, "event", event.count < 0 ? "input.cancel" : "input.touch");
                Num(json, "count", event.count);
                Num(json, "raw_x", event.x);
                Num(json, "raw_y", event.y);
                if (event.name[0] != '\0') {
                    Str(json, "region", event.name);
                }
            } else if (event.kind == InputKind::Signal) {
                Str(json, "event", "input.signal");
                Str(json, "name", event.name);
                Str(json, "source", event.source);
                Num(json, "raw_level", event.raw_level);
                AddBool(json, "active", event.pressed);
            } else {
                Str(json, "event", "input.key");
                Str(json, "name", event.name);
                Str(json, "source", event.source);
                Num(json, "raw_level", event.raw_level);
                AddBool(json, "pressed", event.pressed);
            }
            Send(json);
        }

        const int64_t now = esp_timer_get_time();
        if (now - heartbeat_us >= 2000000) {
            heartbeat_us = now;
            auto* json = cJSON_CreateObject();
            Str(json, "event", "heartbeat");
            AddBool(json, "board_ready", ready.load());
            Num(json, "job_done", job_done.load());
            Send(json);
        }
    }
}

void Hardware(void*) {
    Board::GetInstance();
    // Reserve the scene buffer after the official board is initialized.
    scene_frame = static_cast<uint8_t*>(heap_caps_malloc(kSceneFrameBytes,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (scene_frame == nullptr) {
        return;
    }
    std::memset(scene_frame, 0xff, kSceneFrameBytes);
    ready = true;
    selftest::Start();
    inkdesk_app::Start();
    if (xTaskCreate(InputPoll, "sdk_input", 4096, nullptr, 4, nullptr) != pdPASS) {
        task_failure++;
        dropped++;
    }

    Job job{};
    for (;;) {
        if (xQueueReceive(jobs, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        esp_err_t error = esp_lv_adapter_lock(200);
        if (error == ESP_OK) {
            lv_obj_t* screen = lv_screen_active();
            lv_obj_clean(screen);
            lv_obj_t* label = lv_label_create(screen);
            lv_label_set_text(label, job.text);
            lv_obj_set_width(label, 440);
            lv_obj_center(label);
            lv_obj_invalidate(screen);
            auto* display = LVAdapterDisplay::Instance();
            error = display != nullptr ? display->RefreshDiagnostic() : ESP_ERR_INVALID_STATE;
            esp_lv_adapter_unlock();
        }
        // Completion includes the panel driver result; human visibility is separate.
        job_result = error;
        job_done = job.id;
    }
}

}  // namespace

esp_err_t StartTransport() {
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return ESP_FAIL;
    }
    std::snprintf(device, sizeof(device), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);
    std::snprintf(boot, sizeof(boot), "%08lx%08lx", static_cast<unsigned long>(esp_random()),
                  static_cast<unsigned long>(esp_random()));
    {
        std::lock_guard<std::mutex> lock(scene_mutex);
        scene_version = "boot";
        scene_revision = 0;
    }
    scene_last_refresh_us = 0;

    input_events = xQueueCreate(64, sizeof(InputEvent));
    jobs = xQueueCreate(1, sizeof(Job));
    if (input_events == nullptr || jobs == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    usb_serial_jtag_driver_config_t config = {4096, 2048};
    esp_err_t error = usb_serial_jtag_driver_install(&config);
    if (error != ESP_OK) {
        return error;
    }
    // PAPER file/hash paths add stack frames; real-device low-water was 360 B
    // with 8 KiB. Retain headroom for diagnostics instead of approaching overflow.
    return xTaskCreate(Transport, "sdk_usb", 16384, nullptr, 5, nullptr) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void StartHardware() {
    if (xTaskCreate(Hardware, "sdk_board", 12288, nullptr, 3, nullptr) != pdPASS) {
        job_result = ESP_ERR_NO_MEM;
    }
}

void TouchSample(int count, int x, int y) {
    static InputEvent last{};
    static bool have_last = false;
    if (count < 0) {
        touch_read_errors++;
    } else {
        touch_reader_seen = true;
    }

    InputEvent event{};
    event.kind = InputKind::Touch;
    event.count = count;
    event.x = x;
    event.y = y;
    if (count > 0) {
        CopyText(event.name, sizeof(event.name), TouchRegion(x, y));
    }
    if (have_last && event.count == last.count && event.x == last.x && event.y == last.y) {
        return;
    }
    const bool new_press=count>0 && (!have_last || last.count==0);
    last = event;
    have_last = true;
    selftest::Input(event.name, count > 0, x, y);
    if (new_press) inkdesk_app::Input(event.name, true);
    QueueInput(event);
}

const char* DeviceId() { return device; }
const char* BootId() { return boot; }
bool AudioBusy() { return audio_tone_busy.load() || audio_sample_busy.load(); }

cJSON* DiagnosticCall(const char* command, cJSON* args) {
    auto* request = args != nullptr ? cJSON_Duplicate(args, true) : cJSON_CreateObject();
    cJSON_AddNumberToObject(request, "protocol", 1);
    cJSON_AddNumberToObject(request, "id", 0);
    cJSON_AddStringToObject(request, "cmd", command);
    char* raw = cJSON_PrintUnformatted(request);
    cJSON* reply = nullptr;
    Request(raw, &reply);
    cJSON_free(raw);
    cJSON_Delete(request);
    return reply;
}

cJSON* RecordReplay(const char* path) {
    auto* result = cJSON_CreateObject();
    auto* codec = GetAudioCodecSafe();
    if (codec == nullptr || audio_tone_busy.load() || audio_sample_busy.exchange(true)) {
        Str(result, "result", "FAIL");
        return result;
    }
    LocalAudioLease route;
    if(!route.ready()){
        audio_sample_busy=false;Str(result,"result","FAIL");
        Str(result,"error","本地音频通道未就绪或正在使用蓝牙");return result;
    }
    if (!audio_started.exchange(true)) codec->Start();
    const int volume = codec->output_volume();
    const bool input = codec->input_enabled(), output = codec->output_enabled();
    auto& io = IOExpander::getInstance();
    uint8_t pa = 0;
    bool ok = io.getLevel(IOExpander::Pin::PA, &pa) == ESP_OK;
    if (ok) ok = io.setLevel(IOExpander::Pin::PA, false) == ESP_OK;
    codec->EnableInput(true);
    codec->EnableOutput(false);
    // Three seconds, deinterleaving the official microphone channel (channel 0).
    std::vector<int16_t> mono;
    mono.reserve(48000);
    std::vector<int16_t> chunk(640);
    for (int n = 0; ok && n < 150; ++n) {
        int received = codec->InputDataChecked(chunk);
        if (received != 640) { ok = false; break; }
        for (int i = 0; i < received; i += 2) mono.push_back(chunk[i]);
    }
    FILE* file = ok ? std::fopen(path, "wb") : nullptr;
    bool saved = false;
    if (file != nullptr) {
        // Standard 16kHz mono PCM WAV, little endian ESP32 target.
        uint32_t bytes = mono.size() * 2;
        uint8_t header[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
            16,0,0,0,1,0,1,0,0x80,0x3e,0,0,0,0x7d,0,0,2,0,16,0,'d','a','t','a',0,0,0,0};
        uint32_t riff = bytes + 36;
        std::memcpy(header + 4, &riff, 4);
        std::memcpy(header + 40, &bytes, 4);
        saved = std::fwrite(header, 1, 44, file) == 44 &&
            std::fwrite(mono.data(), 2, mono.size(), file) == mono.size();
        saved = std::fclose(file) == 0 && saved;
    }
    ok = ok && saved;
    int played = 0;
    if (ok && io.setLevel(IOExpander::Pin::PA, true) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(80));
        codec->SetDiagnosticVolume(80);
        codec->EnableOutput(true);
        for (size_t i = 0; i < mono.size(); i += 320) {
            std::vector<int16_t> block(mono.begin() + i, mono.begin() + std::min(i + 320, mono.size()));
            const int wrote = codec->OutputDataChecked(block);
            played += wrote;
            if (wrote != static_cast<int>(block.size())) { ok = false; break; }
        }
    } else ok = false;
    vTaskDelay(pdMS_TO_TICKS(120));
    io.setLevel(IOExpander::Pin::PA, pa != 0);
    codec->SetDiagnosticVolume(volume);
    codec->EnableInput(input);
    codec->EnableOutput(output);
    audio_sample_busy = false;
    Num(result, "recorded_samples", mono.size());
    Num(result, "played_samples", played);
    AddBool(result, "wav_saved", saved);
    Str(result, "path", path);
    Str(result, "result", ok ? "PASS" : "FAIL");
    return result;
}

}  // namespace personal_sdk
