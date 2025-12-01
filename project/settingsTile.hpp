#pragma once
#include "smhiApi.hpp"
#include "stationPicker.hpp"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <lvgl.h>
#include <map>
#include <string>
#include <vector>

extern lv_obj_t *t5;
extern SMHI_API weather;
extern void TodayForecast_OnStationSelected(int station_idx);

static lv_obj_t *kb = NULL;
static lv_obj_t *search_box = NULL;
static lv_obj_t *city_dropdown = NULL;
static lv_obj_t *param_dropdown = NULL;
static lv_obj_t *param_loading_label = NULL;

static int current_station_idx = -1;

static std::map<String, bool> stationValidity;

static std::vector<int> g_available_param_indices;

// Cache: station_id -> list of available parameter indices
static std::map<String, std::vector<int>> g_param_cache;

// Cache for stations that have NO data (to avoid re-checking)
static std::map<String, bool> g_station_no_data_cache;

// SMHI parameter codes and their names
static const int PARAM_CODES[] = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40};
static const int PARAM_COUNT = sizeof(PARAM_CODES) / sizeof(PARAM_CODES[0]);

static const char *PARAM_NAMES[] = {
    "Temperature (1h)",
    "Temperature (Daily Avg)",
    "Wind Direction",
    "Wind Speed",
    "Precipitation (Daily)",
    "Relative Humidity",
    "Precipitation (1h)",
    "Snow Depth",
    "Air Pressure",
    "Sunshine Time",
    "Global Irradiance",
    "Visibility",
    "Current Weather",
    "Precipitation (15m)",
    "Total Cloud Cover",
    "Precipitation (2x/day)",
    "Precipitation (1x/day)",
    "Temp Min (Daily)",
    "Temp Max (Daily)",
    "Wind Gust",
    "Temperature (Monthly)",
    "Precipitation (Monthly)",
    "Longwave Irradiance",
    "Max Mean Wind Speed",
    "Temp Min (12h)",
    "Temp Max (12h)",
    "Cloud Base (Lowest)",
    "Cloud Amount (Lowest)",
    "Cloud Base (2nd)",
    "Cloud Amount (2nd)",
    "Cloud Base (3rd)",
    "Cloud Amount (3rd)",
    "Cloud Base (4th)",
    "Cloud Amount (4th)",
    "Cloud Base (Low Mom)",
    "Cloud Base (Low Min)",
    "Precip Intensity (Max)",
    "Dew Point",
    "Ground State"};

static bool g_index_built = false;
static std::vector<String> g_station_names_folded;

// ------------------------------------------------------------------
// UTF-8 diacritic folding
// ------------------------------------------------------------------
static String fold_sv_ascii_lower(const String &s) {
  String out;
  out.reserve(s.length());
  for (unsigned i = 0; i < s.length(); ++i) {
    unsigned char c = (unsigned char)s[i];
    if (c == 0xC3 && i + 1 < s.length()) {
      unsigned char d = (unsigned char)s[i + 1];
      if (d == 0xA5 || d == 0x85) { out += 'a'; ++i; continue; }
      if (d == 0xA4 || d == 0x84) { out += 'a'; ++i; continue; }
      if (d == 0xB6 || d == 0x96) { out += 'o'; ++i; continue; }
      if (d == 0xA9 || d == 0x89) { out += 'e'; ++i; continue; }
      if (d == 0xA8 || d == 0x88) { out += 'e'; ++i; continue; }
      if (d == 0xB8 || d == 0x98) { out += 'o'; ++i; continue; }
      if (d == 0xA6 || d == 0x86) { out += "ae"; ++i; continue; }
    }
    out += (char)tolower(c);
  }
  return out;
}

static bool contains_folded(const String &hay, const String &needle) {
  return fold_sv_ascii_lower(hay).indexOf(fold_sv_ascii_lower(needle)) >= 0;
}

