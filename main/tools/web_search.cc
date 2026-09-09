#include "web_search.h"
#include <esp_log.h>
#include <cJSON.h>
#include <string>
#include "board.h"
#include "secrets.h"

static const char* TAG = "WebSearch";

void RegisterWebSearchTool(McpServer& server) {
    ESP_LOGI(TAG, "Registering MCP tool: web.search");
    server.AddTool(
        "web.search",
        "Search the web using Firecrawl. Args: query (string, English), limit (int, optional)",
        PropertyList({
            Property("query", kPropertyTypeString),
            Property("limit", kPropertyTypeInteger, 1, 1, 10),
            Property("api_key", kPropertyTypeString, std::string(FIRECRAWL_API_KEY)),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto query = properties["query"].value<std::string>();
            int limit = properties["limit"].value<int>();
            std::string api_key = properties["api_key"].value<std::string>();

            if (query.empty()) {
                throw std::runtime_error("query is required (English)");
            }

            ESP_LOGI(TAG, "web.search invoked - query=%s limit=%d", query.c_str(), limit);

            auto& board = Board::GetInstance();
            // Increase HTTP timeout to 10000 (ms)
            auto http = board.GetNetwork()->CreateHttp(10000);

            std::string url = "https://api.firecrawl.dev/v2/search";

            // Build JSON payload
            cJSON* payload = cJSON_CreateObject();
            cJSON* sources = cJSON_CreateArray();
            cJSON_AddItemToArray(sources, cJSON_CreateString("web"));
            cJSON_AddItemToObject(payload, "sources", sources);
            cJSON_AddItemToObject(payload, "categories", cJSON_CreateArray());
            cJSON_AddNumberToObject(payload, "limit", limit);

            cJSON* scrapeOptions = cJSON_CreateObject();
            cJSON_AddBoolToObject(scrapeOptions, "onlyMainContent", true);
            cJSON_AddNumberToObject(scrapeOptions, "maxAge", 172800000);
            cJSON_AddItemToObject(scrapeOptions, "parsers", cJSON_CreateArray());
            cJSON_AddItemToObject(scrapeOptions, "formats", cJSON_CreateArray());
            cJSON_AddItemToObject(payload, "scrapeOptions", scrapeOptions);

            // Add the query text (must be English per your request)
            cJSON_AddStringToObject(payload, "query", query.c_str());

            char* payload_str = cJSON_PrintUnformatted(payload);
            std::string content(payload_str);
            cJSON_free(payload_str);
            cJSON_Delete(payload);

            http->SetHeader("User-Agent", "XiaoZhi/1.0");
            http->SetHeader("Authorization", std::string("Bearer ") + api_key);
            http->SetHeader("Content-Type", "application/json");
            http->SetContent(std::move(content));

            if (!http->Open("POST", url)) {
                throw std::runtime_error("Failed to open Firecrawl API URL: " + url);
            }

            http->Write("", 0);

            int status = http->GetStatusCode();
            std::string resp = http->ReadAll();
            http->Close();

            // Try to parse response as JSON
            cJSON* parsed = cJSON_Parse(resp.c_str());
            if (!parsed) {
                cJSON* result = cJSON_CreateObject();
                cJSON_AddNumberToObject(result, "status", status);
                cJSON_AddStringToObject(result, "response", resp.c_str());
                return result;
            }

            // Build a summarized result: items[] with { title, description }
            cJSON* summary = cJSON_CreateObject();
            cJSON_AddNumberToObject(summary, "status", status);
            cJSON* items = cJSON_CreateArray();
            std::string summary_text;

            cJSON* data = cJSON_GetObjectItem(parsed, "data");
            if (data) {
                cJSON* web = cJSON_GetObjectItem(data, "web");
                if (cJSON_IsArray(web)) {
                    int available = cJSON_GetArraySize(web);
                    int to_take = (limit > 0) ? std::min(limit, available) : available;
                    // Build a short plain-text summary for quick TTS output
                    std::string summary_text;
                    for (int i = 0; i < to_take; ++i) {
                        cJSON* entry = cJSON_GetArrayItem(web, i);
                        if (!cJSON_IsObject(entry))
                            continue;
                        cJSON* title = cJSON_GetObjectItem(entry, "title");
                        cJSON* description = cJSON_GetObjectItem(entry, "description");
                        cJSON* item = cJSON_CreateObject();
                        if (cJSON_IsString(title))
                            cJSON_AddStringToObject(item, "title", title->valuestring);
                        else
                            cJSON_AddStringToObject(item, "title", "");
                        if (cJSON_IsString(description))
                            cJSON_AddStringToObject(item, "description", description->valuestring);
                        else
                            cJSON_AddStringToObject(item, "description", "");
                        cJSON_AddItemToArray(items, item);
                        // Append a concise, newline-stripped snippet to summary_text
                        const char* t = cJSON_IsString(title) ? title->valuestring : "";
                        const char* d = cJSON_IsString(description) ? description->valuestring : "";
                        std::string ds(d);
                        for (auto& ch : ds)
                            if (ch == '\n' || ch == '\r')
                                ch = ' ';
                        if (ds.size() > 300)
                            ds = ds.substr(0, 300) + "...";
                        if (t && t[0] != '\0') {
                            summary_text += std::string(t) + ": " + ds;
                        } else {
                            summary_text += ds;
                        }
                        if (i < to_take - 1)
                            summary_text += "  ";
                    }
                }
            }

            cJSON_AddItemToObject(summary, "items", items);
            // add the plain-text summary for quick TTS
            cJSON_AddStringToObject(summary, "summary_text", summary_text.c_str());
            cJSON_Delete(parsed);
            return summary;
        });
}
