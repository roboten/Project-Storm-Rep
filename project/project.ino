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

// --------------------------------------------------------------------
// --- WiFi ---
// --------------------------------------------------------------------
static const char* WIFI_SSID = "Nothing";
static const char* WIFI_PASSWORD = "hacM3Plz";

// SMHI
SMHI_API weather(
    "https://opendata-download-metobs.smhi.se/api/version/1.0/parameter/");
String period[3] = {"latest-day", "latest-days", "latest-month"};

LilyGo_Class amoled;

// --- UI objects ---
static lv_obj_t* tileview;
static lv_obj_t* t1;
static lv_obj_t* t2;
static lv_obj_t* t3;
static lv_obj_t* t4;
static lv_obj_t* t5;
static lv_obj_t* chart;
static lv_chart_series_t* series;
static lv_obj_t* slider;
static lv_obj_t* splash_screen;
static lv_obj_t* city_dropdown;
static lv_obj_t* param_dropdown;

// State
static bool wifi_connected = false;
static bool main_screen_loaded = false;
static bool splash_deleted = false;
static bool initial_data_fetched = false;

// Window state for X-axis labeling
static int g_window_start = 0;
static int g_window_size = 0;

// --------------------------------------------------------------------
// --- Forward declarations ---
// --------------------------------------------------------------------
static void switch_to_main(lv_timer_t* timer);
static void connect_wifi_non_blocking();
static void create_ui();
static void setup_weather_screen();
static void create_settings_tile();
static void chart_draw_event_cb(lv_event_t* e);
static void update_chart_from_slider(lv_event_t* e);

// --------------------------------------------------------------------
// --- Timer: switch splash -> main ---
// --------------------------------------------------------------------
static void switch_to_main(lv_timer_t* timer) {
  LV_UNUSED(timer);
  main_screen_loaded = true;
  lv_scr_load_anim(tileview, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
}

// --------------------------------------------------------------------
// --- WiFi (non-blocking) ---
// --------------------------------------------------------------------
static void connect_wifi_non_blocking() {
  static bool wifi_started = false;
  static uint32_t start_time = 0;

  if (!wifi_started) {
    WiFi.scanNetworks();
    Serial.printf("Connecting to WiFi SSID: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifi_started = true;
    start_time = millis();
  }
  if (WiFi.status() == WL_CONNECTED && !wifi_connected) {
    Serial.println("WiFi connected.");
    wifi_connected = true;
  } else if (WiFi.status() != WL_CONNECTED &&
             millis() - start_time > 30000) {
    Serial.println("WiFi could not connect (timeout), retrying...");
    Serial.print("WiFi status: ");
    Serial.println(WiFi.status());
    WiFi.disconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    start_time = millis();
  }
}

// --------------------------------------------------------------------
// --- UI ---
// --------------------------------------------------------------------
static void create_ui() {
  // Splash
  splash_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(splash_screen, lv_color_white(), 0);
  lv_obj_t* splash_label = lv_label_create(splash_screen);
  lv_label_set_text(splash_label, "Group 1\nVersion 0.1");
  lv_obj_set_style_text_font(splash_label, &lv_font_montserrat_28, 0);
  lv_obj_center(splash_label);
  lv_scr_load(splash_screen);

  // Tileview
  tileview = lv_tileview_create(NULL);

  // Tile 1
  t1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t1, lv_color_white(), 0);
  lv_obj_t* t1_label = lv_label_create(t1);
  lv_label_set_text(t1_label, "Tile 1");
  lv_obj_set_style_text_color(t1_label, lv_color_black(), 0);
  lv_obj_center(t1_label);

  // Tile 2
  t2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t2, lv_color_white(), 0);
  lv_obj_t* t2_label = lv_label_create(t2);
  lv_label_set_text(t2_label, "Main Screen");
  lv_obj_set_style_text_color(t2_label, lv_color_black(), 0);
  lv_obj_center(t2_label);

  // Tile 3: Chart + slider
  t3 = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t3, lv_color_white(), 0);

  chart = lv_chart_create(t3);

  // Make room for axis labels
  lv_obj_set_style_pad_left(chart, 64, 0);    // space for Y labels
  lv_obj_set_style_pad_bottom(chart, 36, 0);  // space for X labels

  lv_obj_set_size(chart, lv_disp_get_hor_res(NULL) - 20,
                  lv_disp_get_ver_res(NULL) - 90);
  lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 10);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);

  // Improve tick label readability
  lv_obj_set_style_text_font(chart, &lv_font_montserrat_16,
                             LV_PART_TICKS | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(chart, lv_color_black(),
                              LV_PART_TICKS | LV_STATE_DEFAULT);

  // Series
  series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE),
                               LV_CHART_AXIS_PRIMARY_Y);

  // Fixed Y-axis 5..15 with visible ticks and labels
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 5, 15);
  // major_len=12, minor_len=6, major_cnt=6 (5,7,9,11,13,15), minor_cnt=4,
  // label_en=true, draw_size=60 (reserve space)
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 12, 6, 6, 4, true, 60);

  // X-axis: labels enabled (we'll provide custom labels via draw event)
  // We'll update major_cnt dynamically in update_chart_from_slider()
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 10, 5, 2, 0, true, 36);

  // Draw event to supply custom X labels (HH:MM)
  lv_obj_add_event_cb(chart, chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

  // Slider
  slider = lv_slider_create(t3);
  lv_obj_set_width(slider, lv_disp_get_hor_res(NULL) - 40);
  lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_slider_set_range(slider, 0, 100);

  setup_weather_screen();

  // Tile 4: Settings
  t4 = lv_tileview_add_tile(tileview, 3, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t4, lv_color_white(), 0);
  create_settings_tile();

  // Tile 5: Placeholder
  t5 = lv_tileview_add_tile(tileview, 4, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t5, lv_color_white(), 0);
  lv_obj_t* t5_label = lv_label_create(t5);
  lv_label_set_text(t5_label, "Loading upcoming week weather...");
  lv_obj_set_style_text_font(t5_label, &lv_font_montserrat_24, 0);
  lv_obj_center(t5_label);

  // Defaults for dropdowns if created immediately in settings tile
  if (city_dropdown) lv_dropdown_set_selected(city_dropdown, 0);
  if (param_dropdown) lv_dropdown_set_selected(param_dropdown, 0);

  // Splash -> main
  lv_timer_t* boot_timer = lv_timer_create(switch_to_main, 3000, NULL);
  lv_timer_set_repeat_count(boot_timer, 1);
}

