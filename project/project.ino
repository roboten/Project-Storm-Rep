#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include <vector>

#include "smhiApi.hpp"
#include "upcomingWeek.hpp"
#include "settingsTile.hpp"

// --------------------------------------------------------------------
// --- WiFi credentials ---
// --------------------------------------------------------------------
const char* WIFI_SSID = "Nothing";
const char* WIFI_PASSWORD = "hacM3Plz";

// --------------------------------------------------------------------
// --- Global objects shared with settingsTile.hpp ---
// --------------------------------------------------------------------
LilyGo_Class amoled;
SMHI_API weather("https://opendata-download-metobs.smhi.se/api/version/1.0/parameter/");
String period[3] = {"latest-day", "latest-days", "latest-month"};
lv_obj_t* t5 = nullptr;  // Settings tile pointer for global access

// --------------------------------------------------------------------
// --- UI global state ---
// --------------------------------------------------------------------
static lv_obj_t* tileview;
static lv_obj_t* chart;
static lv_chart_series_t* series;
static lv_obj_t* slider;

// --- States ---
static bool wifi_connected = false;
static bool initial_data_fetched = false;
static int g_window_start = 0;
static int g_window_size = 0;

// --------------------------------------------------------------------
// --- Forward declarations ---
// --------------------------------------------------------------------
static void connect_wifi_non_blocking();
static void create_ui();
static void setup_weather_screen();
static void chart_draw_event_cb(lv_event_t* e);
static void update_chart_from_slider(lv_event_t* e);

// --------------------------------------------------------------------
// --- WiFi (non-blocking) ---
// --------------------------------------------------------------------
static void connect_wifi_non_blocking() {
  static bool wifi_started = false;
  static uint32_t start_time = 0;

  if (!wifi_started) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("Connecting to WiFi SSID: %s\n", WIFI_SSID);
    wifi_started = true;
    start_time = millis();
  }

  if (WiFi.status() == WL_CONNECTED && !wifi_connected) {
    Serial.println("WiFi connected.");
    wifi_connected = true;
  } else if (WiFi.status() != WL_CONNECTED &&
             millis() - start_time > 30000) {
    Serial.println("WiFi connect timeout — retrying...");
    WiFi.disconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    start_time = millis();
  }
}

// --------------------------------------------------------------------
// --- UI creation ---
// --------------------------------------------------------------------
static void create_ui() {
  tileview = lv_tileview_create(NULL);

  // ---------------- Tile 1: Boot / Splash ----------------
  lv_obj_t* t1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t1, lv_color_white(), 0);
  lv_obj_t* splash_label = lv_label_create(t1);
  lv_label_set_text(splash_label, "Group 1\nVersion 0.1");
  lv_obj_set_style_text_font(splash_label, &lv_font_montserrat_28, 0);
  lv_obj_center(splash_label);

  // ---------------- Tile 2: Today’s Forecast ----------------
  lv_obj_t* t2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t2, lv_color_white(), 0);
  lv_obj_t* t2_label = lv_label_create(t2);
  lv_label_set_text(t2_label, "Today’s Forecast");
  lv_obj_center(t2_label);

  // ---------------- Tile 3: 7‑Day Forecast ----------------
  lv_obj_t* t3 = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t3, lv_color_white(), 0);
  lv_obj_t* t3_label = lv_label_create(t3);
  lv_label_set_text(t3_label, "7‑Day Forecast");
  lv_obj_center(t3_label);

  // ---------------- Tile 4: Historical Chart ----------------
  lv_obj_t* t4 = lv_tileview_add_tile(tileview, 3, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t4, lv_color_white(), 0);

  chart = lv_chart_create(t4);
  lv_obj_set_style_pad_left(chart, 64, 0);
  lv_obj_set_style_pad_bottom(chart, 36, 0);
  lv_obj_set_size(chart, lv_disp_get_hor_res(NULL) - 20,
                  lv_disp_get_ver_res(NULL) - 90);
  lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 10);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_obj_set_style_text_font(chart, &lv_font_montserrat_16,
                             LV_PART_TICKS | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(chart, lv_color_black(),
                              LV_PART_TICKS | LV_STATE_DEFAULT);
  lv_obj_add_event_cb(chart, chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN,
                      NULL);

  series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE),
                               LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 5, 15);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 12, 6, 6, 4, true, 60);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 10, 5, 2, 0, true, 36);

  slider = lv_slider_create(t4);
  lv_obj_set_width(slider, lv_disp_get_hor_res(NULL) - 40);
  lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_slider_set_range(slider, 0, 100);

  setup_weather_screen();

  // ---------------- Tile 5: Settings ----------------
  t5 = lv_tileview_add_tile(tileview, 4, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t5, lv_color_white(), 0);
  create_settings_tile();

  lv_scr_load_anim(tileview, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
}

// --------------------------------------------------------------------
// --- Chart X‑axis label callback ---
// --------------------------------------------------------------------
static void chart_draw_event_cb(lv_event_t* e) {
  lv_obj_t* obj = lv_event_get_target(e);
  if (obj != chart) return;

  auto* dsc = (lv_obj_draw_part_dsc_t*)lv_event_get_param(e);
  if (dsc->part != LV_PART_TICKS || dsc->id != LV_CHART_AXIS_PRIMARY_X) return;

  int idx_in_window = (int)(dsc->value + 0.5f);
  if (idx_in_window < 0 || idx_in_window >= g_window_size) return;

  int data_idx = g_window_start + idx_in_window;
  if (data_idx < 0 || data_idx >= (int)weatherData.size()) return;

  const char* hhmm = weatherData[data_idx].time.c_str();
  lv_snprintf(dsc->text, dsc->text_length, "%s", hhmm);
}

// --------------------------------------------------------------------
// --- Update chart based on slider ---
// --------------------------------------------------------------------
static void update_chart_from_slider(lv_event_t* e) {
  if (weatherData.empty()) return;

  int slider_value = lv_slider_get_value(slider);
  int total = (int)weatherData.size();
  const int max_points = 50;
  int window_size = min(max_points, total);
  int start = map(slider_value, 0, 100, 0, max(0, total - window_size));

  g_window_start = start;
  g_window_size = window_size;

  lv_chart_set_point_count(chart, window_size);
  for (int i = 0; i < window_size; i++) {
    float v = weatherData[start + i].temp;
    v = constrain(v, 5.0f, 15.0f);
    lv_chart_set_value_by_id(chart, series, i, v);
  }
  lv_chart_refresh(chart);
}

// --------------------------------------------------------------------
static void setup_weather_screen() {
  lv_obj_add_event_cb(slider, update_chart_from_slider,
                      LV_EVENT_VALUE_CHANGED, NULL);
  update_chart_from_slider(NULL);
}

// --------------------------------------------------------------------
// --- Arduino setup & loop ---
// --------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Starting LilyGo AMOLED setup...");

  if (!amoled.begin()) {
    Serial.println("Failed to init AMOLED");
    while (true) delay(1000);
  }

  amoled.setBrightness(255);
  amoled.setRotation(0);
  beginLvglHelper(amoled);
  delay(200);

  create_ui();
}

void loop() {
  lv_timer_handler();
  connect_wifi_non_blocking();

  if (wifi_connected && !initial_data_fetched) {
    initial_data_fetched = true;
    weather.update_weather_data(0, 0, period[2]);
    update_chart_from_slider(NULL);
  }

  delay(5);
}