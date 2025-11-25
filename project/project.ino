#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LV_Helper.h>
#include <LilyGo_AMOLED.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <lvgl.h>
#include <string>
#include <vector>

#include "time.h"

#include "settingsTile.hpp"
#include "smhiApi.hpp"
#include "stationPicker.hpp"
#include "todayForecast.hpp"
#include "upcomingWeek.hpp"

// --------------------------------------------------------------------
// Wi-Fi
// --------------------------------------------------------------------
const char *WIFI_SSID = "Nothing";
const char *WIFI_PASSWORD = "hacM3Plz";

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

// --------------------------------------------------------------------
// Globals
// --------------------------------------------------------------------
LilyGo_Class amoled;
SMHI_API weather(
    "https://opendata-download-metobs.smhi.se/api/version/1.0/parameter/");

lv_obj_t *t5 = NULL;
lv_obj_t *t2 = NULL;

std::vector<StationInfo> gStations;
std::vector<DataPoint> weatherData;

// --------------------------------------------------------------------
// UI state
// --------------------------------------------------------------------
static lv_obj_t *tileview = NULL;
static lv_obj_t *chart = NULL;
static lv_chart_series_t *series = NULL;
static lv_obj_t *slider = NULL;
static lv_obj_t *forecast_container = NULL;

// States
static bool wifi_connected = false;
static bool stations_loaded = false;
static bool initial_data_fetched = false;

static int g_window_start = 0;
static int g_window_size = 0;
static int g_y_min = -10;
static int g_y_max = 20;

// --------------------------------------------------------------------
// Graph Margins
// --------------------------------------------------------------------
static const int GRAPH_MARGIN_LEFT = 32;
static const int GRAPH_MARGIN_RIGHT = 20;
static const int GRAPH_MARGIN_TOP = 20;
static const int GRAPH_MARGIN_BOTTOM = 40;

// --------------------------------------------------------------------
// Forward declarations
// --------------------------------------------------------------------
static void connect_wifi_non_blocking();
static void create_ui();
static void setup_weather_screen();
static void chart_draw_event_cb(lv_event_t *e);
static void update_chart_from_slider(lv_event_t *e);
void update_7day_forecast_ui(const std::vector<DailyWeather> &forecast);

// --------------------------------------------------------------------
// Wi-Fi
// --------------------------------------------------------------------
static void connect_wifi_non_blocking() {
  static bool wifi_started = false;
  static uint32_t start_time = 0;

  if (!wifi_started) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifi_started = true;
    start_time = millis();
  }

  if (WiFi.status() == WL_CONNECTED && !wifi_connected) {
    wifi_connected = true;
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  } else if (WiFi.status() != WL_CONNECTED && millis() - start_time > 30000) {
    WiFi.disconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    start_time = millis();
  }
}

static const char *get_day_name(const String &date_str) {
  if (date_str.length() >= 10) {
    static char day_buf[8];
    int day = date_str.substring(8, 10).toInt();
    int month = date_str.substring(5, 7).toInt();
    snprintf(day_buf, sizeof(day_buf), "%02d/%02d", month, day);
    return day_buf;
  }
  return "";
}

