#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>

// --------------------------------------------------------------------
// --- Data Definitions ---
// --------------------------------------------------------------------
const char* param_codes[] = {"1", "2", "3"};  // 1=Temp, 2=Precipitation, 3=Wind

struct DataPoint {
  String date;
  String time;
  float temp;
};

struct Station {
  const char* name;
  const char* id;
};

Station stations[] = {
    {"Karlskrona", "65090"},
    {"Stockholm", "98230"},
    {"Göteborg", "71420"},
};

std::vector<DataPoint> weatherData;

// --------------------------------------------------------------------
// --- Helper: epoch → date/time ---
// --------------------------------------------------------------------
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

// --------------------------------------------------------------------
// --- SMHI API class ---
// --------------------------------------------------------------------
class SMHI_API {
 public:
  explicit SMHI_API(const char* apiUrl) : apiUrl(apiUrl) {}

  void update_weather_data(int city_idx, int param_idx, String period) {
    String url = apiUrl;
    url += param_codes[param_idx];
    url += "/station/";
    url += stations[city_idx].id;
    url += "/period/" + period + "/data.json";

    Serial.printf("Fetching data: %s\n", url.c_str());

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;

    if (https.begin(client, url)) {
      int code = https.GET();
      if (code == 200) {
        String payload = https.getString();
        parseWeatherData(payload);
        Serial.println("Weather data fetched OK.");
      } else {
        Serial.printf("HTTP error: %d\n", code);
      }
      https.end();
    } else {
      Serial.println("Failed to start HTTPS");
    }
  }

  void parseWeatherData(const String& jsonData) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonData);
    if (err) {
      Serial.print("JSON parse error: ");
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
        String ts = rec["validTime"].as<String>();
        for (JsonObject p : rec["parameters"].as<JsonArray>()) {
          if (String(p["name"].as<const char*>()) == "t") {
            DataPoint dp;
            dp.date = ts.substring(0, 10);
            dp.time = ts.substring(11, 16);
            dp.temp = p["values"][0].as<float>();
            weatherData.push_back(dp);
            break;
          }
        }
      }
    } else {
      Serial.println("SMHI: Unknown JSON layout!");
    }

    Serial.printf("Parsed %d data points.\n", weatherData.size());
  }

 private:
  const char* apiUrl;
};