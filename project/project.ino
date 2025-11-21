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
#include "stationPicker.hpp"
#include "settingsTile.hpp"
#include "upcomingWeek.hpp"

// --------------------------------------------------------------------
// Wi‑Fi
// --------------------------------------------------------------------
const char* WIFI_SSID = "Nothing";
const char* WIFI_PASSWORD = "hacM3Plz";

// --------------------------------------------------------------------
// Globals shared across modules
// --------------------------------------------------------------------
LilyGo_Class amoled;
SMHI_API weather("https://opendata-download-metobs.smhi.se/api/version/1.0/parameter/");
String period[3] = {"latest-day", "latest-days", "latest-month"};
lv_obj_t* t5 = NULL;

// Only **one real definition** of gStations and weatherData here
std::vector<StationInfo> gStations;
std::vector<DataPoint> weatherData;

// --------------------------------------------------------------------
// UI state
// --------------------------------------------------------------------
static lv_obj_t* tileview = NULL;
static lv_obj_t* chart = NULL;
static lv_chart_series_t* series = NULL;
static lv_obj_t* slider = NULL;
static lv_obj_t* forecast_container=NULL;



// States
static bool wifi_connected = false;
static bool stations_loaded = false;
static bool initial_data_fetched = false;

// Chart window state
static int g_window_start = 0;
static int g_window_size = 0;

// --------------------------------------------------------------------
// Forward declarations
// --------------------------------------------------------------------
static void connect_wifi_non_blocking();
static void create_ui();
static void setup_weather_screen();
static void chart_draw_event_cb(lv_event_t* e);
static void update_chart_from_slider(lv_event_t* e);