// --------------------------------------------------------------------
// Chart Callback
// --------------------------------------------------------------------
static void chart_draw_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);

  // 1. DRAW GRAPH BOUNDING BOX FIRST
  if (code == LV_EVENT_DRAW_MAIN_BEGIN) {
    lv_draw_ctx_t *draw_ctx = (lv_draw_ctx_t *)lv_event_get_param(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    // CALCULATE GRAPH BOUNDS ONCE
    int graphX = coords.x1 + GRAPH_MARGIN_LEFT;
    int graphY = coords.y1 + GRAPH_MARGIN_TOP;
    int graphWidth =
        lv_obj_get_width(obj) - GRAPH_MARGIN_LEFT - GRAPH_MARGIN_RIGHT;
    int graphHeight =
        lv_obj_get_height(obj) - GRAPH_MARGIN_TOP - GRAPH_MARGIN_BOTTOM;

    lv_area_t graph_area;
    graph_area.x1 = graphX;
    graph_area.y1 = graphY;
    graph_area.x2 = graphX + graphWidth - 1;
    graph_area.y2 = graphY + graphHeight - 1;

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_white();
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.border_width = 1;
    rect_dsc.border_color = lv_color_make(200, 200, 200);
    rect_dsc.radius = 0;

    lv_draw_rect(draw_ctx, &rect_dsc, &graph_area);
    return;
  }

  // 2. DRAW LABELS
  if (code == LV_EVENT_DRAW_PART_BEGIN) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);

    if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class,
                                     LV_CHART_DRAW_PART_TICK_LABEL))
      return;

    // Y-AXIS LABELS (LEFT-ALIGNED at FAR LEFT)
    if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
      char buf[16];
      lv_snprintf(buf, sizeof(buf), "%.0f", (float)dsc->value);

      lv_draw_label_dsc_t label_dsc;
      lv_draw_label_dsc_init(&label_dsc);
      label_dsc.color = lv_color_black();
      label_dsc.font = &lv_font_montserrat_14;
      label_dsc.align = LV_TEXT_ALIGN_LEFT; // Align LEFT

      // Use the chart's absolute coordinates to find the far left edge
      lv_area_t coords;
      lv_obj_get_coords(obj, &coords);

      lv_area_t label_area;
      // Start from the very left edge of the widget (coords.x1)
      // End at the start of the graph area (coords.x1 + GRAPH_MARGIN_LEFT)
      label_area.x1 = coords.x1 + 8; // Add small padding from edge
      label_area.x2 = coords.x1 + GRAPH_MARGIN_LEFT - 2;

      // Center vertically around the tick
      if (dsc->p1) {
        label_area.y1 = dsc->p1->y - 10;
        label_area.y2 = dsc->p1->y + 10;
        lv_draw_label(dsc->draw_ctx, &label_dsc, &label_area, buf, NULL);
      }

      // Prevent default drawing
      if (dsc->text)
        dsc->text[0] = '\0';
      return;
    }

    // X-AXIS LABEL DRAWING
    if (dsc->id == LV_CHART_AXIS_PRIMARY_X && dsc->text) {
      if (weatherData.empty() || g_window_size == 0)
        return;

      int tick_idx = (int)(dsc->value + 0.5f);
      int major_cnt = 5;

      int idx_in_window = 0;
      if (major_cnt > 1 && g_window_size > 1) {
        idx_in_window = (tick_idx * (g_window_size - 1)) / (major_cnt - 1);
      }

      if (idx_in_window < 0)
        idx_in_window = 0;
      if (idx_in_window >= g_window_size)
        idx_in_window = g_window_size - 1;

      int data_idx = g_window_start + idx_in_window;
      if (data_idx < 0 || data_idx >= (int)weatherData.size())
        return;

      const String &date_str = weatherData[data_idx].date;
      const char *day_label = get_day_name(date_str);

      if (day_label && strlen(day_label) > 0) {
        lv_snprintf(dsc->text, dsc->text_length, "%s", day_label);
      } else {
        const char *hhmm = weatherData[data_idx].time.c_str();
        lv_snprintf(dsc->text, dsc->text_length, "%s", hhmm);
      }
      return;
    }
  }

  // 3. CUSTOM DRAWING LOOP (LINES & POINTS)
  if (code == LV_EVENT_DRAW_POST) {
    lv_draw_ctx_t *draw_ctx = (lv_draw_ctx_t *)lv_event_get_param(e);

    if (weatherData.empty() || g_window_size < 2)
      return;

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    // RE-CALCULATE GRAPH BOUNDS (Must match exactly)
    int graphX = coords.x1 + GRAPH_MARGIN_LEFT;
    int graphY = coords.y1 + GRAPH_MARGIN_TOP;
    int graphWidth =
        lv_obj_get_width(obj) - GRAPH_MARGIN_LEFT - GRAPH_MARGIN_RIGHT;
    int graphHeight =
        lv_obj_get_height(obj) - GRAPH_MARGIN_TOP - GRAPH_MARGIN_BOTTOM;

    // Define clipping area to strictly inside the graph box
    lv_area_t clip_area;
    clip_area.x1 = graphX;
    clip_area.y1 = graphY;
    clip_area.x2 = graphX + graphWidth - 1;
    clip_area.y2 = graphY + graphHeight - 1;

    // Line style
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = 3;
    line_dsc.color = lv_palette_main(LV_PALETTE_BLUE);
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;

    // Point style
    lv_draw_rect_dsc_t point_dsc;
    lv_draw_rect_dsc_init(&point_dsc);
    point_dsc.bg_color = lv_color_white();
    point_dsc.border_color = lv_palette_main(LV_PALETTE_BLUE);
    point_dsc.border_width = 2;
    point_dsc.radius = LV_RADIUS_CIRCLE;

    int y_range = g_y_max - g_y_min;
    if (y_range <= 0)
      y_range = 1;

    lv_point_t p1;
    bool has_p1 = false;

    // DRAWING LOOP
    for (int i = 0; i < g_window_size; i++) {
      int data_idx = g_window_start + i;
      if (data_idx >= (int)weatherData.size())
        break;

      float val = weatherData[data_idx].temp;

      // CALCULATE COORDINATES MANUALLY
      // X: Distribute evenly across graphWidth
      // Y: Map value to height (inverted because Y grows down)

      int x_offset = (i * (graphWidth - 1)) / (g_window_size - 1);
      int y_offset =
          (int)((1.0f - (val - g_y_min) / (float)y_range) * (graphHeight - 1));

      // APPLY OFFSETS
      lv_point_t p2;
      p2.x = graphX + x_offset;
      p2.y = graphY + y_offset;

      // Draw Line
      if (has_p1) {
        lv_draw_line(draw_ctx, &line_dsc, &p1, &p2);
      }

      p1 = p2;
      has_p1 = true;
    }

    // Draw Points on top (second pass to ensure they are above lines)
    for (int i = 0; i < g_window_size; i++) {
      int data_idx = g_window_start + i;
      if (data_idx >= (int)weatherData.size())
        break;

      float val = weatherData[data_idx].temp;

      int x_offset = (i * (graphWidth - 1)) / (g_window_size - 1);
      int y_offset =
          (int)((1.0f - (val - g_y_min) / (float)y_range) * (graphHeight - 1));

      lv_area_t point_area;
      point_area.x1 = graphX + x_offset - 3;
      point_area.y1 = graphY + y_offset - 3;
      point_area.x2 = graphX + x_offset + 3;
      point_area.y2 = graphY + y_offset + 3;

      lv_draw_rect(draw_ctx, &point_dsc, &point_area);
    }
  }
}

