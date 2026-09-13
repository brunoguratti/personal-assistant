#include "device_timer.h"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "application.h"
#include "assets/lang_config.h"

static const char* TAG = "DeviceTimer";

namespace {

constexpr int kMinDurationSeconds = 1;
constexpr int kMaxDurationSeconds = 24 * 60 * 60;
constexpr int64_t kMicrosecondsPerSecond = 1'000'000LL;
constexpr size_t kMaxLabelLength = 64;
constexpr int64_t kAlarmRepeatIntervalUs = 2 * kMicrosecondsPerSecond;

struct TimerState {
    esp_timer_handle_t handle = nullptr;
    esp_timer_handle_t alarm_repeat_handle = nullptr;

    bool active = false;
    bool alarm_active = false;

    int64_t deadline_us = 0;
    std::string label = "Timer";
};

TimerState g_timer;

int64_t NowUs() { return esp_timer_get_time(); }

int RemainingSeconds() {
    if (!g_timer.active) {
        return 0;
    }

    const int64_t remaining_us = g_timer.deadline_us - NowUs();

    if (remaining_us <= 0) {
        return 0;
    }

    // Round upward so that 0.1 seconds remaining is reported as 1 second.
    return static_cast<int>((remaining_us + kMicrosecondsPerSecond - 1) / kMicrosecondsPerSecond);
}

void PlayAlarmSound() {
    Application::GetInstance().Schedule([]() {
        ESP_LOGW(TAG, "Playing timer alarm sound");
        Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
    });
}

void AlarmRepeatCallback(void*) {
    if (!g_timer.alarm_active) {
        return;
    }

    PlayAlarmSound();
}

void StopAlarmIfNeeded() {
    if (g_timer.alarm_repeat_handle == nullptr ||
        !esp_timer_is_active(g_timer.alarm_repeat_handle)) {
        return;
    }

    const esp_err_t error = esp_timer_stop(g_timer.alarm_repeat_handle);

    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Unable to stop alarm repeat timer: %s", esp_err_to_name(error));
    }
}

void StartAlarmRepeating() {
    if (g_timer.alarm_repeat_handle == nullptr) {
        ESP_LOGE(TAG, "Alarm repeat timer has not been created");
        return;
    }

    StopAlarmIfNeeded();

    const esp_err_t error = esp_timer_start_periodic(g_timer.alarm_repeat_handle,
                                                     static_cast<uint64_t>(kAlarmRepeatIntervalUs));

    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Unable to start repeated alarm: %s", esp_err_to_name(error));
    }
}

void TimerExpiredCallback(void*) {
    g_timer.active = false;
    g_timer.alarm_active = true;
    g_timer.deadline_us = 0;

    ESP_LOGW(TAG, "Timer expired: label=%s", g_timer.label.c_str());

    // Play immediately at expiry, then repeat every few seconds until cancelled.
    PlayAlarmSound();
    StartAlarmRepeating();
}

void EnsureTimerCreated() {
    if (g_timer.handle == nullptr) {
        const esp_timer_create_args_t timer_args = {
            .callback = &TimerExpiredCallback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "device_timer",
            .skip_unhandled_events = false,
        };

        const esp_err_t error = esp_timer_create(&timer_args, &g_timer.handle);

        if (error != ESP_OK) {
            throw std::runtime_error(std::string("Unable to create device timer: ") +
                                     esp_err_to_name(error));
        }
    }

    if (g_timer.alarm_repeat_handle == nullptr) {
        const esp_timer_create_args_t alarm_repeat_args = {
            .callback = &AlarmRepeatCallback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "device_alarm_repeat",
            .skip_unhandled_events = false,
        };

        const esp_err_t error = esp_timer_create(&alarm_repeat_args, &g_timer.alarm_repeat_handle);

        if (error != ESP_OK) {
            throw std::runtime_error(std::string("Unable to create alarm repeat timer: ") +
                                     esp_err_to_name(error));
        }
    }
}