// ------------------------------------------------------------------
// Alias map
// ------------------------------------------------------------------
static std::map<String, std::vector<String>> g_city_alias = {
    {"gothenburg", {"Goteborg", "Göteborg"}},
    {"goteborg", {"Goteborg", "Göteborg"}},
    {"malmo", {"Malmo", "Malmö"}},
    {"vasteras", {"Vasteras", "Västerås"}},
    {"orebro", {"Orebro", "Örebro"}},
    {"gavle", {"Gavle", "Gävle"}},
    {"jonkoping", {"Jonkoping", "Jönköping"}},
    {"norrkoping", {"Norrkoping", "Norrköping"}},
    {"angelholm", {"Angelholm", "Ängelholm"}},
    {"ostersund", {"Ostersund", "Östersund"}},
    {"harnosand", {"Harnosand", "Härnösand"}},
    {"hassleholm", {"Hassleholm", "Hässleholm"}},
    {"nynashamn", {"Nynashamn", "Nynäshamn"}},
    {"vaxjo", {"Vaxjo", "Växjö"}},
    {"taby", {"Taby", "Täby"}},
    {"sodertalje", {"Sodertalje", "Södertälje"}},
    {"umea", {"Umea", "Umeå"}},
    {"skelleftea", {"Skelleftea", "Skellefteå"}},
    {"pitea", {"Pitea", "Piteå"}},
    {"lulea", {"Lulea", "Luleå"}},
    {"borlange", {"Borlange", "Borlänge"}},
    {"alvsjo", {"Alvsjo", "Älvsjö"}},
    {"vanersborg", {"Vanersborg", "Vänersborg"}},
    {"nassjo", {"Nassjo", "Nässjö"}},
    {"hoganas", {"Hoganas", "Höganäs"}},
    {"varmdo", {"Varmdo", "Värmdö"}},
    {"karlskrona", {"Karlskrona"}}};

static std::vector<String> city_search_terms(const String &englishCity) {
  std::vector<String> terms;
  String key = fold_sv_ascii_lower(englishCity);
  auto it = g_city_alias.find(key);
  if (it != g_city_alias.end()) {
    for (const auto &t : it->second)
      terms.push_back(t);
  }
  terms.push_back(englishCity);
  return terms;
}

// ------------------------------------------------------------------
// Build folded station name index
// ------------------------------------------------------------------
static void build_station_index_once() {
  if (g_index_built)
    return;
  g_station_names_folded.clear();
  g_station_names_folded.reserve(gStations.size());
  for (size_t i = 0; i < gStations.size(); ++i) {
    g_station_names_folded.push_back(fold_sv_ascii_lower(gStations[i].name));
  }
  g_index_built = true;
}

// ------------------------------------------------------------------
// Find city name in TOP_100_CITIES from station name
// ------------------------------------------------------------------
static String find_city_name_for_station(const String &stationName) {
  String foldedStation = fold_sv_ascii_lower(stationName);
  
  for (size_t i = 0; i < TOP_100_COUNT; ++i) {
    String cityFolded = fold_sv_ascii_lower(String(TOP_100_CITIES[i]));
    if (foldedStation == cityFolded) {
      return String(TOP_100_CITIES[i]);
    }
  }
  
  for (size_t i = 0; i < TOP_100_COUNT; ++i) {
    String cityFolded = fold_sv_ascii_lower(String(TOP_100_CITIES[i]));
    if (foldedStation.startsWith(cityFolded)) {
      return String(TOP_100_CITIES[i]);
    }
  }
  
  for (size_t i = 0; i < TOP_100_COUNT; ++i) {
    String cityFolded = fold_sv_ascii_lower(String(TOP_100_CITIES[i]));
    if (foldedStation.indexOf(cityFolded) >= 0) {
      return String(TOP_100_CITIES[i]);
    }
  }
  
  return "";
}

// ------------------------------------------------------------------
// Select city in dropdown by name
// ------------------------------------------------------------------
static void select_city_in_dropdown(const String &cityName) {
  if (!city_dropdown || cityName.isEmpty())
    return;
    
  for (size_t i = 0; i < TOP_100_COUNT; ++i) {
    if (cityName.equalsIgnoreCase(TOP_100_CITIES[i])) {
      lv_dropdown_set_selected(city_dropdown, i);
      Serial.printf("City dropdown set to: %s (index %d)\n", 
                    TOP_100_CITIES[i], (int)i);
      return;
    }
  }
  Serial.printf("City not found in dropdown: %s\n", cityName.c_str());
}

