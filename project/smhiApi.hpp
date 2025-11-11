#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>

// Reuse StationInfo from your generated stations.hpp to avoid redefinition
#include "stations.hpp"

// Provided by project.ino
extern std::vector<StationInfo> gStations;

// Weather datapoints for chart
struct DataPoint {
  String date;  // "YYYY-MM-DD"
  String time;  // "HH:MM"
  float temp;
};

// Provided by project.ino
extern std::vector<DataPoint> weatherData;

// ------------------------------------------------------------------
// Epoch ms -> date/time strings (UTC). Add TZ offset if needed.
// ------------------------------------------------------------------
static inline void epoch_ms_to_date_time(uint64_t ms,
                                         String& outDate,
                                         String& outTime,
                                         int tz_offset_minutes = 0) {
  int64_t sec = (int64_t)(ms / 1000) + (int64_t)tz_offset_minutes * 60;
  time_t t = (time_t)sec;
  struct tm tmres;
#if defined(ESP32)
  gmtime_r(&t, &tmres);
#else
  tmres = *gmtime(&t);
#endif
  char dBuf[11];
  char tBuf[6];
  snprintf(dBuf, sizeof(dBuf), "%04d-%02d-%02d", tmres.tm_year + 1900,
           tmres.tm_mon + 1, tmres.tm_mday);
  snprintf(tBuf, sizeof(tBuf), "%02d:%02d", tmres.tm_hour, tmres.tm_min);
  outDate = dBuf;
  outTime = tBuf;
}

// ------------------------------------------------------------------
// SMHI API single-header implementation
// ------------------------------------------------------------------
class SMHI_API {
 public:
  explicit SMHI_API(const char* apiRoot) : apiUrl(apiRoot) {}

  // Returns true if data fetched and parsed into weatherData
  bool update_weather_data(int station_idx, int param_idx, String period) {
    weatherData.clear();

    if (station_idx < 0 || station_idx >= (int)gStations.size()) return false;

    const char* param_codes[] = {"1", "2", "3"};  // temp, precip, wind

    String url = apiUrl;
    url += param_codes[param_idx];
    url += "/station/";
    url += gStations[station_idx].id;
    url += "/period/" + period + "/data.json";

    Serial.printf("Fetching data: %s\n", url.c_str());

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;

    if (!https.begin(client, url)) return false;

    int code = https.GET();
    if (code != 200) {
      Serial.printf("SMHI: HTTP error %d\n", code);
      https.end();
      return false;
    }

    String payload = https.getString();
    https.end();

    parseWeatherData(payload);
    bool ok = !weatherData.empty();
    Serial.printf("SMHI: %s (%d points)\n", ok ? "Data OK" : "No data parsed",
                  (int)weatherData.size());
    return ok;
  }

  void parseWeatherData(const String& jsonData) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonData);
    if (err) {
      Serial.print("SMHI: JSON parse error: ");
      Serial.println(err.c_str());
      return;
    }

    weatherData.clear();

    if (doc["value"].is<JsonArray>()) {
      for (JsonObject v : doc["value"].as<JsonArray>()) {
        DataPoint dp;
        uint64_t ms = v["date"].as<uint64_t>();
        epoch_ms_to_date_time(ms, dp.date, dp.time);
        dp.temp = v["value"].as<float>();
        weatherData.push_back(dp);
      }
    } else if (doc["timeSeries"].is<JsonArray>()) {
      for (JsonObject rec : doc["timeSeries"].as<JsonArray>()) {
        String t = rec["validTime"].as<String>();  // "YYYY-MM-DDTHH:MM:SSZ"
        for (JsonObject p : rec["parameters"].as<JsonArray>()) {
          if (String(p["name"].as<const char*>()) == "t") {
            DataPoint dp;
            dp.date = t.substring(0, 10);
            dp.time = t.substring(11, 16);
            dp.temp = p["values"][0].as<float>();
            weatherData.push_back(dp);
            break;
          }
        }
      }
    }
  }

 private:
  const char* apiUrl;
};