static void create_settings_tile() {
  city_dropdown = lv_dropdown_create(t4);
  lv_dropdown_set_options_static(city_dropdown,
                                 "Karlskrona\nStockholm\nGöteborg");
  lv_obj_align(city_dropdown, LV_ALIGN_TOP_MID, 0, 20);

  param_dropdown = lv_dropdown_create(t4);
  lv_dropdown_set_options_static(param_dropdown,
                                 "Temperature\nPrecipitation\nWind");
  lv_obj_align(param_dropdown, LV_ALIGN_TOP_MID, 0, 70);

  auto cb = [](lv_event_t* e) {
    int city_idx = lv_dropdown_get_selected(city_dropdown);
    int param_idx = lv_dropdown_get_selected(param_dropdown);
    weather.update_weather_data(city_idx, param_idx, period[0]);
    // After new data, refresh chart window and labels
    update_chart_from_slider(NULL);
  };
  lv_obj_add_event_cb(city_dropdown, cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(param_dropdown, cb, LV_EVENT_VALUE_CHANGED, NULL);
}

// --------------------------------------------------------------------
// --- Chart X-axis custom labels via draw event ---
//     Shows HH:MM for the visible points, spaced by major ticks
// --------------------------------------------------------------------
static void chart_draw_event_cb(lv_event_t* e) 
{
  lv_obj_t* obj = lv_event_get_target(e);
  if (obj != chart) return;

  lv_obj_draw_part_dsc_t* dsc =
      (lv_obj_draw_part_dsc_t*)lv_event_get_param(e);

  if (dsc->part != LV_PART_TICKS) return;
  if (dsc->id != LV_CHART_AXIS_PRIMARY_X) return;

  // dsc->value is the x "index" at the tick (0..point_count-1)
  int idx_in_window = (int)(dsc->value + 0.5f);
  if (idx_in_window < 0 || idx_in_window >= g_window_size) return;

  int data_idx = g_window_start + idx_in_window;
  if (data_idx < 0 || data_idx >= (int)weatherData.size()) return;

  // Write into LVGL’s provided label buffer
  const char* hhmm = weatherData[data_idx].time.c_str(); // "HH:MM"
  lv_snprintf(dsc->text, dsc->text_length, "%s", hhmm);
}

// --------------------------------------------------------------------
// --- Update chart from slider ---
// --------------------------------------------------------------------
static void update_chart_from_slider(lv_event_t* e) {
  if (weatherData.empty()) return;

  int slider_value = lv_slider_get_value(slider);
  int total = (int)weatherData.size();

  // Max points displayed at once (balance readability)
  const int max_points = 50;
  int window_size = min(max_points, total);
  if (window_size <= 0) return;

  int start = map(slider_value, 0, 100, 0, max(0, total - window_size));

  // Save window for X label callback
  g_window_start = start;
  g_window_size = window_size;

  // Fixed Y range as requested
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 5, 15);

  // Update X-axis major tick count to avoid overcrowding labels
  // Aim ~8–10 labels
  int desired_labels = 9;
  int major_cnt = max(2, min(desired_labels, window_size));
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 10, 5, major_cnt, 1,
                         true, 36);

  // Set points and values
  lv_chart_set_point_count(chart, window_size);
  for (int i = 0; i < window_size; i++) {
    if (start + i < total) {
      float v = weatherData[start + i].temp;
      // Keep the chart in-range visually; anything outside will clamp
      if (v < 5) v = 5;
      if (v > 15) v = 15;
      lv_chart_set_value_by_id(chart, series, i, v);
    } else {
      lv_chart_set_value_by_id(chart, series, i, LV_CHART_POINT_NONE);
    }
  }

  lv_chart_refresh(chart);
}

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
  amoled.setRotation(0);  // landscape
  beginLvglHelper(amoled);
  lv_timer_handler();
  delay(500);

  create_ui();
  delay(500);
}

void loop() {
  lv_timer_handler();

  if (main_screen_loaded && !splash_deleted) {
    static uint32_t animation_start_time = 0;
    if (animation_start_time == 0) animation_start_time = millis();
    if (millis() - animation_start_time > 600) {
      lv_obj_del_async(splash_screen);
      splash_deleted = true;
    }
  }

  connect_wifi_non_blocking();

  // Initial fetch once WiFi is up
  if (wifi_connected && !initial_data_fetched) {
    initial_data_fetched = true;
    // Karlskrona, Temperature, latest-day
    weather.update_weather_data(0, 0, period[0]);
    update_chart_from_slider(NULL);
  }

  delay(5);
}