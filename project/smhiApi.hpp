#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <WiFiClientSecure.h>
#include <vector>

const char* param_codes[] = {"1", "2", "3"};  // 1=Temp, 2=Nederbörd, 3=Vind

struct DataPoint {
  String date;   // "YYYY-MM-DD"
  String time;   // "HH:MM"
  float temp;
};

struct Station {
  const char* name;
  const char* id;
};

Station stations[] = {{"Karlskrona", "65090"},
                      {"Stockholm", "98230"},
                      {"Göteborg", "71420"}};

static std::vector<DataPoint> weatherData;

class SMHI_API {
 public:
  SMHI_API(const char* apiUrl);
  bool CONNECT();
  void update_weather_data(int city_idx, int param_idx, String period);
  void parseWeatherData(const String& jsonData);

 private:
  const char* apiUrl;
};

// ---- Helper: epoch(ms) -> date/time strings (UTC) ----
// If you want local time, add your timezone offset in minutes
// (e.g. CET winter = +60, CEST summer = +120)
static inline void epoch_ms_to_date_time(uint64_t ms,
                                         String& outDate,
                                         String& outTime,
                                         int tz_offset_minutes = 0) {
  int64_t sec = (int64_t)(ms / 1000) + (int64_t)tz_offset_minutes * 60;
  time_t t = (time_t)sec;
  struct tm tmres;
#if defined(ESP32)
  gmtime_r(&t, &tmres);  // UTC
#else
  tmres = *gmtime(&t);
#endif
  char dBuf[11];  // YYYY-MM-DD
  char tBuf[6];   // HH:MM
  snprintf(dBuf, sizeof(dBuf), "%04d-%02d-%02d", tmres.tm_year + 1900,
           tmres.tm_mon + 1, tmres.tm_mday);
  snprintf(tBuf, sizeof(tBuf), "%02d:%02d", tmres.tm_hour, tmres.tm_min);
  outDate = dBuf;
  outTime = tBuf;
}

SMHI_API::SMHI_API(const char* apiUrl) : apiUrl(apiUrl) {}

void SMHI_API::update_weather_data(int city_idx,
                                   int param_idx,
                                   String period) {
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
    int httpCode = https.GET();
    if (httpCode == 200) {
      String payload = https.getString();
      parseWeatherData(payload);
      Serial.println("Weather data fetched OK.");
    } else {
      Serial.printf("HTTP GET failed, code: %d\n", httpCode);
    }
    https.end();
  } else {
    Serial.println("Failed to begin HTTPS request");
  }
}

void SMHI_API::parseWeatherData(const String& jsonData) {
  DynamicJsonDocument doc(60000);
  DeserializationError err = deserializeJson(doc, jsonData);
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return;
  }

  weatherData.clear();

  // Format 1 (station/period/.../data.json): array "value" with
  // objects that contain "date" as epoch ms and "value" as float
  if (doc.containsKey("value")) {
    JsonArray arr = doc["value"];
    for (JsonObject v : arr) {
      DataPoint dp;

      // date can be number (ms) or string with digits
      uint64_t ms = 0;
      JsonVariant dv = v["date"];
      if (dv.is<uint64_t>()) {
        ms = dv.as<uint64_t>();
      } else if (dv.is<long long>()) {
        ms = (uint64_t)dv.as<long long>();
      } else {
        // fallback if number is encoded as string
        String s = dv.as<String>();
        ms = strtoull(s.c_str(), nullptr, 10);
      }

      // Convert epoch ms -> date/time (UTC). If you want Sweden local time:
      // epoch_ms_to_date_time(ms, dp.date, dp.time, 60 /*CET*/);
      epoch_ms_to_date_time(ms, dp.date, dp.time);

      dp.temp = v["value"].as<float>();
      weatherData.push_back(dp);
    }
  }
  // Format 2 (timeSeries style): "validTime" ISO string and parameters
  else if (doc.containsKey("timeSeries")) {
    JsonArray ts = doc["timeSeries"];
    for (JsonObject rec : ts) {
      String t = rec["validTime"].as<String>();  // e.g. "2024-11-05T12:00:00Z"
      JsonArray pars = rec["parameters"];
      for (JsonObject p : pars) {
        if (String(p["name"].as<const char*>()) == "t") {
          float val = p["values"][0].as<float>();
          DataPoint dp;
          dp.date = t.substring(0, 10);
          dp.time = t.substring(11, 16);
          dp.temp = val;
          weatherData.push_back(dp);
          break;
        }
      }
    }
  } else {
    Serial.println("No known JSON key for data array!");
    return;
  }

  Serial.printf("Parsed %d datapoints.\n", weatherData.size());
}