#include "weather_client.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#include <esp_log.h>
#include <cJSON.h>

#include "board.h"

static const char* TAG = "WeatherClient";

// ============================================================
// URL encoding
// ============================================================

static std::string UrlEncode(const std::string& value) {
    std::ostringstream encoded;

    encoded << std::hex << std::uppercase;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }

    return encoded.str();
}

// ============================================================
// Convert string to lowercase
// ============================================================

static std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return value;
}

// ============================================================
// Round temperature to nearest whole degree
// ============================================================

static int RoundTemperature(double temperature) {
    return static_cast<int>(std::round(temperature));
}

// ============================================================
// WMO weather code → human-readable description
// ============================================================

static const char* WeatherCodeToText(int code) {
    switch (code) {
        case 0:
            return "clear sky";

        case 1:
            return "mainly clear";

        case 2:
            return "partly cloudy";

        case 3:
            return "overcast";

        case 45:
        case 48:
            return "foggy";

        case 51:
        case 53:
        case 55:
            return "drizzle";

        case 56:
        case 57:
            return "freezing drizzle";

        case 61:
        case 63:
        case 65:
            return "rain";

        case 66:
        case 67:
            return "freezing rain";

        case 71:
        case 73:
        case 75:
            return "snow";

        case 77:
            return "snow grains";

        case 80:
        case 81:
        case 82:
            return "rain showers";

        case 85:
        case 86:
            return "snow showers";

        case 95:
            return "thunderstorm";

        case 96:
        case 99:
            return "thunderstorm with hail";

        default:
            return "unknown conditions";
    }
}

// ============================================================
// Find requested day
//
// today       → index 0
// tomorrow    → index 1
// weekday     → matching weekday in returned forecast
// ============================================================

static int FindRequestedDay(const std::string& when, cJSON* dates) {
    if (!cJSON_IsArray(dates)) {
        return -1;
    }

    const int count = cJSON_GetArraySize(dates);

    if (when == "today") {
        return count > 0 ? 0 : -1;
    }

    if (when == "tomorrow") {
        return count > 1 ? 1 : -1;
    }

    if (when == "monday" || when == "tuesday" || when == "wednesday" || when == "thursday" ||
        when == "friday" || when == "saturday" || when == "sunday") {
        for (int i = 0; i < count; ++i) {
            cJSON* dateItem = cJSON_GetArrayItem(dates, i);

            if (!cJSON_IsString(dateItem) || dateItem->valuestring == nullptr) {
                continue;
            }

            const char* date = dateItem->valuestring;

            // Expected format: YYYY-MM-DD
            if (std::strlen(date) < 10) {
                continue;
            }

            int year = 0;
            int month = 0;
            int day = 0;

            if (sscanf(date, "%d-%d-%d", &year, &month, &day) != 3) {
                continue;
            }

            // Gregorian weekday calculation.
            //
            // Result:
            // 0 = Sunday
            // 1 = Monday
            // 2 = Tuesday
            // ...
            // 6 = Saturday

            int y = year;
            int m = month;

            if (m < 3) {
                m += 12;
                --y;
            }

            int zeller = (day + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400) % 7;

            int sundayBasedWeekday = (zeller + 6) % 7;

            const char* weekdayName = nullptr;

            switch (sundayBasedWeekday) {
                case 0:
                    weekdayName = "sunday";
                    break;

                case 1:
                    weekdayName = "monday";
                    break;

                case 2:
                    weekdayName = "tuesday";
                    break;

                case 3:
                    weekdayName = "wednesday";
                    break;

                case 4:
                    weekdayName = "thursday";
                    break;

                case 5:
                    weekdayName = "friday";
                    break;

                case 6:
                    weekdayName = "saturday";
                    break;
            }

            if (weekdayName != nullptr && when == weekdayName) {
                return i;
            }
        }
    }

    return -1;
}

// ============================================================
// Register weather MCP tool
// ============================================================