// --------------------------------------------------------------------
// Wi‑Fi (non-blocking)
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
// UI creation
// --------------------------------------------------------------------
static void create_ui() {
  tileview = lv_tileview_create(NULL);
  // Ensure tile swiping works
  lv_obj_set_scroll_dir(tileview, LV_DIR_ALL);
  lv_obj_add_flag(tileview, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_clear_flag(tileview, LV_OBJ_FLAG_SCROLL_ELASTIC);

  // Tile 1: Boot / Splash
  lv_obj_t* t1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t1, lv_color_white(), 0);
  lv_obj_t* splash_label = lv_label_create(t1);
  lv_label_set_text(splash_label, "Group 1\nVersion 0.3");
  lv_obj_set_style_text_font(splash_label, &lv_font_montserrat_28, 0);
  lv_obj_center(splash_label);

  // Tile 2: Today’s Forecast (placeholder)
  lv_obj_t* t2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t2, lv_color_white(), 0);
  lv_obj_t* t2_label = lv_label_create(t2);
  lv_label_set_text(t2_label, "Today’s Forecast");
  lv_obj_center(t2_label);

  // Tile 3: 7‑Day Forecast (placeholder)
  // Tile 3: 7-Day Forecast
  lv_obj_t* t3 = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t3, lv_color_white(), 0);

  // Create and assign the container
  forecast_container = lv_obj_create(t3);
  lv_obj_set_size(forecast_container, lv_disp_get_hor_res(NULL) - 20, lv_disp_get_ver_res(NULL) - 40);
  lv_obj_align(forecast_container, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_bg_color(forecast_container, lv_color_lighten(lv_color_hex(0x3366FF), 50), 0); // Light blue background for visibility
  lv_obj_set_flex_flow(forecast_container, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(forecast_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

  // Add a placeholder label to confirm the container is visible
  lv_obj_t* placeholder_label = lv_label_create(forecast_container);
  lv_label_set_text(placeholder_label, "Forecast will appear here");
  lv_obj_center(placeholder_label);


  // Tile 4: Historical Chart
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

  // Tile 5: Settings
  t5 = lv_tileview_add_tile(tileview, 4, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t5, lv_color_white(), 0);
  create_settings_tile();

  // Load
  lv_scr_load_anim(tileview, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
}

// --------------------------------------------------------------------
// Chart X‑axis label callback
// --------------------------------------------------------------------
static void chart_draw_event_cb(lv_event_t* e) {
  if (!chart) return;
  lv_obj_t* obj = lv_event_get_target(e);
  if (obj != chart) return;

  lv_obj_draw_part_dsc_t* dsc =
      (lv_obj_draw_part_dsc_t*)lv_event_get_param(e);
  if (dsc->part != LV_PART_TICKS || dsc->id != LV_CHART_AXIS_PRIMARY_X) return;

  int idx_in_window = (int)(dsc->value + 0.5f);
  if (idx_in_window < 0 || idx_in_window >= g_window_size) return;

  int data_idx = g_window_start + idx_in_window;
  if (data_idx < 0 || data_idx >= (int)weatherData.size()) return;

  const char* hhmm = weatherData[data_idx].time.c_str();
  lv_snprintf(dsc->text, dsc->text_length, "%s", hhmm);
}

// --------------------------------------------------------------------
// Update chart based on slider
// --------------------------------------------------------------------
static void update_chart_from_slider(lv_event_t* e) {
  if (!slider || !chart) return;
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
    if (v < 5.0f) v = 5.0f;
    if (v > 15.0f) v = 15.0f;
    lv_chart_set_value_by_id(chart, series, i, v);
  }
  lv_chart_refresh(chart);
}

static void setup_weather_screen() {
  lv_obj_add_event_cb(slider, update_chart_from_slider,
                      LV_EVENT_VALUE_CHANGED, NULL);
  update_chart_from_slider(NULL);
}


//---------------------------------------------
// Function to update the 7-day forecast UI
//---------------------------------------------
void update_7day_forecast_ui(const std::vector<DailyWeather>& forecast) {
    if (!forecast_container) return;

    lv_obj_clean(forecast_container); // Clear existing content

    if (forecast.empty()) {
        lv_obj_t* no_data_label = lv_label_create(forecast_container);
        lv_label_set_text(no_data_label, "No forecast data available.");
        lv_obj_center(no_data_label);
        return;
    }

    // Create UI elements for each day
    for (const auto& day : forecast) {
        lv_obj_t* day_card = lv_obj_create(forecast_container);
        lv_obj_set_size(day_card, 90, 120);
        lv_obj_set_style_bg_color(day_card, lv_color_hex(0xE0E0E0), 0); // Light gray background
        lv_obj_set_style_border_width(day_card, 1, 0);
        lv_obj_set_style_radius(day_card, 8, 0);

        lv_obj_t* date_label = lv_label_create(day_card);
        lv_label_set_text_fmt(date_label, "%s", day.date.c_str());
        lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* temp_label = lv_label_create(day_card);
        lv_label_set_text_fmt(temp_label, "%.1f/%.1f°C", day.tempMin, day.tempMax);
        lv_obj_align(temp_label, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t* symbol_label = lv_label_create(day_card);
        lv_label_set_text_fmt(symbol_label, "%d", day.symbolCode);
        lv_obj_align(symbol_label, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
}



// --------------------------------------------------------------------
// Arduino setup & loop
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

  if (wifi_connected && !stations_loaded) {
    // Fetch and select stations once
    stations_loaded = fetch_and_select_top_stations(10.0f, 50);
    if (stations_loaded) {
      Serial.println("Stations loaded, updating settings dropdown...");
      settings_update_city_options();  // fill dropdown with station names
    } else {
      Serial.println("Failed to load stations.");
    }
  }

  if (wifi_connected && stations_loaded && !initial_data_fetched) {
    initial_data_fetched = true;

    // Default to the first station if exists, Temperature, latest-month
    int station_idx = 0;
    if (!gStations.empty()) station_idx = 0;

    weather.update_weather_data(station_idx, /*param_idx=*/0, period[2]);
    update_chart_from_slider(NULL);
  }

  //7-day forecast
  // Replace the 7-day forecast block in loop() with this:
  if (wifi_connected && stations_loaded) {
    std::vector<DailyWeather> mock_forecast = {
        {"2025-11-18", 5.0, 10.0, 80.0, 0.0, 1},
        {"2025-11-19", 6.0, 11.0, 75.0, 0.0, 2},
        {"2025-11-20", 7.0, 12.0, 70.0, 0.0, 3},
        {"2025-11-21", 8.0, 13.0, 65.0, 0.0, 4},
        {"2025-11-22", 9.0, 14.0, 60.0, 0.0, 5},
        {"2025-11-23", 10.0, 15.0, 55.0, 0.0, 6},
        {"2025-11-24", 11.0, 16.0, 50.0, 0.0, 7}
    };
    update_7day_forecast_ui(mock_forecast);
    // Disable further updates to avoid redundancy
    static bool mock_data_shown = false;
    if (!mock_data_shown) {
        mock_data_shown = true;
    }
  }




  delay(5);
}