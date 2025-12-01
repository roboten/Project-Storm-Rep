#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <vector>

#include "stations.hpp"

extern std::vector<StationInfo> gStations;

struct DataPoint {
  String date;
  String time;
  float temp;
};

extern std::vector<DataPoint> weatherData;

static inline void epoch_ms_to_date_time(uint64_t ms, String &outDate,
                                         String &outTime,
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

// Helper to safely parse a value that could be string, int, or float
static float parseValueToFloat(JsonVariant val) {
  if (val.isNull()) {
    return 0.0f;
  }
  
  if (val.is<const char*>()) {
    return atof(val.as<const char*>());
  }
  
  if (val.is<float>()) {
    return val.as<float>();
  }
  
  if (val.is<int>()) {
    return (float)val.as<int>();
  }
  
  // Fallback
  return val.as<String>().toFloat();
}

class SMHI_API {
public:
  explicit SMHI_API(const char *apiRoot) : apiUrl(apiRoot) {}

  bool update_weather_data(int station_idx, int param_code, String period) {
    weatherData.clear();

    if (station_idx < 0 || station_idx >= (int)gStations.size())
      return false;

    String url = apiUrl;
    url += String(param_code);
    url += "/station/";
    url += gStations[station_idx].id;
    url += "/period/" + period + "/data.json";

    Serial.printf("Fetching data: %s\n", url.c_str());

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);
    
    HTTPClient https;
    https.setTimeout(15000);

    if (!https.begin(client, url))
      return false;

    int code = https.GET();
    if (code != 200) {
      Serial.printf("SMHI: HTTP error %d\n", code);
      https.end();
      return false;
    }

    // Get stream instead of string to save memory
    Stream& stream = https.getStream();
    
    Serial.printf("SMHI: Response size: %d bytes\n", https.getSize());

    // Parse using streaming approach
    bool success = parseWeatherDataStream(stream);
    
    https.end();

    Serial.printf("SMHI: %s (%d points)\n", 
                  success ? "Data OK" : "No data parsed",
                  (int)weatherData.size());
    
    return success;
  }

  bool parseWeatherDataStream(Stream& stream) {
    weatherData.clear();
    
    // First, find the "value" array in the stream
    if (!stream.find("\"value\"")) {
      Serial.println("SMHI: 'value' key not found, trying 'timeSeries'");
      
      // Try timeSeries format (forecast API)
      if (!stream.find("\"timeSeries\"")) {
        Serial.println("SMHI: No recognized data array found");
        return false;
      }
      return parseTimeSeriesStream(stream);
    }
    
    // Find the start of the array
    if (!stream.find("[")) {
      Serial.println("SMHI: Array start not found");
      return false;
    }
    
    Serial.println("SMHI: Parsing value array...");
    
    // Create a filter to only parse what we need
    StaticJsonDocument<128> filter;
    filter["date"] = true;
    filter["value"] = true;
    filter["ref"] = true;
    filter["from"] = true;
    
    // Buffer for reading individual JSON objects
    char objBuffer[256];
    int parseCount = 0;
    int errorCount = 0;
    
    while (true) {
      // Read one JSON object from the array
      if (!readNextJsonObject(stream, objBuffer, sizeof(objBuffer))) {
        break; // End of array or error
      }
      
      // Parse the small object
      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(doc, objBuffer, 
                                                  DeserializationOption::Filter(filter));
      
      if (err) {
        errorCount++;
        if (errorCount <= 3) {
          Serial.printf("SMHI: Parse error: %s\n", err.c_str());
        }
        continue;
      }
      
      DataPoint dp;
      bool parsed = false;
      
      // Hourly format: { "date": 1753318800000, "value": "18.7" }
      if (doc.containsKey("date") && doc.containsKey("value")) {
        uint64_t ms = doc["date"].as<uint64_t>();
        epoch_ms_to_date_time(ms, dp.date, dp.time);
        dp.temp = parseValueToFloat(doc["value"]);
        parsed = true;
      }
      // Daily format: { "ref": "2025-07-24", "value": "18.3" }
      else if (doc.containsKey("ref") && doc.containsKey("value")) {
        dp.date = doc["ref"].as<String>();
        dp.time = "12:00";
        dp.temp = parseValueToFloat(doc["value"]);
        parsed = true;
      }
      // Fallback: use "from" timestamp
      else if (doc.containsKey("from") && doc.containsKey("value")) {
        uint64_t ms = doc["from"].as<uint64_t>();
        epoch_ms_to_date_time(ms, dp.date, dp.time);
        dp.temp = parseValueToFloat(doc["value"]);
        parsed = true;
      }
      
      if (parsed) {
        weatherData.push_back(dp);
        parseCount++;
        
        // Print progress every 500 entries
        if (parseCount % 500 == 0) {
          Serial.printf("SMHI: Parsed %d entries...\n", parseCount);
        }
      }
    }
    
    Serial.printf("SMHI: Finished parsing - %d entries, %d errors\n", 
                  parseCount, errorCount);
    
    return parseCount > 0;
  }
  
  bool parseTimeSeriesStream(Stream& stream) {
    // Find array start
    if (!stream.find("[")) {
      return false;
    }
    
    char objBuffer[2048]; // TimeSeries objects are larger
    int parseCount = 0;
    
    StaticJsonDocument<256> filter;
    filter["validTime"] = true;
    filter["parameters"][0]["name"] = true;
    filter["parameters"][0]["values"][0] = true;
    
    while (readNextJsonObject(stream, objBuffer, sizeof(objBuffer))) {
      DynamicJsonDocument doc(2048);
      DeserializationError err = deserializeJson(doc, objBuffer,
                                                  DeserializationOption::Filter(filter));
      if (err) continue;
      
      String t = doc["validTime"].as<String>();
      if (t.length() < 16) continue;
      
      for (JsonObject p : doc["parameters"].as<JsonArray>()) {
        const char* name = p["name"] | "";
        if (strcmp(name, "t") == 0) {
          DataPoint dp;
          dp.date = t.substring(0, 10);
          dp.time = t.substring(11, 16);
          dp.temp = p["values"][0].as<float>();
          weatherData.push_back(dp);
          parseCount++;
          break;
        }
      }
    }
    
    return parseCount > 0;
  }

private:
  const char *apiUrl;
  
  // Read a single JSON object from stream (handles nested braces)
  bool readNextJsonObject(Stream& stream, char* buffer, size_t bufSize) {
    size_t pos = 0;
    int braceCount = 0;
    bool started = false;
    bool inString = false;
    bool escaped = false;
    unsigned long timeout = millis() + 10000;
    
    while (millis() < timeout) {
      if (!stream.available()) {
        delay(1);
        continue;
      }
      
      char c = stream.read();
      
      // Handle string state (to ignore braces inside strings)
      if (!escaped && c == '"') {
        inString = !inString;
      }
      escaped = (!escaped && c == '\\');
      
      if (!started) {
        if (c == '{') {
          started = true;
          braceCount = 1;
          if (pos < bufSize - 1) buffer[pos++] = c;
        } else if (c == ']') {
          // End of array
          return false;
        }
        // Skip whitespace and commas before object
      } else {
        if (pos < bufSize - 1) {
          buffer[pos++] = c;
        } else {
          // Buffer overflow - skip this object
          while (braceCount > 0 && millis() < timeout) {
            if (stream.available()) {
              char skip = stream.read();
              if (!inString) {
                if (skip == '{') braceCount++;
                else if (skip == '}') braceCount--;
              }
              if (!escaped && skip == '"') inString = !inString;
              escaped = (!escaped && skip == '\\');
            }
          }
          return false;
        }
        
        if (!inString) {
          if (c == '{') {
            braceCount++;
          } else if (c == '}') {
            braceCount--;
            if (braceCount == 0) {
              buffer[pos] = '\0';
              return true;
            }
          }
        }
      }
    }
    
    Serial.println("SMHI: Timeout reading JSON object");
    return false;
  }
};