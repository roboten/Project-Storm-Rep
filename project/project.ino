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

#include "7dayForecast.hpp"
#include "settingsTile.hpp"
#include "smhiApi.hpp"
#include "stationPicker.hpp"

// --------------------------------------------------------------------
// Wi-Fi Configuration
// Network credentials for connecting to WiFi
// --------------------------------------------------------------------
const char *WIFI_SSID = "Nothing";
const char *WIFI_PASSWORD = "hacM3Plz";

// NTP (Network Time Protocol) configuration for time synchronization
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;     // GMT+1 (Swedish timezone)
const int daylightOffset_sec = 3600; // +1 hour for daylight saving time

// --------------------------------------------------------------------
// Global Objects
// Core application objects and data structures
// --------------------------------------------------------------------
LilyGo_Class amoled; // AMOLED display driver instance
// SMHI API wrapper for fetching meteorological observation data
SMHI_API weather("https://opendata-download-metobs.smhi.se/api/version/1.0/parameter/");

// UI tile objects (used across multiple files)
lv_obj_t *t4 = NULL; // Settings tile
lv_obj_t *t2 = NULL; // 7-day forecast tile

// Global data storage
std::vector<StationInfo> gStations; // All available SMHI weather stations
std::vector<DataPoint> weatherData; // Current weather data points for graphing

// --------------------------------------------------------------------
// UI State Variables
// Track UI components and application state
// --------------------------------------------------------------------
static lv_obj_t *tileview = NULL; // Main tileview container (4 tiles: splash,
                                  // forecast, chart, settings)
static lv_obj_t *chart =
    NULL; // Line chart object for weather data visualization
static lv_chart_series_t *series = NULL; // Data series for the chart
static lv_obj_t *slider = NULL; // Slider for scrolling through historical data

// Application state flags
static bool wifi_connected = false;  // True when WiFi connection is established
static bool stations_loaded = false; // True when station list has been loaded
static bool initial_data_fetched =
    false; // True when first weather data fetch is complete

// Chart windowing and scaling variables
static int g_window_start =
    0; // Index of first data point in current view window
static int g_window_size = 0; // Number of data points to display in window
static int g_y_min = -10;     // Minimum Y-axis value (temperature)
static int g_y_max = 20;      // Maximum Y-axis value (temperature)

// --------------------------------------------------------------------
// Graph Margins
// Control spacing around the chart drawing area
// Left margin is dynamic based on Y-axis label width
// --------------------------------------------------------------------
static int g_graph_margin_left = 32; // Dynamically adjusted for Y-axis labels
static const int GRAPH_MARGIN_RIGHT = 20; // Fixed right margin
static const int GRAPH_MARGIN_TOP = 20;   // Fixed top margin
static const int GRAPH_MARGIN_BOTTOM =
    40; // Fixed bottom margin for X-axis labels

// --------------------------------------------------------------------
// Forward declarations
// --------------------------------------------------------------------
static void connect_wifi_non_blocking();
static void create_ui();
static void setup_weather_screen();
static void chart_draw_event_cb(lv_event_t *e);
static void update_chart_from_slider(lv_event_t *e);
static int calculate_margin_for_range(int y_min, int y_max);

// --------------------------------------------------------------------
// WiFi Connection Management
// Non-blocking WiFi connection with automatic retry on timeout
// --------------------------------------------------------------------
static void connect_wifi_non_blocking() {
  static bool wifi_started = false;
  static uint32_t start_time = 0;

  // Start WiFi connection on first call
  if (!wifi_started) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifi_started = true;
    start_time = millis();
  }

  // Configure NTP time sync once connected
  if (WiFi.status() == WL_CONNECTED && !wifi_connected) {
    wifi_connected = true;
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }
  // Retry connection after 30 second timeout
  else if (WiFi.status() != WL_CONNECTED && millis() - start_time > 30000) {
    WiFi.disconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    start_time = millis();
  }
}

/**
 * Convert date string to display format
 * Handles both daily (YYYY-MM-DD) and monthly (YYYY-MM) formats
 * Returns a human-readable date string for X-axis labels
 */
static const char *get_day_name(const String &date_str) {
  static char day_buf[12];

  // YYYY-MM-DD format (10+ chars) -> show MM/DD
  if (date_str.length() >= 10) {
    int day = date_str.substring(8, 10).toInt();
    int month = date_str.substring(5, 7).toInt();
    snprintf(day_buf, sizeof(day_buf), "%02d/%02d", month, day);
    return day_buf;
  }

  // YYYY-MM format (7 chars) -> show month name or MM/YYYY
  if (date_str.length() >= 7) {
    int month = date_str.substring(5, 7).toInt();
    int year = date_str.substring(2, 4).toInt();
    snprintf(day_buf, sizeof(day_buf), "%02d/%02d", month, year);
    return day_buf;
  }

  return "";
}

