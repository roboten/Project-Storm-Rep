#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <vector>

#include "stations.hpp"

// Global station list defined in project.ino
extern std::vector<StationInfo> gStations;

/**
 * Data Point Structure
 * Represents a single weather measurement with date, time, and value
 * Used for both historical observation data and forecast data
 */
struct DataPoint {
  String date; // YYYY-MM-DD or YYYY-MM format
  String time; // HH:MM format
  float temp;  // Temperature or other parameter value
};

// Global weather data storage (accessed from project.ino for chart rendering)
extern std::vector<DataPoint> weatherData;

/**
 * Convert epoch milliseconds to date and time strings
 * Applies timezone offset and formats as YYYY-MM-DD and HH:MM
 *
 * @param ms Epoch time in milliseconds
 * @param outDate Output date string
 * @param outTime Output time string
 * @param tz_offset_minutes Timezone offset in minutes (default 0 for UTC)
 */
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

/**
 * Safely Parse JSON Value to Float
 * Handles multiple JSON value types (string, int, float)
 * SMHI API sometimes returns numbers as strings, this handles all cases
 */
static float parseValueToFloat(JsonVariant val) {
  if (val.isNull()) {
    return 0.0f;
  }

  if (val.is<const char *>()) {
    return atof(val.as<const char *>());
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

/**
 * SMHI_API Class
 * Wrapper for SMHI (Swedish Meteorological and Hydrological Institute) API
 * Fetches meteorological observation data and forecast data
 *
 * Uses streaming JSON parsing to minimize memory usage when handling
 * large datasets (e.g., "latest-months" can contain thousands of data points)
 */
class SMHI_API {
public:
  explicit SMHI_API(const char *apiRoot) : apiUrl(apiRoot) {}

  /**
   * Fetch Weather Data from SMHI API
   *
   * @param station_idx Index into gStations vector
   * @param param_code SMHI parameter code (1=temp, 7=precip, etc.)
   * @param period "latest-day", "latest-months", etc.
   * @return true if data was successfully fetched and parsed
   *
   * Clears weatherData vector and populates it with new data
   * Uses streaming parsing to handle large responses without excessive RAM
   */
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
    Stream &stream = https.getStream();

    Serial.printf("SMHI: Response size: %d bytes\n", https.getSize());

    // Parse using streaming approach
    bool success = parseWeatherDataStream(stream);

    https.end();

    Serial.printf("SMHI: %s (%d points)\n",
                  success ? "Data OK" : "No data parsed",
                  (int)weatherData.size());

    return success;
  }

  /**
   * Stream-based Weather Data Parser
   *
   * Parses SMHI observation API responses without loading entire JSON into
   * memory Handles both hourly data (with epoch timestamps) and daily data
   * (with dates)
   *
   * Data formats supported:
   * - Hourly: { "date": 1753318800000, "value": "18.7" }
   * - Daily:  { "ref": "2025-07-24", "value": "18.3" }
   * - Fallback: { "from": timestamp, "value": "..." }
   *
   * @param stream HTTP response stream
   * @return true if at least one data point was parsed
   */
  bool parseWeatherDataStream(Stream &stream) {
    weatherData.clear();

    // First, find the "value" array in the stream (observation API format)
    if (!stream.find("\"value\"")) {
      Serial.println("SMHI: 'value' key not found, trying 'timeSeries'");

      // Try timeSeries format (forecast API, though usually handled separately)
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

    // Create a JSON filter to parse only needed fields (memory optimization)
    StaticJsonDocument<128> filter;
    filter["date"] = true;  // Epoch timestamp
    filter["value"] = true; // Measurement value
    filter["ref"] = true;   // Date reference (daily data)
    filter["from"] = true;  // Alternative timestamp

    // Buffer for reading individual JSON objects
    char objBuffer[256];
    int parseCount = 0;
    int errorCount = 0;

    while (true) {
      // Read one JSON object from the array
      if (!readNextJsonObject(stream, objBuffer, sizeof(objBuffer))) {
        break; // End of array or error
      }

      // Parse the small JSON object with filtering
      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(
          doc, objBuffer, DeserializationOption::Filter(filter));

      if (err) {
        errorCount++;
        if (errorCount <= 3) { // Only log first 3 errors to avoid spam
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

  /**
   * Parse Forecast Time Series Data
   * Used for SMHI forecast API (different format than observation API)
   *
   * Format: { "validTime": "2025-01-15T12:00:00Z", "parameters": [...] }
   */
  bool parseTimeSeriesStream(Stream &stream) {
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
      DeserializationError err = deserializeJson(
          doc, objBuffer, DeserializationOption::Filter(filter));
      if (err)
        continue;

      String t = doc["validTime"].as<String>();
      if (t.length() < 16)
        continue;

      for (JsonObject p : doc["parameters"].as<JsonArray>()) {
        const char *name = p["name"] | "";
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

  /**
   * Read Next JSON Object from Stream
   *
   * Reads a single {} object from a JSON array in the stream
   * Handles nested braces, string escapes, and buffer overflow
   *
   * This is the core of the streaming parser - allows processing
   * huge JSON arrays one object at a time without loading the entire
   * response into memory
   *
   * @param stream HTTP response stream
   * @param buffer Output buffer for JSON object
   * @param bufSize Size of output buffer
   * @return true if object was successfully read, false on end of array or
   * error
   */
  bool readNextJsonObject(Stream &stream, char *buffer, size_t bufSize) {
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

      // Track string state to ignore braces inside string literals
      if (!escaped && c == '"') {
        inString = !inString;
      }
      escaped = (!escaped && c == '\\'); // Handle escape sequences

      if (!started) {
        if (c == '{') {
          started = true;
          braceCount = 1;
          if (pos < bufSize - 1)
            buffer[pos++] = c;
        } else if (c == ']') {
          return false; // End of JSONarray
        }
        // Skip whitespace and commas before object start
      } else {
        if (pos < bufSize - 1) {
          buffer[pos++] = c;
        } else {
          // Buffer overflow - skip this object
          while (braceCount > 0 && millis() < timeout) {
            if (stream.available()) {
              char skip = stream.read();
              if (!inString) {
                if (skip == '{')
                  braceCount++;
                else if (skip == '}')
                  braceCount--;
              }
              if (!escaped && skip == '"')
                inString = !inString;
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