void RegisterWeatherTool(McpServer& server) {
    esp_log_level_set(TAG, ESP_LOG_INFO);

    ESP_LOGI(TAG, "Registering MCP tool: weather.get");

    server.AddTool(
        "weather.get",

        "Get weather information for a location. "
        "Args: location (string), when (string). "
        "when can be: now, today, tomorrow, monday, tuesday, "
        "wednesday, thursday, friday, saturday, sunday, or week. "
        "For week, return the daily forecast for each day.",

        PropertyList({Property("location", kPropertyTypeString),

                      Property("when", kPropertyTypeString)}),

        [](const PropertyList& properties) -> ReturnValue {
            // ====================================================
            // Read parameters
            // ====================================================

            std::string location = properties["location"].value<std::string>();

            std::string when = properties["when"].value<std::string>();

            location = ToLower(location);
            when = ToLower(when);

            if (location.empty()) {
                throw std::runtime_error("Weather location is required");
            }

            if (when.empty()) {
                when = "today";
            }

            ESP_LOGI(TAG, "weather.get invoked - location='%s' when='%s'", location.c_str(),
                     when.c_str());

            // ====================================================
            // Validate requested period
            // ====================================================

            const bool isNow = when == "now";

            const bool isWeek = when == "week";

            const bool isSingleDay = when == "today" || when == "tomorrow" || when == "monday" ||
                                     when == "tuesday" || when == "wednesday" ||
                                     when == "thursday" || when == "friday" || when == "saturday" ||
                                     when == "sunday";

            if (!isNow && !isWeek && !isSingleDay) {
                throw std::runtime_error(
                    "Invalid weather period. "
                    "Use now, today, tomorrow, "
                    "a weekday, or week.");
            }

            // ====================================================
            // Network
            // ====================================================

            auto& board = Board::GetInstance();

            // ====================================================
            // STEP 1: Geocoding
            // ====================================================

            std::string geocodeUrl =
                "https://geocoding-api.open-meteo.com/v1/search"
                "?name=" +
                UrlEncode(location) +
                "&count=1"
                "&language=en"
                "&format=json";

            ESP_LOGI(TAG, "Geocoding location: %s", geocodeUrl.c_str());

            auto geoHttp = board.GetNetwork()->CreateHttp(5);

            geoHttp->SetHeader("User-Agent", "XiaoZhi/1.0");

            if (!geoHttp->Open("GET", geocodeUrl)) {
                ESP_LOGE(TAG, "Failed to open geocoding connection");

                throw std::runtime_error("Unable to connect to weather location service");
            }

            int geoStatus = geoHttp->GetStatusCode();

            if (geoStatus != 200) {
                std::string body = geoHttp->ReadAll();

                geoHttp->Close();

                ESP_LOGE(TAG, "Geocoding HTTP error: %d", geoStatus);

                throw std::runtime_error("Weather location lookup failed");
            }

            std::string geoResponse = geoHttp->ReadAll();

            geoHttp->Close();

            // ====================================================
            // Parse geocoding response
            // ====================================================

            cJSON* geoJson = cJSON_Parse(geoResponse.c_str());

            if (!geoJson) {
                throw std::runtime_error("Invalid weather location response");
            }

            cJSON* results = cJSON_GetObjectItem(geoJson, "results");

            if (!cJSON_IsArray(results) || cJSON_GetArraySize(results) == 0) {
                cJSON_Delete(geoJson);

                throw std::runtime_error("Location not found");
            }

            cJSON* firstResult = cJSON_GetArrayItem(results, 0);

            cJSON* latitude = cJSON_GetObjectItem(firstResult, "latitude");

            cJSON* longitude = cJSON_GetObjectItem(firstResult, "longitude");

            if (!cJSON_IsNumber(latitude) || !cJSON_IsNumber(longitude)) {
                cJSON_Delete(geoJson);

                throw std::runtime_error("Invalid coordinates returned for location");
            }

            double lat = latitude->valuedouble;

            double lon = longitude->valuedouble;

            std::string resolvedLocation = location;

            cJSON* name = cJSON_GetObjectItem(firstResult, "name");

            if (cJSON_IsString(name) && name->valuestring != nullptr) {
                resolvedLocation = name->valuestring;
            }

            ESP_LOGI(TAG, "Resolved location: %s (%.6f, %.6f)", resolvedLocation.c_str(), lat, lon);

            cJSON_Delete(geoJson);

            // ====================================================
            // STEP 2: Build Open-Meteo request
            //
            // now:
            //     current weather only
            //
            // everything else:
            //     daily forecast only
            //
            // This keeps the response small for ESP32-S3.
            // ====================================================

            char urlBuffer[1024];

            if (isNow) {
                snprintf(urlBuffer, sizeof(urlBuffer),

                         "https://api.open-meteo.com/v1/forecast"
                         "?latitude=%.6f"
                         "&longitude=%.6f"
                         "&current="
                         "temperature_2m,"
                         "relative_humidity_2m,"
                         "weather_code,"
                         "wind_speed_10m,"
                         "is_day"
                         "&timezone=auto",

                         lat, lon);

            } else {
                snprintf(urlBuffer, sizeof(urlBuffer),

                         "https://api.open-meteo.com/v1/forecast"
                         "?latitude=%.6f"
                         "&longitude=%.6f"
                         "&daily="
                         "temperature_2m_max,"
                         "temperature_2m_min,"
                         "weather_code,"
                         "precipitation_probability_max,"
                         "wind_speed_10m_max"
                         "&forecast_days=7"
                         "&timezone=auto",

                         lat, lon);
            }

            std::string forecastUrl = urlBuffer;

            ESP_LOGI(TAG, "Fetching forecast: %s", forecastUrl.c_str());

            // ====================================================
            // STEP 3: Open forecast connection
            // ====================================================

            auto forecastHttp = board.GetNetwork()->CreateHttp(6);

            forecastHttp->SetHeader("User-Agent", "XiaoZhi/1.0");

            if (!forecastHttp->Open("GET", forecastUrl)) {
                ESP_LOGE(TAG, "Failed to open forecast connection");

                throw std::runtime_error("Unable to connect to weather service");
            }

            int forecastStatus = forecastHttp->GetStatusCode();

            ESP_LOGI(TAG, "Forecast HTTP status: %d", forecastStatus);

            if (forecastStatus != 200) {
                std::string body = forecastHttp->ReadAll();

                forecastHttp->Close();

                ESP_LOGE(TAG, "Forecast HTTP error: %d", forecastStatus);

                throw std::runtime_error("Weather service returned HTTP error");
            }

            // ====================================================
            // Read forecast response
            // ====================================================

            std::string forecastResponse = forecastHttp->ReadAll();

            forecastHttp->Close();

            ESP_LOGI(TAG, "Forecast response received: %u bytes",
                     static_cast<unsigned>(forecastResponse.size()));

            if (forecastResponse.empty()) {
                throw std::runtime_error("Weather service returned an empty response");
            }

            // ====================================================
            // Parse forecast JSON
            // ====================================================

            cJSON* forecastJson = cJSON_Parse(forecastResponse.c_str());

            if (!forecastJson) {
                ESP_LOGE(TAG, "Failed to parse forecast JSON");

                throw std::runtime_error("Invalid weather forecast response");
            }

            // ====================================================
            // CURRENT WEATHER
            // ====================================================

            if (isNow) {
                cJSON* current = cJSON_GetObjectItem(forecastJson, "current");

                if (!cJSON_IsObject(current)) {
                    cJSON_Delete(forecastJson);

                    throw std::runtime_error("Current weather data unavailable");
                }

                cJSON* temperature = cJSON_GetObjectItem(current, "temperature_2m");

                cJSON* humidity = cJSON_GetObjectItem(current, "relative_humidity_2m");

                cJSON* wind = cJSON_GetObjectItem(current, "wind_speed_10m");

                cJSON* code = cJSON_GetObjectItem(current, "weather_code");

                cJSON* isDay = cJSON_GetObjectItem(current, "is_day");

                cJSON* result = cJSON_CreateObject();

                if (!result) {
                    cJSON_Delete(forecastJson);

                    throw std::runtime_error("Unable to create weather result");
                }

                cJSON_AddStringToObject(result, "location", resolvedLocation.c_str());

                cJSON_AddStringToObject(result, "when", "now");

                // ROUND TEMPERATURE
                if (cJSON_IsNumber(temperature)) {
                    cJSON_AddNumberToObject(result, "temperature_c",
                                            RoundTemperature(temperature->valuedouble));
                }

                if (cJSON_IsNumber(humidity)) {
                    cJSON_AddNumberToObject(result, "humidity_percent", humidity->valuedouble);
                }

                if (cJSON_IsNumber(wind)) {
                    cJSON_AddNumberToObject(result, "wind_speed_kmh", wind->valuedouble);
                }

                if (cJSON_IsNumber(code)) {
                    cJSON_AddNumberToObject(result, "weather_code", code->valueint);

                    cJSON_AddStringToObject(result, "condition", WeatherCodeToText(code->valueint));
                }

                if (cJSON_IsNumber(isDay)) {
                    cJSON_AddStringToObject(result, "day_or_night",
                                            isDay->valueint ? "day" : "night");
                }

                cJSON_Delete(forecastJson);

                ESP_LOGI(TAG, "Current weather retrieved successfully");

                return result;
            }

            // ====================================================
            // DAILY FORECAST
            // ====================================================

            cJSON* daily = cJSON_GetObjectItem(forecastJson, "daily");

            if (!cJSON_IsObject(daily)) {
                cJSON_Delete(forecastJson);

                throw std::runtime_error("Daily forecast data unavailable");
            }

            cJSON* dates = cJSON_GetObjectItem(daily, "time");

            cJSON* minTemps = cJSON_GetObjectItem(daily, "temperature_2m_min");

            cJSON* maxTemps = cJSON_GetObjectItem(daily, "temperature_2m_max");

            cJSON* weatherCodes = cJSON_GetObjectItem(daily, "weather_code");

            cJSON* precipitation = cJSON_GetObjectItem(daily, "precipitation_probability_max");

            cJSON* winds = cJSON_GetObjectItem(daily, "wind_speed_10m_max");

            if (!cJSON_IsArray(dates) || !cJSON_IsArray(minTemps) || !cJSON_IsArray(maxTemps) ||
                !cJSON_IsArray(weatherCodes)) {
                cJSON_Delete(forecastJson);

                throw std::runtime_error("Invalid daily forecast data");
            }

            int dateCount = cJSON_GetArraySize(dates);

            if (dateCount <= 0) {
                cJSON_Delete(forecastJson);

                throw std::runtime_error("No forecast dates returned");
            }

            // ====================================================
            // WEEKLY FORECAST
            // ====================================================

            if (isWeek) {
                cJSON* result = cJSON_CreateObject();

                if (!result) {
                    cJSON_Delete(forecastJson);

                    throw std::runtime_error("Unable to create weather result");
                }

                cJSON_AddStringToObject(result, "location", resolvedLocation.c_str());

                cJSON_AddStringToObject(result, "when", "week");

                cJSON* forecastArray = cJSON_CreateArray();

                if (!forecastArray) {
                    cJSON_Delete(result);
                    cJSON_Delete(forecastJson);

                    throw std::runtime_error("Unable to create weekly forecast");
                }

                int count = std::min(dateCount, 7);

                for (int i = 0; i < count; ++i) {
                    cJSON* day = cJSON_CreateObject();

                    if (!day) {
                        continue;
                    }

                    cJSON* date = cJSON_GetArrayItem(dates, i);

                    cJSON* minTemp = cJSON_GetArrayItem(minTemps, i);

                    cJSON* maxTemp = cJSON_GetArrayItem(maxTemps, i);

                    cJSON* code = cJSON_GetArrayItem(weatherCodes, i);

                    if (cJSON_IsString(date)) {
                        cJSON_AddStringToObject(day, "date", date->valuestring);
                    }

                    // ROUND MINIMUM TEMPERATURE
                    if (cJSON_IsNumber(minTemp)) {
                        cJSON_AddNumberToObject(day, "temperature_min_c",
                                                RoundTemperature(minTemp->valuedouble));
                    }

                    // ROUND MAXIMUM TEMPERATURE
                    if (cJSON_IsNumber(maxTemp)) {
                        cJSON_AddNumberToObject(day, "temperature_max_c",
                                                RoundTemperature(maxTemp->valuedouble));
                    }

                    if (cJSON_IsNumber(code)) {
                        cJSON_AddNumberToObject(day, "weather_code", code->valueint);

                        cJSON_AddStringToObject(day, "condition",
                                                WeatherCodeToText(code->valueint));
                    }

                    if (cJSON_IsArray(precipitation)) {
                        cJSON* precip = cJSON_GetArrayItem(precipitation, i);

                        if (cJSON_IsNumber(precip)) {
                            cJSON_AddNumberToObject(day, "precipitation_probability_percent",
                                                    precip->valuedouble);
                        }
                    }

                    if (cJSON_IsArray(winds)) {
                        cJSON* wind = cJSON_GetArrayItem(winds, i);

                        if (cJSON_IsNumber(wind)) {
                            cJSON_AddNumberToObject(day, "wind_speed_max_kmh", wind->valuedouble);
                        }
                    }

                    cJSON_AddItemToArray(forecastArray, day);
                }

                cJSON_AddItemToObject(result, "forecast", forecastArray);

                cJSON_Delete(forecastJson);

                ESP_LOGI(TAG, "Weekly forecast retrieved successfully");

                return result;
            }

            // ====================================================
            // SINGLE DAY FORECAST
            // ====================================================

            int dayIndex = FindRequestedDay(when, dates);

            if (dayIndex < 0 || dayIndex >= dateCount) {
                cJSON_Delete(forecastJson);

                throw std::runtime_error(
                    "Requested day is not available "
                    "in the forecast");
            }

            cJSON* date = cJSON_GetArrayItem(dates, dayIndex);

            cJSON* minTemp = cJSON_GetArrayItem(minTemps, dayIndex);

            cJSON* maxTemp = cJSON_GetArrayItem(maxTemps, dayIndex);

            cJSON* code = cJSON_GetArrayItem(weatherCodes, dayIndex);

            cJSON* precip = nullptr;

            if (cJSON_IsArray(precipitation)) {
                precip = cJSON_GetArrayItem(precipitation, dayIndex);
            }

            cJSON* wind = nullptr;

            if (cJSON_IsArray(winds)) {
                wind = cJSON_GetArrayItem(winds, dayIndex);
            }

            cJSON* result = cJSON_CreateObject();

            if (!result) {
                cJSON_Delete(forecastJson);

                throw std::runtime_error("Unable to create weather result");
            }

            cJSON_AddStringToObject(result, "location", resolvedLocation.c_str());

            cJSON_AddStringToObject(result, "when", when.c_str());

            if (cJSON_IsString(date)) {
                cJSON_AddStringToObject(result, "date", date->valuestring);
            }

            // ROUND MINIMUM TEMPERATURE
            if (cJSON_IsNumber(minTemp)) {
                cJSON_AddNumberToObject(result, "temperature_min_c",
                                        RoundTemperature(minTemp->valuedouble));
            }

            // ROUND MAXIMUM TEMPERATURE
            if (cJSON_IsNumber(maxTemp)) {
                cJSON_AddNumberToObject(result, "temperature_max_c",
                                        RoundTemperature(maxTemp->valuedouble));
            }

            if (cJSON_IsNumber(code)) {
                cJSON_AddNumberToObject(result, "weather_code", code->valueint);

                cJSON_AddStringToObject(result, "condition", WeatherCodeToText(code->valueint));
            }

            if (cJSON_IsNumber(precip)) {
                cJSON_AddNumberToObject(result, "precipitation_probability_percent",
                                        precip->valuedouble);
            }

            if (cJSON_IsNumber(wind)) {
                cJSON_AddNumberToObject(result, "wind_speed_max_kmh", wind->valuedouble);
            }

            cJSON_Delete(forecastJson);

            ESP_LOGI(TAG,
                     "Daily forecast retrieved successfully: "
                     "when=%s index=%d",
                     when.c_str(), dayIndex);

            return result;
        });
}