// ------------------------------------------------------------------
// Build dropdown options
// ------------------------------------------------------------------
static void settings_update_city_options() {
  if (!city_dropdown)
    return;
    
  std::string opts;
  for (size_t i = 0; i < TOP_100_COUNT; ++i) {
    opts += TOP_100_CITIES[i];
    opts += "\n";
  }
  
  lv_dropdown_clear_options(city_dropdown);
  lv_dropdown_set_options(city_dropdown, opts.c_str());
}

// ------------------------------------------------------------------
// Filter dropdown
// ------------------------------------------------------------------
static void filter_city_dropdown(const char *filter) {
  if (!city_dropdown)
    return;
  String f = filter ? filter : "";
  std::string out;
  for (size_t i = 0; i < TOP_100_COUNT; ++i) {
    String city = TOP_100_CITIES[i];
    if (f.isEmpty() || contains_folded(city, f)) {
      out += city.c_str();
      out += "\n";
    }
  }
  if (out.empty())
    out = "No match\n";
  lv_dropdown_clear_options(city_dropdown);
  lv_dropdown_set_options(city_dropdown, out.c_str());
}

// ------------------------------------------------------------------
// Check if station has actual data for parameter 1
// Returns true if HTTP 200 and data exists
// ------------------------------------------------------------------
static bool station_has_param1_data(const String &stationId) {
  // Check negative cache first
  auto neg_it = g_station_no_data_cache.find(stationId);
  if (neg_it != g_station_no_data_cache.end()) {
    Serial.printf("  Station %s known to have no data (cached)\n", stationId.c_str());
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000);
  HTTPClient http;

  String url = "https://opendata-download-metobs.smhi.se/api/version/1.0/parameter/1/station/";
  url += stationId;
  url += "/period/latest-months/data.json";

  Serial.printf("  Checking param 1 data: %s\n", url.c_str());

  if (!http.begin(client, url)) {
    Serial.println("  HTTP begin failed");
    return false;
  }
    
  int code = http.GET();
  http.end();
  
  Serial.printf("  HTTP response: %d\n", code);
  
  if (code != 200) {
    // Cache this station as having no data
    g_station_no_data_cache[stationId] = true;
    return false;
  }
  
  return true;
}

// ------------------------------------------------------------------
// Check if parameter is available for station (metadata check)
// ------------------------------------------------------------------
static bool check_param_available(const String &stationId, int paramCode) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000);
  HTTPClient http;

  String url =
      "https://opendata-download-metobs.smhi.se/api/version/1.0/parameter/";
  url += String(paramCode);
  url += "/station/";
  url += stationId;
  url += "/";

  if (!http.begin(client, url))
    return false;
  int code = http.GET();
  http.end();

  return (code >= 200 && code < 400);
}

// ------------------------------------------------------------------
// Update parameter dropdown from cached or fetched data
// ------------------------------------------------------------------
static void update_param_dropdown_from_indices() {
  if (param_dropdown) {
    if (g_available_param_indices.empty()) {
      lv_dropdown_clear_options(param_dropdown);
      lv_dropdown_set_options(param_dropdown, "No parameters available");
    } else {
      std::string opts;
      for (int idx : g_available_param_indices) {
        opts += PARAM_NAMES[idx];
        opts += "\n";
      }
      lv_dropdown_clear_options(param_dropdown);
      lv_dropdown_set_options(param_dropdown, opts.c_str());
      lv_dropdown_set_selected(param_dropdown, 0);
    }
  }
}

