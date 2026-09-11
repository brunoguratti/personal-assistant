#include "device_timer.h"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <cJSON.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "DeviceTimer";

namespace {

constexpr int kMinDurationSeconds = 1;
constexpr int kMaxDurationSeconds = 24 * 60 * 60;
constexpr int64_t kMicrosecondsPerSecond = 1000000LL;

struct TimerState {
    esp_timer_handle_t handle = nullptr;
    bool active = false;
    bool alarm_active = false;
    int64_t deadline_us = 0;
    std::string label = "Timer";
};

TimerState g_timer;


int64_t NowUs() {
    return esp_timer_get_time();
}


int RemainingSeconds() {
    if (!g_timer.active) {
        return 0;
    }

    const int64_t remaining_us = g_timer.deadline_us - NowUs();

    if (remaining_us <= 0) {
        return 0;
    }

    return static_cast<int>(
        (remaining_us + kMicrosecondsPerSecond - 1) / kMicrosecondsPerSecond
    );
}


void TimerExpiredCallback(void*) {
    g_timer.active = false;
    g_timer.alarm_active = true;
    g_timer.deadline_us = 0;

    ESP_LOGW(TAG, "Timer expired: label=%s", g_timer.label.c_str());

    // Phase 1: log-only alarm. This confirms that the timer works.
    // Later we will safely add sound, LEDs, and display behavior.
    ESP_LOGW(TAG, "ALARM: Timer expired");
}


void EnsureTimerCreated() {
    if (g_timer.handle != nullptr) {
        return;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = &TimerExpiredCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "device_timer",
        .skip_unhandled_events = false,
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_timer.handle));
}


void StopActiveTimerIfNeeded() {
    if (g_timer.handle != nullptr && esp_timer_is_active(g_timer.handle)) {
        const esp_err_t error = esp_timer_stop(g_timer.handle);

        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Unable to stop existing timer: %s", esp_err_to_name(error));
        }
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

}  // namespace


void RegisterDeviceTimerTools(McpServer& server) {
    EnsureTimerCreated();

    ESP_LOGI(TAG, "Registering MCP tools: device.timer.start/status/cancel");

    server.AddTool(
        "device.timer.start",
        "Start or replace one timer on this device. "
        "It triggers a local alarm at expiry even during temporary network loss.",
        PropertyList({
            Property(
                "duration_seconds",
                kPropertyTypeInteger,
                60,
                kMinDurationSeconds,
                kMaxDurationSeconds
            ),
            Property("label", kPropertyTypeString, std::string("Timer")),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            const int duration_seconds =
                properties["duration_seconds"].value<int>();

            std::string label =
                properties["label"].value<std::string>();

            if (duration_seconds < kMinDurationSeconds ||
                duration_seconds > kMaxDurationSeconds) {
                throw std::runtime_error(
                    "duration_seconds must be between 1 and 86400"
                );
            }

            if (label.empty()) {
                label = "Timer";
            }

            if (label.size() > 64) {
                label.resize(64);
            }

            EnsureTimerCreated();
            StopActiveTimerIfNeeded();

            g_timer.active = true;
            g_timer.alarm_active = false;
            g_timer.label = label;
            g_timer.deadline_us =
                NowUs() + static_cast<int64_t>(duration_seconds) * kMicrosecondsPerSecond;

            const esp_err_t error = esp_timer_start_once(
                g_timer.handle,
                static_cast<uint64_t>(duration_seconds) * kMicrosecondsPerSecond
            );

            if (error != ESP_OK) {
                g_timer.active = false;
                g_timer.deadline_us = 0;

                throw std::runtime_error(
                    std::string("Unable to start timer: ") + esp_err_to_name(error)
                );
            }

            ESP_LOGI(
                TAG,
                "Timer started: duration=%d label=%s",
                duration_seconds,
                g_timer.label.c_str()
            );

            return CreateStatusResult();
        }
    );

    server.AddTool(
        "device.timer.status",
        "Get the current local timer state and the number of seconds remaining.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return CreateStatusResult();
        }
    );

    server.AddTool(
        "device.timer.cancel",
        "Cancel the active local device timer and clear its alarm state.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            const bool was_active = g_timer.active || g_timer.alarm_active;

            StopActiveTimerIfNeeded();

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

            ESP_LOGI(TAG, "Timer cancelled");

            return result;
        }
    );
}
