#pragma once

#include "mcp_server.h"

void RegisterDeviceTimerTools(McpServer& server);

#include <string>

struct DeviceTimerStatus {
    bool active;
    bool alarm_active;
    int remaining_seconds;
    std::string label;
};

DeviceTimerStatus GetDeviceTimerStatus();
bool StartDeviceTimer(int duration_seconds, const std::string& label, std::string& error_message);
bool CancelDeviceTimer();