// --------------------------------------------------------------------
// Update Chart
// --------------------------------------------------------------------
static void update_chart_from_slider(lv_event_t *e) {
  if (!slider || !chart)
    return;
  if (weatherData.empty())
    return;

  int slider_value = lv_slider_get_value(slider);
  int total = (int)weatherData.size();
  const int max_points = 50;
  int window_size = min(max_points, total);
  int start = map(slider_value, 0, 100, 0, max(0, total - window_size));

  g_window_start = start;
  g_window_size = window_size;

  float min_val = 100.0f;
  float max_val = -100.0f;

  for (int i = 0; i < window_size; i++) {
    float v = weatherData[start + i].temp;
    if (v < min_val)
      min_val = v;
    if (v > max_val)
      max_val = v;
  }

  int y_min = (int)floor(min_val - 2.0f);
  int y_max = (int)ceil(max_val + 2.0f);
  g_y_min = y_min;
  g_y_max = y_max;

  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
  lv_chart_set_point_count(chart, window_size);

  for (int i = 0; i < window_size; i++) {
    float v = weatherData[start + i].temp;
    lv_chart_set_value_by_id(chart, series, i, v);
  }

  lv_chart_refresh(chart);
  lv_obj_invalidate(chart);
}

static void setup_weather_screen() {
  lv_obj_add_event_cb(slider, update_chart_from_slider, LV_EVENT_VALUE_CHANGED,
                      NULL);
  update_chart_from_slider(NULL);
}