// ------------------------------------------------------------------
// Fetch available parameters for station and update dropdown
// Uses cache if available. Only call AFTER confirming param 1 works!
// ------------------------------------------------------------------
static void fetch_available_parameters(const String &stationId) {
  // Check cache first
  auto it = g_param_cache.find(stationId);
  if (it != g_param_cache.end()) {
    Serial.printf("Using cached parameters for station %s (%d params)\n",
                  stationId.c_str(), (int)it->second.size());
    g_available_param_indices = it->second;
    update_param_dropdown_from_indices();
    
    if (param_loading_label) {
      lv_obj_add_flag(param_loading_label, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  // Not in cache, fetch from API
  g_available_param_indices.clear();
  
  // Parameter 1 is already confirmed to work, add it first
  g_available_param_indices.push_back(0); // Index 0 = param code 1

  if (param_loading_label) {
    lv_label_set_text(param_loading_label, "Finding parameters...");
    lv_obj_clear_flag(param_loading_label, LV_OBJ_FLAG_HIDDEN);
    lv_timer_handler();
  }

  Serial.printf("Fetching available parameters for station %s...\n",
                stationId.c_str());

  // Priority parameters to check first (skip index 0, already added)
  int priority_params[] = {3, 5, 6}; // Wind Speed, Precipitation, Humidity
  int priority_count = sizeof(priority_params) / sizeof(priority_params[0]);

  int checked = 1; // Already checked param 1
  
  for (int i = 0; i < priority_count; i++) {
    int idx = priority_params[i];
    checked++;
    
    if (param_loading_label) {
      lv_label_set_text_fmt(param_loading_label, "Checking %d/%d...", 
                            checked, PARAM_COUNT);
      lv_timer_handler();
    }
    
    if (check_param_available(stationId, PARAM_CODES[idx])) {
      g_available_param_indices.push_back(idx);
      Serial.printf("  Parameter %d (%s) available\n", PARAM_CODES[idx],
                    PARAM_NAMES[idx]);
    }
  }

  // Check remaining parameters
  for (int idx = 1; idx < PARAM_COUNT; idx++) { // Start from 1, skip param 1 (index 0)
    // Skip if already checked in priority list
    bool already_checked = false;
    for (int i = 0; i < priority_count; i++) {
      if (priority_params[i] == idx) {
        already_checked = true;
        break;
      }
    }
    if (already_checked)
      continue;

    checked++;
    
    if (param_loading_label) {
      lv_label_set_text_fmt(param_loading_label, "Checking %d/%d...", 
                            checked, PARAM_COUNT);
      lv_timer_handler();
    }

    if (check_param_available(stationId, PARAM_CODES[idx])) {
      g_available_param_indices.push_back(idx);
      Serial.printf("  Parameter %d (%s) available\n", PARAM_CODES[idx],
                    PARAM_NAMES[idx]);
    }
  }

  std::sort(g_available_param_indices.begin(), g_available_param_indices.end());

  // Store in cache
  g_param_cache[stationId] = g_available_param_indices;
  Serial.printf("Cached %d parameters for station %s\n",
                (int)g_available_param_indices.size(), stationId.c_str());

  update_param_dropdown_from_indices();

  if (param_loading_label) {
    lv_obj_add_flag(param_loading_label, LV_OBJ_FLAG_HIDDEN);
  }

  Serial.printf("Found %d available parameters\n",
                (int)g_available_param_indices.size());
}

// ------------------------------------------------------------------
// Get actual parameter CODE from dropdown selection
// ------------------------------------------------------------------
static int get_actual_param_code(int dropdown_idx) {
  if (dropdown_idx < 0 ||
      dropdown_idx >= (int)g_available_param_indices.size()) {
    return 1;
  }
  return PARAM_CODES[g_available_param_indices[dropdown_idx]];
}

// ------------------------------------------------------------------
// Find dropdown index for a given param code
// ------------------------------------------------------------------
static int find_dropdown_idx_for_code(int param_code) {
  for (size_t i = 0; i < g_available_param_indices.size(); i++) {
    if (PARAM_CODES[g_available_param_indices[i]] == param_code) {
      return (int)i;
    }
  }
  return 0;
}

// ------------------------------------------------------------------
// Keyboard helpers
// ------------------------------------------------------------------
static void close_keyboard() {
  if (kb) {
    lv_obj_del(kb);
    kb = NULL;
  }
}

static void kb_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY)
    close_keyboard();
}

static void ta_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED && !kb) {
    kb = lv_keyboard_create(t5);
    lv_keyboard_set_textarea(kb, search_box);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_size(kb, lv_disp_get_hor_res(NULL),
                    lv_disp_get_ver_res(NULL) / 2);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);
  } else if (code == LV_EVENT_DEFOCUSED) {
    close_keyboard();
  } else if (code == LV_EVENT_VALUE_CHANGED) {
    const char *txt = lv_textarea_get_text(search_box);
    filter_city_dropdown(txt);
  }
}