// --------------------------------------------------------------------
// Dynamic Margin Calculation
// Calculate left margin based on Y-axis value range
// Ensures Y-axis labels don't get cut off for large numbers
// --------------------------------------------------------------------
static int calculate_margin_for_range(int y_min, int y_max) {
  // Find the largest absolute value to determine digit count
  int max_val = max(abs(y_min), abs(y_max));
  int digits = 1;
  while (max_val >= 10) {
    max_val /= 10;
    digits++;
  }
  // Add extra space if negative sign is needed
  if (y_min < 0)
    digits++;

  // Calculate margin: ~9 pixels per digit + 20 pixel padding, minimum 32
  return max(32, digits * 9 + 20);
}

// --------------------------------------------------------------------
// Chart Drawing Event Callback
// Handles custom drawing for the weather chart including:
// - Graph background and border
// - Y-axis temperature labels (right-aligned in left margin)
// - X-axis date labels
// - Data point lines and markers
// --------------------------------------------------------------------
static void chart_draw_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);

  // Draw graph background and border before main content
  if (code == LV_EVENT_DRAW_MAIN_BEGIN) {
    lv_draw_ctx_t *draw_ctx = (lv_draw_ctx_t *)lv_event_get_param(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    // Calculate actual graph drawing area (inside margins)
    int graphX = coords.x1 + g_graph_margin_left;
    int graphY = coords.y1 + GRAPH_MARGIN_TOP;
    int graphWidth =
        lv_obj_get_width(obj) - g_graph_margin_left - GRAPH_MARGIN_RIGHT;
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

  // Custom axis label drawing
  if (code == LV_EVENT_DRAW_PART_BEGIN) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);

    // Only process tick labels
    if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class,
                                     LV_CHART_DRAW_PART_TICK_LABEL))
      return;

    // Draw Y-axis temperature labels (right-aligned in left margin)
    if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
      char buf[16];
      lv_snprintf(buf, sizeof(buf), "%.0f", (float)dsc->value);

      lv_draw_label_dsc_t label_dsc;
      lv_draw_label_dsc_init(&label_dsc);
      label_dsc.color = lv_color_black();
      label_dsc.font = &lv_font_montserrat_14;
      label_dsc.align = LV_TEXT_ALIGN_RIGHT;

      lv_area_t coords;
      lv_obj_get_coords(obj, &coords);

      lv_area_t label_area;
      label_area.x1 = coords.x1 + 2;
      label_area.x2 = coords.x1 + g_graph_margin_left - 4;

      if (dsc->p1) {
        label_area.y1 = dsc->p1->y - 10;
        label_area.y2 = dsc->p1->y + 10;
        lv_draw_label(dsc->draw_ctx, &label_dsc, &label_area, buf, NULL);
      }

      if (dsc->text)
        dsc->text[0] = '\0';
      return;
    }

    // Draw X-axis date labels
    if (dsc->id == LV_CHART_AXIS_PRIMARY_X && dsc->text) {
      if (weatherData.empty() || g_window_size == 0)
        return;

      int tick_idx = (int)(dsc->value + 0.5f);
      int major_cnt = 5; // Number of major ticks on X-axis

      // Map tick index to actual data point in window
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

  // Draw data lines and points after main chart is rendered
  if (code == LV_EVENT_DRAW_POST) {
    lv_draw_ctx_t *draw_ctx = (lv_draw_ctx_t *)lv_event_get_param(e);

    if (weatherData.empty() || g_window_size < 2)
      return;

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int graphX = coords.x1 + g_graph_margin_left;
    int graphY = coords.y1 + GRAPH_MARGIN_TOP;
    int graphWidth =
        lv_obj_get_width(obj) - g_graph_margin_left - GRAPH_MARGIN_RIGHT;
    int graphHeight =
        lv_obj_get_height(obj) - GRAPH_MARGIN_TOP - GRAPH_MARGIN_BOTTOM;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = 3;
    line_dsc.color = lv_palette_main(LV_PALETTE_BLUE);
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;

    lv_draw_rect_dsc_t point_dsc;
    lv_draw_rect_dsc_init(&point_dsc);
    point_dsc.bg_color = lv_color_white();
    point_dsc.border_color = lv_palette_main(LV_PALETTE_BLUE);
    point_dsc.border_width = 2;
    point_dsc.radius = LV_RADIUS_CIRCLE;

    // Calculate Y-axis range for scaling data points
    int y_range = g_y_max - g_y_min;
    if (y_range <= 0)
      y_range = 1;

    lv_point_t p1;
    bool has_p1 = false;

    // Draw lines connecting data points
    for (int i = 0; i < g_window_size; i++) {
      int data_idx = g_window_start + i;
      if (data_idx >= (int)weatherData.size())
        break;

      float val = weatherData[data_idx].temp;

      // Calculate position within graph bounds
      int x_offset = (i * (graphWidth - 1)) / (g_window_size - 1);
      // Invert Y (higher temps at top) and scale to graph height
      int y_offset =
          (int)((1.0f - (val - g_y_min) / (float)y_range) * (graphHeight - 1));

      lv_point_t p2;
      p2.x = graphX + x_offset;
      p2.y = graphY + y_offset;

      if (has_p1) {
        lv_draw_line(draw_ctx, &line_dsc, &p1, &p2);
      }

      p1 = p2;
      has_p1 = true;
    }

    // Draw circular markers at each data point
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
// Chart Update from Slider
// Updates visible data window and Y-axis range based on slider position
// Recalculates margins dynamically for proper label sizing
// --------------------------------------------------------------------
static void update_chart_from_slider(lv_event_t *e) {
  if (!slider || !chart)
    return;
  if (weatherData.empty())
    return;

  int slider_value = lv_slider_get_value(slider);
  int total = (int)weatherData.size();
  const int max_points = 50; // Limit visible points for performance
  int window_size = min(max_points, total);
  // Map slider (0-100) to data range
  int start = map(slider_value, 0, 100, 0, max(0, total - window_size));

  g_window_start = start;
  g_window_size = window_size;

  // Find min/max temperature in current window for Y-axis scaling
  float min_val = 100000.0f;
  float max_val = -100000.0f;

  for (int i = 0; i < window_size; i++) {
    float v = weatherData[start + i].temp;
    if (v < min_val)
      min_val = v;
    if (v > max_val)
      max_val = v;
  }

  // Add padding to Y-axis range for better visibility
  int y_min = (int)floor(min_val - 2.0f);
  int y_max = (int)ceil(max_val + 2.0f);
  g_y_min = y_min;
  g_y_max = y_max;

  // Recalculate left margin based on new Y-axis values
  g_graph_margin_left = calculate_margin_for_range(y_min, y_max);

  // Apply new margin to chart
  lv_obj_set_style_pad_left(chart, g_graph_margin_left, LV_PART_MAIN);

  // Update chart with new range and data
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
  lv_chart_set_point_count(chart, window_size);

  // Populate chart data points
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
// UI Creation
// Creates a 4-tile horizontal tileview:
// Tile 0: Splash screen
// Tile 1: 7-day forecast
// Tile 2: Historical data chart with slider
// Tile 3: Settings page
// --------------------------------------------------------------------
static void create_ui() {
  tileview = lv_tileview_create(NULL);
  lv_obj_set_scroll_dir(tileview, LV_DIR_ALL);
  lv_obj_add_flag(tileview, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_clear_flag(tileview, LV_OBJ_FLAG_SCROLL_ELASTIC);

  // Tile 1: Splash screen with version info
  lv_obj_t *t1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t1, lv_color_white(), 0);
  lv_obj_t *splash_label = lv_label_create(t1);
  lv_label_set_text(splash_label, "Group 1\nVersion 0.9");
  lv_obj_set_style_text_font(splash_label, &lv_font_montserrat_28, 0);
  lv_obj_center(splash_label);

  // Tile 2: 7-day weather forecast
  t2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t2, lv_color_white(), 0);
  SevenDayForecast_CreateOn(
      t2); // Create forecast cards (defined in 7dayForecast.hpp)

  // Tile 3: Historical data chart with custom drawing
  lv_obj_t *t3 = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t3, lv_color_white(), 0);

  // Create chart with custom styling
  chart = lv_chart_create(t3);
  lv_obj_remove_style_all(chart); // Start with clean slate
  // Size chart to fit screen with margins
  lv_obj_set_size(chart, lv_disp_get_hor_res(NULL) - 20,
                  lv_disp_get_ver_res(NULL) - 90);
  lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(chart, 0, LV_PART_MAIN);

  lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(chart, lv_color_make(220, 220, 220),LV_PART_MAIN);

  lv_obj_set_style_line_opa(chart, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);

  lv_obj_set_style_pad_left(chart, g_graph_margin_left, LV_PART_MAIN);
  lv_obj_set_style_pad_right(chart, GRAPH_MARGIN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_pad_top(chart, GRAPH_MARGIN_TOP, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(chart, GRAPH_MARGIN_BOTTOM, LV_PART_MAIN);

  lv_obj_set_style_text_font(chart, &lv_font_montserrat_14, LV_PART_TICKS);
  lv_obj_set_style_text_color(chart, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_color(chart, lv_color_black(), LV_PART_TICKS);

  lv_obj_set_style_line_color(chart, lv_color_black(), LV_PART_TICKS);
  lv_obj_set_style_line_width(chart, 1, LV_PART_TICKS);

  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_obj_set_scrollbar_mode(chart, LV_SCROLLBAR_MODE_OFF);

  // Register custom draw callback for manual line/point rendering
  lv_obj_add_event_cb(chart, chart_draw_event_cb, LV_EVENT_ALL, NULL);

  // Create data series
  series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE),LV_CHART_AXIS_PRIMARY_Y);

  // Configure axis tick marks
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 8, 4, 6, 2, true, 100);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 8, 4, 5, 2, true, 30);

  // Set grid line counts
  lv_chart_set_div_line_count(chart, 5, 6);

  // Create slider for scrolling through historical data
  slider = lv_slider_create(t3);
  lv_obj_set_width(slider, lv_disp_get_hor_res(NULL) - 40);
  lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_slider_set_range(slider, 0, 100); // 0 = oldest data, 100 = newest data
  setup_weather_screen();              // Connect slider to chart update handler

  // Tile 4: Settings page for city/parameter selection
  t4 = lv_tileview_add_tile(tileview, 3, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(t4, lv_color_white(), 0);
  create_settings_tile(); // Create settings UI (defined in settingsTile.hpp)

  lv_scr_load_anim(tileview, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
}

// --------------------------------------------------------------------
// Arduino Setup Function
// Initializes display, LVGL, and UI
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

/**
 * Main Loop
 * Handles:
 * - LVGL timer updates for UI
 * - Non-blocking WiFi connection
 * - Station list loading once WiFi is connected
 * - Initial weather data fetch with saved preferences
 */
void loop() {
  lv_timer_handler();          // Process LVGL UI updates
  connect_wifi_non_blocking(); // Maintain WiFi connection

  // Load station list once WiFi is connected
  if (wifi_connected && !stations_loaded) {
    stations_loaded = fetch_and_select_top_stations(10.0f, 50);
    if (stations_loaded) {
      settings_update_city_options(); // Populate city dropdown
    }
  }

  // Perform initial data fetch once after everything is loaded
  if (wifi_connected && stations_loaded && !initial_data_fetched) {
    initial_data_fetched = true;

    // Default values if no saved preferences
    int station_idx = -1;
    int param_code = 1; // Temperature (1 hour)
    String city_name = "Karlskrona";

    // Try to find Karlskrona station as default
    for (size_t i = 0; i < gStations.size(); ++i) {
      if (gStations[i].name.equalsIgnoreCase("Karlskrona") ||
          gStations[i].name.startsWith("Karlskrona")) {
        station_idx = (int)i;
        break;
      }
    }

    if (station_idx < 0) {
      for (size_t i = 0; i < gStations.size(); ++i) {
        if (gStations[i].name.indexOf("Karlskrona") >= 0) {
          station_idx = (int)i;
          break;
        }
      }
    }

    // Load saved preferences (overrides defaults)
    Preferences prefs;
    if (prefs.begin("weather", true)) { // Read-only mode
      String st_id = prefs.getString("station_id", "");
      int saved_param = prefs.getInt("param_code", -1);
      String saved_city = prefs.getString("city_name", "");
      prefs.end();

      if (!st_id.isEmpty()) {
        for (size_t i = 0; i < gStations.size(); ++i) {
          if (gStations[i].id == st_id) {
            station_idx = (int)i;
            break;
          }
        }
      }

      if (saved_param > 0) {
        param_code = saved_param;
      }

      if (!saved_city.isEmpty()) {
        city_name = saved_city;
      }

      Serial.printf("Loaded settings: station_id=%s, param_code=%d, city=%s\n",
                    st_id.c_str(), param_code, city_name.c_str());
    }

    if (station_idx < 0 && !gStations.empty()) {
      station_idx = 0;
      city_name = "Stockholm";
    }

    Serial.printf("Using station_idx=%d, param_code=%d, city=%s\n", station_idx,
                  param_code, city_name.c_str());

    // Fetch and display initial weather data
    if (station_idx >= 0) {
      weather.update_weather_data(station_idx, param_code, "latest-months");
      update_chart_from_slider(NULL); // Update chart with fetched data
      SevenDayForecast_OnStationSelected(station_idx); // Update 7-day forecast
      settings_sync_state(station_idx, param_code,
                          city_name); // Sync settings UI
    }
  }

  delay(5);
}