// --------------------------------------------------------------------
// Create UI
// --------------------------------------------------------------------
static void create_ui() {
  tileview = lv_tileview_create(NULL);
  lv_obj_set_scroll_dir(tileview, LV_DIR_ALL);
  lv_obj_add_flag(tileview, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_clear_flag(tileview, LV_OBJ_FLAG_SCROLL_ELASTIC);

  // 1. Splash
  lv_obj_t *t1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t1, lv_color_white(), 0);
  lv_obj_t *splash_label = lv_label_create(t1);
  lv_label_set_text(splash_label, "Group 1\nVersion 0.3");
  lv_obj_set_style_text_font(splash_label, &lv_font_montserrat_28, 0);
  lv_obj_center(splash_label);

  // 2. Today
  t2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t2, lv_color_white(), 0);
  TodayForecast_CreateOn(t2);

  // 3. 7-Day
  lv_obj_t *t3 = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t3, lv_color_white(), 0);
  forecast_container = lv_obj_create(t3);
  lv_obj_set_size(forecast_container, lv_disp_get_hor_res(NULL) - 20,
                  lv_disp_get_ver_res(NULL) - 40);
  lv_obj_align(forecast_container, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_bg_color(forecast_container,
                            lv_color_lighten(lv_color_hex(0x3366FF), 50), 0);
  lv_obj_set_flex_flow(forecast_container, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(forecast_container, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

  // 4. Chart
  lv_obj_t *t4 = lv_tileview_add_tile(tileview, 3, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t4, lv_color_white(), 0);

  // --- CHART SETUP START ---
  chart = lv_chart_create(t4);

  // 1. RESET: Remove all styles to prevent white-text defaults
  lv_obj_remove_style_all(chart);

  // 2. SIZE
  lv_obj_set_size(chart, lv_disp_get_hor_res(NULL) - 20,
                  lv_disp_get_ver_res(NULL) - 90);
  lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 10);

  // 3. BACKGROUND (Transparent - we draw it manually)
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(chart, 0, LV_PART_MAIN);

  // 4. GRID LINES
  lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(chart, lv_color_make(220, 220, 220),
                              LV_PART_MAIN);

  // 5. SERIES STYLES
  // HIDE default series lines and points (we draw them manually)
  lv_obj_set_style_line_opa(chart, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR); // Ensure size is 0

  // 6. PADDING (Matches fixed margins)
  // This ensures data points are mapped to the correct area
  lv_obj_set_style_pad_left(chart, GRAPH_MARGIN_LEFT, LV_PART_MAIN);
  lv_obj_set_style_pad_right(chart, GRAPH_MARGIN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_pad_top(chart, GRAPH_MARGIN_TOP, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(chart, GRAPH_MARGIN_BOTTOM, LV_PART_MAIN);

  // 7. TEXT STYLE
  // Use a readable font for axis labels
  lv_obj_set_style_text_font(chart, &lv_font_montserrat_14, LV_PART_TICKS);
  // Force BLACK text on all parts
  lv_obj_set_style_text_color(chart, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_color(chart, lv_color_black(), LV_PART_TICKS);

  // 8. TICK MARKS
  lv_obj_set_style_line_color(chart, lv_color_black(), LV_PART_TICKS);
  lv_obj_set_style_line_width(chart, 1, LV_PART_TICKS);

  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_obj_set_scrollbar_mode(chart, LV_SCROLLBAR_MODE_OFF);

  // 9. OVERFLOW
  // Removed LV_OBJ_FLAG_OVERFLOW_VISIBLE to ensure lines are clipped to the
  // graph area lv_obj_add_flag(chart, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  // Callback - Listen to ALL events to handle MAIN_DRAW
  lv_obj_add_event_cb(chart, chart_draw_event_cb, LV_EVENT_ALL, NULL);

  series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE),
                               LV_CHART_AXIS_PRIMARY_Y);

  // 10. AXIS CONFIG
  // draw_size matched to padding to prevent clipping
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 8, 4, 6, 2, true, 100);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 8, 4, 5, 2, true, 30);

  lv_chart_set_div_line_count(chart, 5, 6);

  slider = lv_slider_create(t4);
  lv_obj_set_width(slider, lv_disp_get_hor_res(NULL) - 40);
  lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_slider_set_range(slider, 0, 100);
  setup_weather_screen();
  // --- CHART SETUP END ---

  // 5. Settings
  t5 = lv_tileview_add_tile(tileview, 4, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t5, lv_color_white(), 0);
  create_settings_tile();

  lv_scr_load_anim(tileview, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
}

void update_7day_forecast_ui(const std::vector<DailyWeather> &forecast) {
  if (!forecast_container)
    return;
  lv_obj_clean(forecast_container);

  if (forecast.empty()) {
    lv_obj_t *no_data_label = lv_label_create(forecast_container);
    lv_label_set_text(no_data_label, "No forecast data available.");
    lv_obj_center(no_data_label);
    return;
  }

  for (const auto &day : forecast) {
    lv_obj_t *day_card = lv_obj_create(forecast_container);
    lv_obj_set_size(day_card, 90, 120);
    lv_obj_set_style_bg_color(day_card, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(day_card, 1, 0);
    lv_obj_set_style_radius(day_card, 8, 0);

    lv_obj_t *date_label = lv_label_create(day_card);
    lv_label_set_text_fmt(date_label, "%s", day.date.c_str());
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *temp_label = lv_label_create(day_card);
    lv_label_set_text_fmt(temp_label, "%.1f/%.1f°C", day.tempMin, day.tempMax);
    lv_obj_align(temp_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *symbol_label = lv_label_create(day_card);
    lv_label_set_text_fmt(symbol_label, "%d", day.symbolCode);
    lv_obj_align(symbol_label, LV_ALIGN_BOTTOM_MID, 0, -4);
  }
}

// --------------------------------------------------------------------
// Arduino Setup
// --------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  if (!amoled.begin()) {
    while (true)
      delay(1000);
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
    stations_loaded = fetch_and_select_top_stations(10.0f, 50);
    if (stations_loaded) {
      settings_update_city_options();
    }
  }

  if (wifi_connected && stations_loaded && !initial_data_fetched) {
    initial_data_fetched = true;

    int station_idx = 0;
    int param_idx = 0;

    for (size_t i = 0; i < gStations.size(); ++i) {
      if (gStations[i].name.equalsIgnoreCase("Karlskrona")) {
        station_idx = (int)i;
        break;
      }
    }

    Preferences prefs;
    if (prefs.begin("weather", true)) {
      String st_id = prefs.getString("station_id", "");
      param_idx = prefs.getInt("param_idx", 0);
      prefs.end();

      if (!st_id.isEmpty()) {
        for (size_t i = 0; i < gStations.size(); ++i) {
          if (gStations[i].id == st_id) {
            station_idx = (int)i;
            break;
          }
        }
      }
    }

    if (gStations.empty())
      station_idx = -1;

    if (station_idx >= 0) {
      weather.update_weather_data(station_idx, param_idx, "latest-months");
      update_chart_from_slider(NULL);
      TodayForecast_OnStationSelected(station_idx);
      settings_sync_state(station_idx, param_idx);
    }

    std::vector<DailyWeather> mock_forecast = {
        {String("2025-11-21"), 5.0, 10.0, 80.0, 0.0, 1},
        {String("2025-11-22"), 6.0, 11.0, 75.0, 0.0, 2},
        {String("2025-11-23"), 7.0, 12.0, 70.0, 0.0, 3},
        {String("2025-11-24"), 8.0, 13.0, 65.0, 0.0, 4},
        {String("2025-11-25"), 9.0, 14.0, 60.0, 0.0, 5},
        {String("2025-11-26"), 10.0, 15.0, 55.0, 0.0, 6},
        {String("2025-11-27"), 11.0, 16.0, 50.0, 0.0, 7}};
    update_7day_forecast_ui(mock_forecast);
  }

  delay(5);
}