// ------------------------------------------------------------------
// Find city candidates
// ------------------------------------------------------------------
static void find_city_candidates(const String &englishCity,
                                 std::vector<int> &out) {
  build_station_index_once();
  out.clear();
  auto terms = city_search_terms(englishCity);

  std::vector<String> folded_terms;
  folded_terms.reserve(terms.size());
  for (const auto &t : terms)
    folded_terms.push_back(fold_sv_ascii_lower(t));

  for (int i = 0; i < (int)gStations.size(); ++i) {
    const String &foldedName = g_station_names_folded[(size_t)i];
    for (const auto &ft : folded_terms) {
      if (foldedName.indexOf(ft) >= 0) {
        out.push_back(i);
        break;
      }
    }
  }

  std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
    const String &na = g_station_names_folded[(size_t)a];
    const String &nb = g_station_names_folded[(size_t)b];
    bool ast = false, bst = false;
    for (const auto &ft : folded_terms) {
      if (!ast && na.startsWith(ft))
        ast = true;
      if (!bst && nb.startsWith(ft))
        bst = true;
    }
    if (ast != bst)
      return ast;
    return na.length() < nb.length();
  });
}

// ------------------------------------------------------------------
// Try candidates until one works
// Flow:
// 1. Check cache first (instant if cached)
// 2. Check if param 1 has data (quick validation)
// 3. If param 1 works, fetch all other parameters
// 4. If param 1 fails, try next station
// ------------------------------------------------------------------
static int ensure_station_has_data_from_candidates(const std::vector<int> &cand,
                                                   const String &cityKey) {
  Serial.printf("Trying %d candidate stations for %s\n", 
                (int)cand.size(), cityKey.c_str());
  
  for (int idx : cand) {
    const String& stationId = gStations[(size_t)idx].id;
    const String& stationName = gStations[(size_t)idx].name;
    
    Serial.printf("Trying station: %s (ID %s)\n", stationName.c_str(), stationId.c_str());
    
    // 1. Check if we have cached params for this station
    auto cache_it = g_param_cache.find(stationId);
    if (cache_it != g_param_cache.end() && !cache_it->second.empty()) {
      Serial.printf("  Using cached data (%d params)\n", (int)cache_it->second.size());
      g_available_param_indices = cache_it->second;
      update_param_dropdown_from_indices();
      stationValidity[cityKey] = true;
      TodayForecast_OnStationSelected(idx);
      current_station_idx = idx;
      return idx;
    }
    
    // 2. Check if station is in negative cache (known to have no data)
    auto neg_it = g_station_no_data_cache.find(stationId);
    if (neg_it != g_station_no_data_cache.end()) {
      Serial.printf("  Skipping - known to have no data\n");
      continue;
    }
    
    // 3. Check if parameter 1 has actual data
    if (!station_has_param1_data(stationId)) {
      Serial.printf("  No param 1 data, trying next station\n");
      continue;
    }
    
    // 4. Param 1 works! Now fetch all available parameters
    Serial.printf("  Param 1 OK! Fetching all parameters...\n");
    fetch_available_parameters(stationId);

    if (!g_available_param_indices.empty()) {
      stationValidity[cityKey] = true;
      Serial.printf("SUCCESS: Using station %s (ID %s) with %d params\n",
                    stationName.c_str(), stationId.c_str(),
                    (int)g_available_param_indices.size());
      TodayForecast_OnStationSelected(idx);
      current_station_idx = idx;
      return idx;
    }
  }
  
  Serial.printf("FAILED: No working station found for %s\n", cityKey.c_str());
  return -1;
}

