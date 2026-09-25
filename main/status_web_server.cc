#include "status_web_server.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <cJSON.h>

#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "device_timer.h"
#include "system_info.h"

namespace {
constexpr char kTag[] = "StatusWebServer";

httpd_handle_t server_handle = nullptr;

constexpr size_t kMaxTimerRequestBytes = 256;
esp_err_t SendJson(httpd_req_t* request, const std::string& json, const char* status = nullptr) {
    if (status != nullptr) {
        httpd_resp_set_status(request, status);
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json.c_str(), static_cast<ssize_t>(json.size()));
}

std::string BuildJsonString(const cJSON* root) {
    char* rendered = cJSON_PrintUnformatted(root);
    if (rendered == nullptr) {
        return {};
    }

    std::string json(rendered);
    cJSON_free(rendered);
    return json;
}

std::string GetDeviceIpAddress() {
    esp_netif_t* netif = esp_netif_get_default_netif();

    if (netif == nullptr) {
        return {};
    }

    esp_netif_ip_info_t ip_info{};
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        return {};
    }

    char ip_address[16] = {};
    if (esp_ip4addr_ntoa(&ip_info.ip, ip_address, sizeof(ip_address)) == nullptr) {
        return {};
    }

    return std::string(ip_address);
}

bool ReadRequestBody(httpd_req_t* request, char* buffer, size_t buffer_size,
                     size_t& received_length) {
    received_length = 0;

    if (request->content_len <= 0 || static_cast<size_t>(request->content_len) >= buffer_size) {
        return false;
    }

    const size_t expected_length = static_cast<size_t>(request->content_len);

    while (received_length < expected_length) {
        const int result =
            httpd_req_recv(request, buffer + received_length, expected_length - received_length);

        if (result <= 0) {
            return false;
        }

        received_length += static_cast<size_t>(result);
    }

    buffer[received_length] = '\0';
    return true;
}

