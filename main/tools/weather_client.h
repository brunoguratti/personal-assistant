#pragma once

#include <string>
#include "mcp_server.h"

// Registers the weather MCP tool on the given McpServer instance.
void RegisterWeatherTool(McpServer& server);
