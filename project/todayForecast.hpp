#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <lvgl.h>
#include <time.h>
#include <vector>

#include "stationPicker.hpp"
#include "weatherIcons.hpp"

// Extern to global t2 defined in project.ino
extern lv_obj_t *t2;

struct TodayHour {
  String hhmm;
  float temp;
  int symb;
};

class TodayForecastView {
public:
  TodayForecastView() : parent(nullptr), row(nullptr), title(nullptr) {}

  void create(lv_obj_t *parent_container) {
    parent = parent_container ? parent_container : t2;
    if (!parent)
      return;

    title = lv_label_create(parent);
    lv_label_set_text(title, "Forecast (Next 24h)");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 10);

    row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), lv_pct(100)); // Fill available space
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 5, 0);
    lv_obj_set_style_pad_gap(row, 15, 0); // Increased gap
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_pad_bottom(row, 10,
                                0); // Add padding at bottom for scrollbar
    lv_obj_set_scroll_dir(row, LV_DIR_HOR); // Ensure horizontal scrolling
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_align_to(row, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
  }

  bool fetchAndRenderForStationIdx(int station_idx) {
    if (station_idx < 0 || station_idx >= (int)gStations.size())
      return false;
    char lat[16], lon[16];
    dtostrf(gStations[(size_t)station_idx].lat, 0, 4, lat);
    dtostrf(gStations[(size_t)station_idx].lon, 0, 4, lon);
    return fetchAndRenderForLatLon(lat, lon);
  }

  bool fetchAndRenderForLatLon(const char *lat, const char *lon) {
    if (!lat || !lon)
      return false;
    if (WiFi.status() != WL_CONNECTED)
      return false;

    String url = build_pmp3g_url(lat, lon);
    Serial.print("TodayForecast: fetching ");
    Serial.println(url);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);

    HTTPClient https;
    https.setReuse(true);

    if (!https.begin(client, url)) {
      Serial.println("TodayForecast: Connection failed");
      return false;
    }

    int code = https.GET();
    if (code != 200) {
      Serial.printf("TodayForecast: HTTP Error %d\n", code);
      https.end();
      return false;
    }

    // Get Stream
    Stream &stream = https.getStream();

    // Wait a tiny bit for data
    unsigned long start = millis();
    while (stream.available() == 0 && millis() - start < 3000) {
      delay(10);
    }

    // Use iterative parser
    bool success = parse_iteratively(stream);
    https.end();

    if (success) {
      render();
      Serial.printf("TodayForecast: Rendered %d hours\n", (int)hours.size());
    } else {
      Serial.println("TodayForecast: Parsing failed or no data found");
    }
    return success;
  }

