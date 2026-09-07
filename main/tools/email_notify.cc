#include "email_notify.h"
#include <esp_log.h>
#include <cJSON.h>
#include <cctype>
#include <string>
#include "board.h"
#include "secrets.h"
// In your tool callback:
std::string api_key = EMAIL_SERVICE_API_KEY;

static const char* TAG = "EmailNotify";

void RegisterEmailNotifyTool(McpServer& server) {
    ESP_LOGI(TAG, "Registering MCP tool: email.send");
    server.AddTool(
        "email.send",
        "Send an email to Bruno or Tai. Args: recipient (string: bruno|tai), var1 (string, "
        "optional), var2 (string, message), host "
        "(optional), api_key (required)",
        PropertyList({
            Property("recipient", kPropertyTypeString),
            Property("var1", kPropertyTypeString, std::string("Wall-E")),
            Property("var2", kPropertyTypeString),
            Property("host", kPropertyTypeString, std::string("www.circuitdigest.cloud")),
            Property("api_key", kPropertyTypeString, std::string(EMAIL_SERVICE_API_KEY)),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto recipient = properties["recipient"].value<std::string>();
            auto var1 = properties["var1"].value<std::string>();
            auto var2 = properties["var2"].value<std::string>();
            std::string host = properties["host"].value<std::string>();
            std::string api_key = properties["api_key"].value<std::string>();

            if (api_key.empty()) {
                throw std::runtime_error("api_key is required");
            }
            if (recipient.empty()) {
                throw std::runtime_error("recipient is required (bruno|tai)");
            }

            // Map recipient to email address
            std::string to;
            std::string r = recipient;
            for (auto& c : r)
                c = tolower(c);
            if (r == "bruno" || r == "brunoguratti") {
                to = "brunoguratti@gmail.com";
            } else if (r == "tai" || r == "taicobalchini") {
                to = "taicobalchini0@gmail.com";
            } else {
                throw std::runtime_error("recipient must be 'bruno' or 'tai'");
            }

            const std::string subject = std::string("Notes from AI - ") + var1;

            ESP_LOGI(TAG, "email.send invoked - recipient=%s to=%s host=%s", recipient.c_str(),
                     to.c_str(), host.c_str());

            auto& board = Board::GetInstance();
            auto http = board.GetNetwork()->CreateHttp(5);

            std::string url = std::string("https://") + host + "/api/v1/email/send";

            // Build JSON payload
            cJSON* payload = cJSON_CreateObject();
            cJSON_AddStringToObject(payload, "to_email", to.c_str());
            cJSON_AddStringToObject(payload, "template_id", "Device Notification");
            cJSON* vars = cJSON_CreateObject();
            cJSON_AddStringToObject(vars, "title", subject.c_str());
            cJSON_AddStringToObject(vars, "description", var2.c_str());
            // Include device/message placeholders for clarity
            cJSON_AddStringToObject(vars, "device_name", var1.c_str());
            cJSON_AddStringToObject(vars, "message", var2.c_str());
            cJSON_AddItemToObject(payload, "variables", vars);

            char* payload_str = cJSON_PrintUnformatted(payload);
            std::string content(payload_str);
            cJSON_free(payload_str);
            cJSON_Delete(payload);

            http->SetHeader("User-Agent", "XiaoZhi/1.0");
            http->SetHeader("Authorization", api_key);
            http->SetHeader("Content-Type", "application/json");
            http->SetContent(std::move(content));

            if (!http->Open("POST", url)) {
                throw std::runtime_error("Failed to open email API URL: " + url);
            }

            // Write content if the implementation requires manual write (some impls expect
            // SetContent) Ensure request is flushed
            http->Write("", 0);

            int status = http->GetStatusCode();
            std::string resp = http->ReadAll();
            http->Close();

            cJSON* result = cJSON_CreateObject();
            cJSON_AddNumberToObject(result, "status", status);
            cJSON_AddStringToObject(result, "response", resp.c_str());

            if (status < 200 || status >= 300) {
                ESP_LOGE(TAG, "Email API returned status=%d body=%s", status, resp.c_str());
            } else {
                ESP_LOGI(TAG, "Email sent successfully: %s", resp.c_str());
            }

            return result;
        });
}
