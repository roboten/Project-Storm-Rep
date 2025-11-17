#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include <time.h>
#include <lvgl.h>

#include "stationPicker.hpp"   // använder gStations (lat/lon)

// Vill du rita i en specifik tile? Ge den som parent i TodayForecast_CreateOn(...)
extern lv_obj_t* t2; // valfri befintlig tile som fallback

// ---------------------------------------------
// Datamodell (timvis)
// ---------------------------------------------
struct TodayHour {
  String hhmm;   // "HH:MM" lokal tid
  float  temp;   // °C (SMHI param "t")
  int    symb;   // Wsymb2
};

// ---------------------------------------------
// UI + logik för "Today"
// ---------------------------------------------
class TodayForecastView {
public:
  TodayForecastView() : parent(nullptr), row(nullptr), title(nullptr) {}

  // Skapa UI-kontainer där timchipsen ritas
  void create(lv_obj_t* parent_container) {
    parent = parent_container ? parent_container : t2;
    if (!parent) return;

    title = lv_label_create(parent);
    lv_label_set_text(title, "Today");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, -2);

    row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 10, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 10);
  }

  // Hämta + rendera baserat på index vald i Settings
  bool fetchAndRenderForStationIdx(int station_idx) {
    if (station_idx < 0 || station_idx >= (int)gStations.size()) return false;
    char lat[16], lon[16];
    dtostrf(gStations[(size_t)station_idx].lat, 0, 4, lat);
    dtostrf(gStations[(size_t)station_idx].lon, 0, 4, lon);
    return fetchAndRenderForLatLon(lat, lon);
  }

  // Direkt med lat/lon
  bool fetchAndRenderForLatLon(const char* lat, const char* lon) {
    if (!lat || !lon) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    String url = build_pmp3g_url(lat, lon);
    WiFiClientSecure client; client.setInsecure();
    HTTPClient https;
    if (!https.begin(client, url)) return false;

    int code = https.GET();
    if (code != 200) { https.end(); return false; }
    String payload = https.getString();
    https.end();

    if (!parse_today(payload)) return false;
    render();
    return true;
  }

  void print() const {
    Serial.printf("[Today] %u hours\n", (unsigned)hours.size());
    for (auto& h : hours) {
      Serial.printf("%s  %.1f°C  sym=%d\n", h.hhmm.c_str(), h.temp, h.symb);
    }
  }

private:
  lv_obj_t* parent;
  lv_obj_t* row;
  lv_obj_t* title;
  std::vector<TodayHour> hours;

  static String build_pmp3g_url(const char* lat, const char* lon) {
    String url = "https://opendata-download-metfcst.smhi.se/api/category/pmp3g/version/2/geotype/point/lon/";
    url += lon; url += "/lat/"; url += lat; url += "/data.json";
    return url;
  }

  static String today_iso_date_local() { //Ändrade om
    time_t now; time(&now);
    struct tm* lt = localtime(&now);
    char buf[11]; 
    strftime(buf, sizeof(buf), "%Y-%m-%d", lt);
    return String(buf);
  }

  static bool param_float(const JsonArray& params, const char* name, float& out) {
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

  static bool param_int(const JsonArray& params, const char* name, int& out) {
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
  
  static String today_iso_date_utc() {
    time_t now; time(&now);
    struct tm* ut = gmtime(&now);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", ut);
    return String(buf);
  } 

  bool parse_today(const String& json) {
    DynamicJsonDocument doc(180000);
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;
    if (!doc.containsKey("timeSeries")) return false;

    String todayLocal = today_iso_date_local();
    String todayUtc = today_iso_date_utc();
    hours.clear();

    JsonArray ts = doc["timeSeries"].as<JsonArray>();
    for (JsonObject rec : ts) {
      String vt = rec["validTime"] | ""; // "YYYY-MM-DDTHH:MM:SSZ"
      if (vt.length() < 16) continue;
//      if (vt.substring(0,10) != today) continue; old

        String date = vt.substring(0,10);
        if (date != todayLocal && date != todayUtc) continue;

      JsonArray params = rec["parameters"].as<JsonArray>();
      float t = NAN; int sym = 0;
      param_float(params, "t", t);
      param_int(params, "Wsymb2", sym);

      TodayHour h;
      h.hhmm = vt.substring(11,16);
      h.temp = t;
      h.symb = sym;
      hours.push_back(h);
    }

    std::sort(hours.begin(), hours.end(),
      [](const TodayHour& a, const TodayHour& b){ return a.hhmm < b.hhmm; });

    return !hours.empty();
  }

  static const char* symb_to_emoji(int s) {
    if (s==1) return "☀️";
    if (s==2) return "🌤️";
    if (s==3||s==4||s==5) return "☁️";
    if (s==6||s==7||s==8||s==9) return "🌧️";
    if (s==10||s==11) return "⛈️";
    if (s==18) return "🌫️";
    return "❓";
  }

  void clear_row() { if (row) lv_obj_clean(row); }

  void render() {
    if (!parent || !row) return;
    clear_row();

    for (const auto& h : hours) {
      lv_obj_t* chip = lv_obj_create(row);
      lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
      lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_all(chip, 6, 0);
      lv_obj_set_style_radius(chip, 12, 0);
      lv_obj_set_style_bg_opa(chip, LV_OPA_20, 0);

      lv_obj_t* icon = lv_label_create(chip);
      lv_label_set_text(icon, symb_to_emoji(h.symb));

      lv_obj_t* t = lv_label_create(chip);
      char tb[16]; snprintf(tb, sizeof(tb), "%.0f°C", h.temp);
      lv_label_set_text(t, tb);

      lv_obj_t* tm = lv_label_create(chip);
      lv_label_set_text(tm, h.hhmm.c_str());
    }
  }
};

// ---------------------------------------------
// Globala, enkla hooks för integration
// ---------------------------------------------
static TodayForecastView g_today;

// Skapa UI en gång, t.ex. i din UI-init: TodayForecast_CreateOn(tile)
inline void TodayForecast_CreateOn(lv_obj_t* parent) { g_today.create(parent); }

// Kalla denna när en stad valts i Settings (efter att ok_idx satts)
inline void TodayForecast_OnStationSelected(int station_idx) {
  g_today.fetchAndRenderForStationIdx(station_idx); // tyst no-op vid fel
}