// ------------------------------------------------------------------
// City selection changed
// ------------------------------------------------------------------
static void city_selection_changed(lv_event_t *e) {
  (void)e;
  char buf[128];
  buf[0] = '\0';
  lv_dropdown_get_selected_str(city_dropdown, buf, sizeof(buf));
  String city(buf);
  if (city.isEmpty())
    return;

  Serial.printf("\n=== City selected: %s ===\n", buf);

  std::vector<int> cand;
  find_city_candidates(city, cand);
  if (cand.empty()) {
    Serial.printf("No station candidates found for %s\n", buf);
    return;
  }

  int ok_idx = ensure_station_has_data_from_candidates(cand, city);
  if (ok_idx >= 0) {
    int param_code = g_available_param_indices.empty()
                         ? 1
                         : PARAM_CODES[g_available_param_indices[0]];
    weather.update_weather_data(ok_idx, param_code, "latest-months");
    current_station_idx = ok_idx;
  } else {
    Serial.printf("No working station found for %s\n", buf);
    
    // Update UI to show no data
    if (param_dropdown) {
      lv_dropdown_clear_options(param_dropdown);
      lv_dropdown_set_options(param_dropdown, "No station available");
    }
  }
}

// ------------------------------------------------------------------
// Parameter selection changed
// ------------------------------------------------------------------
static void param_selection_changed(lv_event_t *e) {
  (void)e;
  if (current_station_idx < 0)
    return;

  int dropdown_idx = lv_dropdown_get_selected(param_dropdown);
  int actual_param_code = get_actual_param_code(dropdown_idx);

  if (dropdown_idx >= 0 && dropdown_idx < (int)g_available_param_indices.size()) {
    Serial.printf("Parameter changed: dropdown=%d, code=%d (%s)\n", dropdown_idx,
                  actual_param_code,
                  PARAM_NAMES[g_available_param_indices[dropdown_idx]]);
  }

  weather.update_weather_data(current_station_idx, actual_param_code,
                              "latest-months");
}

// ------------------------------------------------------------------
// Save/Reset Defaults
// ------------------------------------------------------------------
static void save_btn_event_cb(lv_event_t *e) {
  if (current_station_idx < 0 || current_station_idx >= (int)gStations.size()) {
    Serial.println("Cannot save: Invalid station index");
    return;
  }

  int dropdown_idx = lv_dropdown_get_selected(param_dropdown);
  int actual_param_code = get_actual_param_code(dropdown_idx);
  
  char city_buf[128];
  lv_dropdown_get_selected_str(city_dropdown, city_buf, sizeof(city_buf));

  Preferences prefs;
  if (prefs.begin("weather", false)) {
    prefs.putString("station_id", gStations[current_station_idx].id);
    prefs.putInt("param_code", actual_param_code);
    prefs.putString("city_name", city_buf);
    prefs.end();
    Serial.printf("Settings saved: station=%s, param_code=%d, city=%s\n",
                  gStations[current_station_idx].id.c_str(), actual_param_code, city_buf);

    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    lv_label_set_text(label, "Saved!");
  }
}

static void reset_btn_event_cb(lv_event_t *e) {
  Preferences prefs;
  String st_id = "";
  int p_code = 1;
  String city_name = "";

  if (prefs.begin("weather", true)) {
    st_id = prefs.getString("station_id", "");
    p_code = prefs.getInt("param_code", 1);
    city_name = prefs.getString("city_name", "");
    prefs.end();
    Serial.printf("Settings loaded: station=%s, param_code=%d, city=%s\n",
                  st_id.c_str(), p_code, city_name.c_str());
  }

  int new_idx = 0;
  if (!st_id.isEmpty()) {
    for (size_t i = 0; i < gStations.size(); ++i) {
      if (gStations[i].id == st_id) {
        new_idx = (int)i;
        break;
      }
    }
  }

  current_station_idx = new_idx;

  if (city_name.isEmpty() && new_idx >= 0 && new_idx < (int)gStations.size()) {
    city_name = find_city_name_for_station(gStations[new_idx].name);
  }

  settings_update_city_options();
  select_city_in_dropdown(city_name);

  if (new_idx >= 0 && new_idx < (int)gStations.size()) {
    fetch_available_parameters(gStations[new_idx].id);

    int dropdown_idx = find_dropdown_idx_for_code(p_code);

    if (param_dropdown)
      lv_dropdown_set_selected(param_dropdown, dropdown_idx);

    weather.update_weather_data(new_idx, p_code, "latest-months");
    TodayForecast_OnStationSelected(new_idx);
  }

  lv_obj_t *btn = lv_event_get_target(e);
  lv_obj_t *label = lv_obj_get_child(btn, 0);
  lv_label_set_text(label, "Reset!");
}

