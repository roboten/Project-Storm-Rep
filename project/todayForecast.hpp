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

extern lv_obj_t *t2;

struct DayForecast {
  String date;      // YYYY-MM-DD
  String weekday;   // Mon, Tue, etc.
  float temp;
  int symb;
};

class WeekForecastView {
public:
  WeekForecastView() : parent(nullptr), row(nullptr), title(nullptr) {}

  void create(lv_obj_t *parent_container) {
    parent = parent_container ? parent_container : t2;
    if (!parent)
      return;

    title = lv_label_create(parent);
    lv_label_set_text(title, "7-Day Forecast");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 10);

    row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 5, 0);
    lv_obj_set_style_pad_gap(row, 15, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
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
    Serial.print("WeekForecast: fetching ");
    Serial.println(url);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);

    HTTPClient https;
    https.setReuse(true);

    if (!https.begin(client, url)) {
      Serial.println("WeekForecast: Connection failed");
      return false;
    }

    int code = https.GET();
    if (code != 200) {
      Serial.printf("WeekForecast: HTTP Error %d\n", code);
      https.end();
      return false;
    }

    Stream &stream = https.getStream();

    unsigned long start = millis();
    while (stream.available() == 0 && millis() - start < 3000) {
      delay(10);
    }

    bool success = parse_iteratively(stream);
    https.end();

    if (success) {
      render();
      Serial.printf("WeekForecast: Rendered %d days\n", (int)days.size());
    } else {
      Serial.println("WeekForecast: Parsing failed or no data found");
    }
    return success;
  }

private:
  lv_obj_t *parent;
  lv_obj_t *row;
  lv_obj_t *title;
  std::vector<DayForecast> days;

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

  static const char *get_weekday_name(const String &dateStr) {
    // Parse YYYY-MM-DD
    int year = dateStr.substring(0, 4).toInt();
    int month = dateStr.substring(5, 7).toInt();
    int day = dateStr.substring(8, 10).toInt();

    // Zeller's congruence for Gregorian calendar
    if (month < 3) {
      month += 12;
      year--;
    }
    int k = year % 100;
    int j = year / 100;
    int h = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
    // h: 0=Sat, 1=Sun, 2=Mon, ...
    static const char *names[] = {"Sat", "Sun", "Mon", "Tue",
                                  "Wed", "Thu", "Fri"};
    if (h < 0)
      h += 7;
    return names[h];
  }

  bool readNextObject(Stream &stream, char *buffer, size_t bufSize) {
    size_t pos = 0;
    int braceCount = 0;
    bool started = false;
    unsigned long lastRead = millis();

    while (true) {
      if (millis() - lastRead > 5000)
        return false;

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
            return false;
          }
        } else {
          if (pos < bufSize - 1)
            buffer[pos++] = c;

          if (c == '{') {
            braceCount++;
          } else if (c == '}') {
            braceCount--;
            if (braceCount == 0) {
              buffer[pos] = 0;
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

  bool parse_iteratively(Stream &stream) {
    days.clear();

    if (!stream.find("\"timeSeries\"")) {
      Serial.println("WeekForecast: 'timeSeries' not found in stream");
      return false;
    }
    if (!stream.find("[")) {
      Serial.println("WeekForecast: Array start '[' not found");
      return false;
    }

    StaticJsonDocument<256> filter;
    filter["validTime"] = true;
    filter["parameters"][0]["name"] = true;
    filter["parameters"][0]["values"][0] = true;

    char jsonBuf[4096];
    DynamicJsonDocument chunkDoc(6144);

    String lastDate = "";
    int count = 0;

    while (count < 7) {
      if (readNextObject(stream, jsonBuf, sizeof(jsonBuf))) {
        chunkDoc.clear();
        DeserializationError err = deserializeJson(
            chunkDoc, jsonBuf, DeserializationOption::Filter(filter));

        if (err) {
          Serial.print("Chunk parse error: ");
          Serial.println(err.c_str());
          continue;
        }

        String vt = chunkDoc["validTime"] | "";
        if (vt.length() >= 16) {
          // Extract hour (UTC)
          int hour = vt.substring(11, 13).toInt();
          String date = vt.substring(0, 10);

          // Only take 12:00 UTC entries, and only one per day
          if (hour == 12 && date != lastDate) {
            JsonArray params = chunkDoc["parameters"].as<JsonArray>();
            float t = NAN;
            int sym = 0;
            if (param_float(params, "t", t) &&
                param_int(params, "Wsymb2", sym)) {
              DayForecast d;
              d.date = date;
              d.weekday = get_weekday_name(date);
              d.temp = t;
              d.symb = sym;
              days.push_back(d);
              lastDate = date;
              count++;
            }
          }
        }
      } else {
        break;
      }
    }

    return !days.empty();
  }

  void clear_row() {
    if (row)
      lv_obj_clean(row);
  }

  void render() {
    if (!parent || !row)
      return;
    clear_row();

    for (const auto &d : days) {
      lv_obj_t *chip = lv_obj_create(row);
      lv_obj_set_size(chip, 140, lv_pct(95));
      lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_all(chip, 2, 0);
      lv_obj_set_style_radius(chip, 10, 0);
      lv_obj_set_style_bg_color(chip, lv_color_hex(0x2C3E50), 0);
      lv_obj_set_style_border_width(chip, 0, 0);
      lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

      // Weekday name
      lv_obj_t *weekdayLabel = lv_label_create(chip);
      lv_label_set_text(weekdayLabel, d.weekday.c_str());
      lv_obj_set_style_text_font(weekdayLabel, &lv_font_montserrat_20, 0);
      lv_obj_set_style_text_color(weekdayLabel, lv_color_hex(0xFFFFFF), 0);

      // Date (MM/DD format)
      lv_obj_t *dateLabel = lv_label_create(chip);
      String dateStr =
          d.date.substring(5, 7) + "/" + d.date.substring(8, 10);
      lv_label_set_text(dateLabel, dateStr.c_str());
      lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_color(dateLabel, lv_color_hex(0xAAAAAA), 0);

      // Icon container
      lv_obj_t *icon_cont = lv_obj_create(chip);
      lv_obj_set_size(icon_cont, 100, 100);
      lv_obj_set_style_bg_opa(icon_cont, LV_OPA_0, 0);
      lv_obj_set_style_border_width(icon_cont, 0, 0);
      lv_obj_clear_flag(icon_cont, LV_OBJ_FLAG_SCROLLABLE);

      draw_weather_icon(icon_cont, d.symb, 90);

      // Temperature (12:00 UTC = 13:00 Swedish time in winter)
      lv_obj_t *t = lv_label_create(chip);
      lv_label_set_text_fmt(t, "%.0f°C", d.temp);
      lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
      lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
    }
  }
};

static WeekForecastView g_week;
inline void TodayForecast_CreateOn(lv_obj_t *parent) { g_week.create(parent); }
inline void TodayForecast_OnStationSelected(int station_idx) {
  g_week.fetchAndRenderForStationIdx(station_idx);
}