void StopActiveTimerIfNeeded() {
    if (g_timer.handle == nullptr || !esp_timer_is_active(g_timer.handle)) {
        return;
    }

    const esp_err_t error = esp_timer_stop(g_timer.handle);

    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Unable to stop existing timer: %s", esp_err_to_name(error));
    }
}

cJSON* CreateStatusResult() {
    cJSON* result = cJSON_CreateObject();

    if (result == nullptr) {
        throw std::runtime_error("Unable to create timer result");
    }

    cJSON_AddBoolToObject(result, "active", g_timer.active);
    cJSON_AddBoolToObject(result, "alarm_active", g_timer.alarm_active);
    cJSON_AddNumberToObject(result, "remaining_seconds", RemainingSeconds());
    cJSON_AddStringToObject(result, "label", g_timer.label.c_str());

    return result;
}

std::string NormalizedLabel(std::string label) {
    if (label.empty()) {
        return "Timer";
    }

    if (label.size() > kMaxLabelLength) {
        label.resize(kMaxLabelLength);
    }

    return label;
}

}  // namespace

void RegisterDeviceTimerTools(McpServer& server) {
    EnsureTimerCreated();

    ESP_LOGI(TAG, "Registering MCP tools: device.timer.start/status/cancel");

    server.AddTool(
        "device.timer.start",
        "Start or replace one timer on this device. "
        "The local alarm triggers at expiry even during temporary network loss.",
        PropertyList({
            Property("duration_seconds", kPropertyTypeInteger, 60, kMinDurationSeconds,
                     kMaxDurationSeconds),
            Property("label", kPropertyTypeString, std::string("Timer")),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            const int duration_seconds = properties["duration_seconds"].value<int>();

            if (duration_seconds < kMinDurationSeconds || duration_seconds > kMaxDurationSeconds) {
                throw std::runtime_error("duration_seconds must be between 1 and 86400");
            }

            const std::string label = NormalizedLabel(properties["label"].value<std::string>());

            EnsureTimerCreated();

            // A newly requested timer replaces any previously running timer.
            StopActiveTimerIfNeeded();
            StopAlarmIfNeeded();

            g_timer.active = true;
            g_timer.alarm_active = false;
            g_timer.label = label;
            g_timer.deadline_us =
                NowUs() + static_cast<int64_t>(duration_seconds) * kMicrosecondsPerSecond;

            const esp_err_t error = esp_timer_start_once(
                g_timer.handle, static_cast<uint64_t>(duration_seconds) * kMicrosecondsPerSecond);

            if (error != ESP_OK) {
                g_timer.active = false;
                g_timer.deadline_us = 0;

                throw std::runtime_error(std::string("Unable to start timer: ") +
                                         esp_err_to_name(error));
            }

            ESP_LOGI(TAG, "Timer started: duration=%d label=%s", duration_seconds,
                     g_timer.label.c_str());

            return CreateStatusResult();
        });

    server.AddTool("device.timer.status",
                   "Get the current local timer state and the number of seconds remaining.",
                   PropertyList(),
                   [](const PropertyList&) -> ReturnValue { return CreateStatusResult(); });

    server.AddTool("device.timer.cancel",
                   "Cancel the active local device timer and clear its alarm state.",
                   PropertyList(), [](const PropertyList&) -> ReturnValue {
                       const bool was_active = g_timer.active || g_timer.alarm_active;

                       StopActiveTimerIfNeeded();
                       StopAlarmIfNeeded();

                       g_timer.active = false;
                       g_timer.alarm_active = false;
                       g_timer.deadline_us = 0;
                       g_timer.label = "Timer";

                       cJSON* result = cJSON_CreateObject();

                       if (result == nullptr) {
                           throw std::runtime_error("Unable to create timer result");
                       }

                       cJSON_AddBoolToObject(result, "cancelled", was_active);
                       cJSON_AddBoolToObject(result, "active", false);
                       cJSON_AddBoolToObject(result, "alarm_active", false);
                       cJSON_AddNumberToObject(result, "remaining_seconds", 0);
                       cJSON_AddStringToObject(result, "label", "Timer");

                       ESP_LOGI(TAG, "Timer cancelled");

                       return result;
                   });
}