// ------------------------------------------------------------------
// Clear caches
// ------------------------------------------------------------------
static void clear_param_cache() {
  g_param_cache.clear();
  g_station_no_data_cache.clear();
  Serial.println("All caches cleared");
}

static void print_cache_stats() {
  Serial.printf("Parameter cache: %d stations\n", (int)g_param_cache.size());
  Serial.printf("No-data cache: %d stations\n", (int)g_station_no_data_cache.size());
}

// ------------------------------------------------------------------
// Create Settings tile
// ------------------------------------------------------------------
static void create_settings_tile() {
  search_box = lv_textarea_create(t5);
  lv_textarea_set_one_line(search_box, true);
  lv_textarea_set_placeholder_text(search_box, "Search city...");
  lv_obj_set_size(search_box, 220, 40);
  lv_obj_align(search_box, LV_ALIGN_TOP_MID, 0, 15);
  lv_obj_add_event_cb(search_box, ta_event_cb, LV_EVENT_ALL, NULL);

  city_dropdown = lv_dropdown_create(t5);
  lv_obj_align(city_dropdown, LV_ALIGN_TOP_MID, 0, 70);
  lv_dropdown_set_options(city_dropdown, "Loading cities...\n");

  param_dropdown = lv_dropdown_create(t5);
  lv_dropdown_set_options(param_dropdown, "Select city first...\n");
  lv_obj_align(param_dropdown, LV_ALIGN_TOP_MID, 0, 130);

  param_loading_label = lv_label_create(t5);
  lv_label_set_text(param_loading_label, "Checking parameters...");
  lv_obj_align(param_loading_label, LV_ALIGN_TOP_MID, 0, 175);
  lv_obj_set_style_text_color(param_loading_label, lv_color_hex(0x666666), 0);
  lv_obj_add_flag(param_loading_label, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_event_cb(city_dropdown, city_selection_changed,
                      LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(param_dropdown, param_selection_changed,
                      LV_EVENT_VALUE_CHANGED, NULL);

  settings_update_city_options();

  lv_obj_t *save_btn = lv_btn_create(t5);
  lv_obj_set_size(save_btn, 140, 60);
  lv_obj_align(save_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
  lv_obj_add_event_cb(save_btn, save_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *save_label = lv_label_create(save_btn);
  lv_label_set_text(save_label, "Set Default");
  lv_obj_center(save_label);

  lv_obj_t *reset_btn = lv_btn_create(t5);
  lv_obj_set_size(reset_btn, 140, 60);
  lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
  lv_obj_add_event_cb(reset_btn, reset_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *reset_label = lv_label_create(reset_btn);
  lv_label_set_text(reset_label, "Reset");
  lv_obj_center(reset_label);
}

// ------------------------------------------------------------------
// Sync UI state with loaded defaults
// ------------------------------------------------------------------
static void settings_sync_state(int station_idx, int param_code, const String &city_name) {
  current_station_idx = station_idx;

  settings_update_city_options();

  String cityToSelect = city_name;
  
  if (cityToSelect.isEmpty() && station_idx >= 0 && station_idx < (int)gStations.size()) {
    cityToSelect = find_city_name_for_station(gStations[station_idx].name);
    Serial.printf("Derived city name from station: %s\n", cityToSelect.c_str());
  }

  if (!cityToSelect.isEmpty()) {
    select_city_in_dropdown(cityToSelect);
  }

  if (station_idx >= 0 && station_idx < (int)gStations.size()) {
    fetch_available_parameters(gStations[station_idx].id);

    int dropdown_idx = find_dropdown_idx_for_code(param_code);

    if (param_dropdown)
      lv_dropdown_set_selected(param_dropdown, dropdown_idx);
  }
}