private:
  lv_obj_t *parent;
  lv_obj_t *row;
  lv_obj_t *title;
  std::vector<TodayHour> hours;

  static String build_pmp3g_url(const char *lat, const char *lon) {
    String url = "https://opendata-download-metfcst.smhi.se/api/category/pmp3g/"
                 "version/2/geotype/point/lon/";
    url += lon;
    url += "/lat/";
    url += lat;
    url += "/data.json";
    return url;
  }

  static bool param_float(const JsonArray &params, const char *name,
                          float &out) {
    for (JsonObject p : params) {
      if (strcmp(p["name"] | "", name) == 0) {
        if (p["values"].is<JsonArray>() && p["values"].size() > 0) {
          out = p["values"][0].as<float>();
          return true;
        }
      }
    }
    return false;
  }

  static bool param_int(const JsonArray &params, const char *name, int &out) {
    for (JsonObject p : params) {
      if (strcmp(p["name"] | "", name) == 0) {
        if (p["values"].is<JsonArray>() && p["values"].size() > 0) {
          out = p["values"][0].as<int>();
          return true;
        }
      }
    }
    return false;
  }

  // Helper to read one JSON object from the stream
  // Returns true if an object was read, false if end of array or error
  bool readNextObject(Stream &stream, char *buffer, size_t bufSize) {
    size_t pos = 0;
    int braceCount = 0;
    bool started = false;
    unsigned long lastRead = millis();

    while (true) {
      if (millis() - lastRead > 5000)
        return false; // Timeout

      if (stream.available()) {
        char c = stream.read();
        lastRead = millis();

        if (!started) {
          if (c == '{') {
            started = true;
            braceCount = 1;
            if (pos < bufSize - 1)
              buffer[pos++] = c;
          } else if (c == ']') {
            return false; // End of array
          }
          // Ignore other chars like , or whitespace before {
        } else {
          if (pos < bufSize - 1)
            buffer[pos++] = c;

          if (c == '{') {
            braceCount++;
          } else if (c == '}') {
            braceCount--;
            if (braceCount == 0) {
              buffer[pos] = 0; // Null terminate
              return true;
            }
          }
        }
      } else {
        delay(5);
      }
    }
    return false;
  }

  // === THE FIX: Iterative Parser ===
  // This reads the stream one entry at a time.
  // It consumes almost 0 RAM regardless of file size.
  bool parse_iteratively(Stream &stream) {
    hours.clear();

    // 1. Fast-forward stream to "timeSeries" array
    if (!stream.find("\"timeSeries\"")) {
      Serial.println("TodayForecast: 'timeSeries' not found in stream");
      return false;
    }
    if (!stream.find("[")) {
      Serial.println("TodayForecast: Array start '[' not found");
      return false;
    }

    // 2. Prepare Filter
    StaticJsonDocument<256> filter;
    filter["validTime"] = true;
    filter["parameters"][0]["name"] = true;
    filter["parameters"][0]["values"][0] = true;

    // 3. Loop through the array items one by one
    int count = 0;
    char jsonBuf[4096];
    // Use DynamicJsonDocument on heap to avoid stack overflow
    DynamicJsonDocument chunkDoc(6144);

    while (count < 24) {
      // Inverted check: if readNextObject returns true, we HAVE data
      if (readNextObject(stream, jsonBuf, sizeof(jsonBuf))) {

        chunkDoc.clear();
        DeserializationError err = deserializeJson(
            chunkDoc, jsonBuf, DeserializationOption::Filter(filter));

        if (err) {
          Serial.print("Chunk parse error: ");
          Serial.println(err.c_str());
          continue;
        }

        // Process this single hour
        String vt = chunkDoc["validTime"] | "";
        if (vt.length() >= 16) {
          JsonArray params = chunkDoc["parameters"].as<JsonArray>();
          float t = NAN;
          int sym = 0;
          if (param_float(params, "t", t) && param_int(params, "Wsymb2", sym)) {
            TodayHour h;

            // Adjust time to UTC+1 (Stockholm)
            int hh = vt.substring(11, 13).toInt();
            hh = (hh + 1) % 24;
            char buf[8];
            sprintf(buf, "%02d:%s", hh, vt.substring(14, 16).c_str());
            h.hhmm = String(buf);

            h.temp = t;
            h.symb = sym;
            hours.push_back(h);
            count++;
          }
        }
      } else {
        // Failed to read next object (end of array or error)
        break;
      }
    }

    return !hours.empty();
  }

  void clear_row() {
    if (row)
      lv_obj_clean(row);
  }

  void render() {
    if (!parent || !row)
      return;
    clear_row();

    for (const auto &h : hours) {
      lv_obj_t *chip = lv_obj_create(row);
      lv_obj_set_size(chip, 140, lv_pct(95)); // Much wider and taller
      lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_all(chip, 2, 0);
      lv_obj_set_style_radius(chip, 10, 0);
      lv_obj_set_style_bg_color(chip, lv_color_hex(0x2C3E50),
                                0); // Dark background
      lv_obj_set_style_border_width(chip, 0, 0);
      lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

      lv_obj_t *tm = lv_label_create(chip);
      lv_label_set_text(tm, h.hhmm.c_str());
      lv_obj_set_style_text_font(tm, &lv_font_montserrat_20, 0); // Larger font
      lv_obj_set_style_text_color(tm, lv_color_hex(0xFFFFFF),
                                  0); // White text

      // Icon container
      lv_obj_t *icon_cont = lv_obj_create(chip);
      lv_obj_set_size(icon_cont, 120, 120); // Much larger icon container
      lv_obj_set_style_bg_opa(icon_cont, LV_OPA_0, 0);
      lv_obj_set_style_border_width(icon_cont, 0, 0);
      lv_obj_clear_flag(icon_cont, LV_OBJ_FLAG_SCROLLABLE);

      draw_weather_icon(icon_cont, h.symb, 110); // Draw big icon

      lv_obj_t *t = lv_label_create(chip);
      lv_label_set_text_fmt(t, "%.0f°", h.temp);
      lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);  // Larger font
      lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0); // White text
    }
  }
};

static TodayForecastView g_today;
inline void TodayForecast_CreateOn(lv_obj_t *parent) { g_today.create(parent); }
inline void TodayForecast_OnStationSelected(int station_idx) {
  g_today.fetchAndRenderForStationIdx(station_idx);
}