constexpr char kHealthHtml[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Health · XiaoZhi Dashboard</title>
<style>
:root { color-scheme: dark; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
* { box-sizing: border-box; }
body { background: #10131a; color: #edf2f7; margin: 0; padding: 20px; }
main { max-width: 820px; margin: auto; }
h1 { margin: 0 0 6px; font-size: 1.65rem; }
.nav { display: flex; flex-wrap: wrap; gap: 8px; margin: 18px 0 20px; }
.nav a { background: #1b202b; border: 1px solid #30394a; border-radius: 7px; color: #cbd5e1; font-weight: 700; padding: 8px 11px; text-decoration: none; }
.nav a:hover { background: #2563eb; border-color: #2563eb; color: white; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(190px, 1fr)); gap: 12px; }
.card { background: #1b202b; border: 1px solid #30394a; border-radius: 12px; padding: 16px; }
.card-wide { grid-column: 1 / -1; }
.label { color: #a0aec0; font-size: .78rem; font-weight: 700; letter-spacing: .08em; margin-bottom: 7px; text-transform: uppercase; }
.value { color: #f8fafc; font-size: 1.18rem; font-weight: 700; }
.muted { color: #a0aec0; font-size: .9rem; }
.status { display: inline-block; background: #166534; border-radius: 999px; font-weight: 700; padding: 6px 10px; margin-bottom: 18px; }
</style>
</head>
<body>
<main>
  <h1>Health dashboard</h1>
  <div id="updated" class="muted">Loading health data…</div>
  <nav class="nav">
    <a href="/tools">Tools</a>
  </nav>
  <div id="status" class="status">Checking device…</div>
  <div class="grid">
    <section class="card"><div class="label">Free SRAM</div><div id="free-sram" class="value">—</div></section>
    <section class="card"><div class="label">Minimum SRAM</div><div id="minimum-sram" class="value">—</div></section>
    <section class="card"><div class="label">Free PSRAM</div><div id="free-psram" class="value">—</div></section>
    <section class="card"><div class="label">Uptime</div><div id="uptime" class="value">—</div></section>
    <section class="card card-wide"><div class="label">Device information</div><div id="device-info" class="muted">—</div></section>
  </div>
</main>
<script>
function formatBytes(bytes) { return Number.isFinite(bytes) ? `${Math.round(bytes / 1024)} KB` : "—"; }
function formatDuration(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return "—";
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  return h > 0 ? `${h}h ${m}m ${s}s` : m > 0 ? `${m}m ${s}s` : `${s}s`;
}
async function refreshHealth() {
  const status = document.getElementById("status");
  try {
    const response = await fetch("/api/health", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    document.getElementById("free-sram").textContent = formatBytes(data.free_heap_bytes);
    document.getElementById("minimum-sram").textContent = formatBytes(data.minimum_free_heap_bytes);
    document.getElementById("free-psram").textContent = formatBytes(data.free_psram_bytes);
    document.getElementById("uptime").textContent = formatDuration(data.uptime_seconds);
    document.getElementById("device-info").textContent = `${data.chip || "Unknown chip"} · ${data.firmware || "Unknown firmware"} · ${data.mac || "Unknown MAC"}`;
    document.getElementById("updated").textContent = `Updated ${new Date().toLocaleTimeString()}`;
    status.textContent = "Device online";
    status.style.background = "#166534";
  } catch (error) {
    status.textContent = "Health unavailable";
    status.style.background = "#991b1b";
    document.getElementById("updated").textContent = `Health update failed: ${error.message}`;
  }
}
refreshHealth();
setInterval(refreshHealth, 3000);
</script>
</body>
</html>
)HTML";

constexpr char kToolsHtml[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Tools · XiaoZhi Dashboard</title>
<style>
:root { color-scheme: dark; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
* { box-sizing: border-box; }
body { background: #10131a; color: #edf2f7; margin: 0; padding: 20px; }
main { max-width: 820px; margin: auto; }
h1 { margin: 0 0 6px; font-size: 1.65rem; }
h2 { margin: 0; font-size: 1.2rem; }
a { color: inherit; text-decoration: none; }
.nav { display: flex; flex-wrap: wrap; gap: 8px; margin: 18px 0 20px; }
.nav a { background: #1b202b; border: 1px solid #30394a; border-radius: 7px; color: #cbd5e1; font-weight: 700; padding: 8px 11px; }
.nav a:hover { background: #2563eb; border-color: #2563eb; color: white; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(190px, 1fr)); gap: 12px; }
.card { background: #1b202b; border: 1px solid #30394a; border-radius: 12px; padding: 16px; }
.tool-card { display: block; transition: border-color .15s, transform .15s; }
.tool-card:hover { border-color: #60a5fa; transform: translateY(-1px); }
.label { color: #a0aec0; font-size: .78rem; font-weight: 700; letter-spacing: .08em; margin-bottom: 7px; text-transform: uppercase; }
.muted { color: #a0aec0; font-size: .9rem; }
</style>
</head>
<body>
<main>
  <h1>Tools</h1>
  <div class="muted">Choose a device or personal productivity tool.</div>
  <nav class="nav">
    <a href="/">Health</a>
  </nav>
  <div class="grid">
      <a class="card tool-card" href="/tools/timer">
    <div class="label">Local device</div>
    <h2>Timer</h2>
    <p class="muted">Start, monitor, or cancel a timer running on the device.</p>
  </a>
  </div>
</main>
</body>
</html>
)HTML";

constexpr char kTimerHtml[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Timer · XiaoZhi Dashboard</title>
<style>
:root { color-scheme: dark; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
* { box-sizing: border-box; }
body { background: #10131a; color: #edf2f7; margin: 0; padding: 20px; }
main { max-width: 820px; margin: auto; }
h1 { margin: 0 0 6px; font-size: 1.65rem; }
.nav { display: flex; flex-wrap: wrap; gap: 8px; margin: 18px 0 20px; }
.nav a { background: #1b202b; border: 1px solid #30394a; border-radius: 7px; color: #cbd5e1; font-weight: 700; padding: 8px 11px; text-decoration: none; }
.nav a:hover { background: #2563eb; border-color: #2563eb; color: white; }
.card { background: #1b202b; border: 1px solid #30394a; border-radius: 12px; padding: 16px; }
.label { color: #a0aec0; font-size: .78rem; font-weight: 700; letter-spacing: .08em; margin-bottom: 7px; text-transform: uppercase; }
.value { color: #f8fafc; font-size: 1.18rem; font-weight: 700; }
.muted { color: #a0aec0; font-size: .9rem; }
.controls { display: grid; gap: 10px; margin-top: 12px; }
input { background: #10131a; border: 1px solid #4a5568; border-radius: 7px; color: #edf2f7; font: inherit; padding: 9px 10px; width: 100%; }
button { background: #2563eb; border: 0; border-radius: 7px; color: white; cursor: pointer; font: inherit; font-weight: 700; padding: 9px 12px; }
button:hover { background: #1d4ed8; }
button.cancel { background: #b91c1c; }
button.cancel:hover { background: #991b1b; }
.message { color: #a0aec0; font-size: .9rem; min-height: 1.2em; }
</style>
</head>
<body>
<main>
  <h1>Local timer</h1>
  <div class="muted">This timer runs locally on the XiaoZhi device.</div>
  <nav class="nav">
    <a href="/">Health</a>
    <a href="/tools">Tools</a>
  </nav>
  <section class="card">
    <div class="label">Current timer</div>
    <div id="timer-state" class="value">Loading…</div>
    <div id="timer-detail" class="muted"></div>
    <div class="controls">
      <label><span class="label">Timer label</span><input id="timer-label-input" type="text" maxlength="64" value="Timer"></label>
      <label><span class="label">Duration in seconds</span><input id="timer-seconds-input" type="number" min="1" max="86400" value="60"></label>
      <button id="timer-start-button" type="button">Start timer</button>
      <button id="timer-cancel-button" class="cancel" type="button">Cancel timer</button>
      <div id="timer-message" class="message"></div>
    </div>
  </section>
</main>
<script>
function formatDuration(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return "—";
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  return h > 0 ? `${h}h ${m}m ${s}s` : m > 0 ? `${m}m ${s}s` : `${s}s`;
}
async function refreshTimer() {
  const state = document.getElementById("timer-state");
  const detail = document.getElementById("timer-detail");
  try {
    const response = await fetch("/api/timer", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    if (data.active) {
      state.textContent = data.label || "Timer active";
      detail.textContent = `${formatDuration(data.remaining_seconds)} remaining${data.alarm_active ? " · alarm active" : ""}`;
    } else {
      state.textContent = "No active timer";
      detail.textContent = "";
    }
  } catch (error) {
    state.textContent = "Timer unavailable";
    detail.textContent = error.message;
  }
}
async function startTimer() {
  const message = document.getElementById("timer-message");
  const seconds = Number(document.getElementById("timer-seconds-input").value);
  const label = document.getElementById("timer-label-input").value.trim() || "Timer";
  if (!Number.isInteger(seconds) || seconds < 1 || seconds > 86400) {
    message.textContent = "Enter a duration from 1 to 86,400 seconds.";
    return;
  }
  try {
    const response = await fetch("/api/timer", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ duration_seconds: seconds, label: label })
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    message.textContent = "Timer started.";
    await refreshTimer();
  } catch (error) {
    message.textContent = `Could not start timer: ${error.message}`;
  }
}
async function cancelTimer() {
  const message = document.getElementById("timer-message");
  try {
    const response = await fetch("/api/timer", { method: "DELETE" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    message.textContent = "Timer cancelled.";
    await refreshTimer();
  } catch (error) {
    message.textContent = `Could not cancel timer: ${error.message}`;
  }
}
document.getElementById("timer-start-button").addEventListener("click", startTimer);
document.getElementById("timer-cancel-button").addEventListener("click", cancelTimer);
refreshTimer();
setInterval(refreshTimer, 1000);
</script>
</body>
</html>
)HTML";

esp_err_t HtmlPageHandler(httpd_req_t* request, const std::string& html) {
    httpd_resp_set_type(request, "text/html");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, html.c_str(), static_cast<ssize_t>(html.size()));
}

esp_err_t ToolsPageHandler(httpd_req_t* request) { return HtmlPageHandler(request, kToolsHtml); }

esp_err_t TimerPageHandler(httpd_req_t* request) { return HtmlPageHandler(request, kTimerHtml); }

esp_err_t HealthHandler(httpd_req_t* request) {
    const int64_t uptime_seconds = esp_timer_get_time() / 1000000LL;

    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Unable to build health response");
    }

    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "firmware", SystemInfo::GetUserAgent().c_str());
    cJSON_AddStringToObject(root, "chip", SystemInfo::GetChipModelName().c_str());
    cJSON_AddStringToObject(root, "mac", SystemInfo::GetMacAddress().c_str());
    cJSON_AddNumberToObject(root, "uptime_seconds", static_cast<double>(uptime_seconds));
    cJSON_AddNumberToObject(root, "free_heap_bytes",
                            static_cast<double>(SystemInfo::GetFreeHeapSize()));
    cJSON_AddNumberToObject(root, "minimum_free_heap_bytes",
                            static_cast<double>(SystemInfo::GetMinimumFreeHeapSize()));
    cJSON_AddNumberToObject(root, "free_psram_bytes",
                            static_cast<double>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    cJSON_AddNumberToObject(root, "reset_reason", static_cast<double>(esp_reset_reason()));

    const std::string json = BuildJsonString(root);
    cJSON_Delete(root);

    if (json.empty()) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Unable to render health response");
    }

    return SendJson(request, json);
}

esp_err_t TimerStatusHandler(httpd_req_t* request) {
    const DeviceTimerStatus timer = GetDeviceTimerStatus();

    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Unable to build timer response");
    }

    cJSON_AddBoolToObject(root, "active", timer.active);
    cJSON_AddBoolToObject(root, "alarm_active", timer.alarm_active);
    cJSON_AddNumberToObject(root, "remaining_seconds", timer.remaining_seconds);
    cJSON_AddStringToObject(root, "label", timer.label.c_str());

    const std::string json = BuildJsonString(root);
    cJSON_Delete(root);

    if (json.empty()) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Unable to render timer response");
    }

    return SendJson(request, json);
}

esp_err_t TimerStartHandler(httpd_req_t* request) {
    if (request->content_len <= 0 || request->content_len > kMaxTimerRequestBytes) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Timer request must be 1 to 256 bytes");
    }

    char body[kMaxTimerRequestBytes + 1] = {};
    size_t received_length = 0;

    if (!ReadRequestBody(request, body, sizeof(body), received_length)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Unable to read timer request");
    }

    cJSON* root = cJSON_ParseWithLength(body, received_length);
    if (root == nullptr) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Request body must be valid JSON");
    }

    const cJSON* duration = cJSON_GetObjectItemCaseSensitive(root, "duration_seconds");
    const cJSON* label = cJSON_GetObjectItemCaseSensitive(root, "label");

    if (!cJSON_IsNumber(duration) || duration->valuedouble != duration->valueint) {
        cJSON_Delete(root);

        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "duration_seconds must be an integer");
    }

    std::string timer_label = "Timer";

    if (label != nullptr) {
        if (!cJSON_IsString(label) || label->valuestring == nullptr) {
            cJSON_Delete(root);

            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "label must be a string");
        }

        timer_label = label->valuestring;
    }

    std::string error_message;
    const bool started = StartDeviceTimer(duration->valueint, timer_label, error_message);

    cJSON_Delete(root);

    if (!started) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, error_message.c_str());
    }

    return TimerStatusHandler(request);
}

esp_err_t TimerCancelHandler(httpd_req_t* request) {
    CancelDeviceTimer();
    return TimerStatusHandler(request);
}

esp_err_t HealthDashboardPageHandler(httpd_req_t* request) {
    return HtmlPageHandler(request, kHealthHtml);
}

const httpd_uri_t kHealthDashboardUri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = HealthDashboardPageHandler,
    .user_ctx = nullptr,
};

const httpd_uri_t kToolsPageUri = {
    .uri = "/tools",
    .method = HTTP_GET,
    .handler = ToolsPageHandler,
    .user_ctx = nullptr,
};

const httpd_uri_t kTimerPageUri = {
    .uri = "/tools/timer",
    .method = HTTP_GET,
    .handler = TimerPageHandler,
    .user_ctx = nullptr,
};

const httpd_uri_t kHealthUri = {
    .uri = "/api/health",
    .method = HTTP_GET,
    .handler = HealthHandler,
    .user_ctx = nullptr,
};

const httpd_uri_t kTimerStatusUri = {
    .uri = "/api/timer",
    .method = HTTP_GET,
    .handler = TimerStatusHandler,
    .user_ctx = nullptr,
};

const httpd_uri_t kTimerStartUri = {
    .uri = "/api/timer",
    .method = HTTP_POST,
    .handler = TimerStartHandler,
    .user_ctx = nullptr,
};

const httpd_uri_t kTimerCancelUri = {
    .uri = "/api/timer",
    .method = HTTP_DELETE,
    .handler = TimerCancelHandler,
    .user_ctx = nullptr,
};

bool RegisterUri(httpd_handle_t server, const httpd_uri_t& uri, const char* name) {
    const esp_err_t result = httpd_register_uri_handler(server, &uri);

    if (result == ESP_OK) {
        return true;
    }

    ESP_LOGE(kTag, "Failed to register %s: %s", name, esp_err_to_name(result));
    return false;
}

}  // namespace

StatusWebServer& StatusWebServer::GetInstance() {
    static StatusWebServer instance;
    return instance;
}

void StatusWebServer::Start() {
    if (server_handle != nullptr) {
        ESP_LOGD(kTag, "Server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    esp_err_t result = httpd_start(&server_handle, &config);

    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start HTTP server: %s", esp_err_to_name(result));
        server_handle = nullptr;
        return;
    }

    const bool registered =
        RegisterUri(server_handle, kHealthDashboardUri, "GET /") &&
        RegisterUri(server_handle, kToolsPageUri, "GET /tools") &&
        RegisterUri(server_handle, kTimerPageUri, "GET /tools/timer") &&
        RegisterUri(server_handle, kHealthUri, "GET /api/health") &&
        RegisterUri(server_handle, kTimerStatusUri, "GET /api/timer") &&
        RegisterUri(server_handle, kTimerStartUri, "POST /api/timer") &&
        RegisterUri(server_handle, kTimerCancelUri, "DELETE /api/timer");

    if (!registered) {
        httpd_stop(server_handle);
        server_handle = nullptr;
        return;
    }

    const std::string ip_address = GetDeviceIpAddress();

    if (ip_address.empty() || ip_address == "0.0.0.0") {
        ESP_LOGW(kTag, "Dashboard started, but no IPv4 address is available yet");
        ESP_LOGI(kTag, "Health dashboard: http://<device-ip>/");
        ESP_LOGI(kTag, "Tools: http://<device-ip>/tools");
        return;
    }

    ESP_LOGI(kTag, "Health dashboard: http://%s/", ip_address.c_str());
    ESP_LOGI(kTag, "Tools: http://%s/tools", ip_address.c_str());
    ESP_LOGI(kTag, "Timer: http://%s/tools/timer", ip_address.c_str());
}
