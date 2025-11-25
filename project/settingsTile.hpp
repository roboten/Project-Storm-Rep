#pragma once
#include "smhiApi.hpp"
#include "stationPicker.hpp" // TOP_100_CITIES + gStations
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <lvgl.h>
#include <map>
#include <string>
#include <vector>

// Externals from main sketch
extern lv_obj_t *t5;
extern SMHI_API weather;

extern void TodayForecast_OnStationSelected(int station_idx);

// UI state
static lv_obj_t *kb = NULL;
static lv_obj_t *search_box = NULL;
static lv_obj_t *city_dropdown = NULL;
static lv_obj_t *param_dropdown = NULL;

// Track currently selected station index for saving
static int current_station_idx = -1;

// Cache: city name -> validated (has data)
static std::map<String, bool> stationValidity;

// A folded search index of station names (built on first use to speed up
// contains check)
static bool g_index_built = false;
static std::vector<String> g_station_names_folded;

// ------------------------------------------------------------------
// UTF-8 diacritic folding to ASCII lower for robust matching
// å/ä/Å/Ä -> a, ö/Ö -> o, é/è/É/È -> e, ø/Ø -> o, æ/Æ -> ae
// ------------------------------------------------------------------
static String fold_sv_ascii_lower(const String &s) {
  String out;
  out.reserve(s.length());
  for (unsigned i = 0; i < s.length(); ++i) {
    unsigned char c = (unsigned char)s[i];
    if (c == 0xC3 && i + 1 < s.length()) {
      unsigned char d = (unsigned char)s[i + 1];
      if (d == 0xA5 || d == 0x85) {
        out += 'a';
        ++i;
        continue;
      } // å Å
      if (d == 0xA4 || d == 0x84) {
        out += 'a';
        ++i;
        continue;
      } // ä Ä
      if (d == 0xB6 || d == 0x96) {
        out += 'o';
        ++i;
        continue;
      } // ö Ö
      if (d == 0xA9 || d == 0x89) {
        out += 'e';
        ++i;
        continue;
      } // é É
      if (d == 0xA8 || d == 0x88) {
        out += 'e';
        ++i;
        continue;
      } // è È
      if (d == 0xB8 || d == 0x98) {
        out += 'o';
        ++i;
        continue;
      } // ø Ø
      if (d == 0xA6 || d == 0x86) {
        out += "ae";
        ++i;
        continue;
      } // æ Æ
    }
    out += (char)tolower(c);
  }
  return out;
}

static bool contains_folded(const String &hay, const String &needle) {
  return fold_sv_ascii_lower(hay).indexOf(fold_sv_ascii_lower(needle)) >= 0;
}

// ------------------------------------------------------------------
// Alias map: English city name -> list of Swedish search terms
// You can extend this as needed.
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
    {"varmdo", {"Varmdo", "Värmdö"}}};

// Return list of terms to search for this city (English name). Includes the
// city itself.
static std::vector<String> city_search_terms(const String &englishCity) {
  std::vector<String> terms;
  String key = fold_sv_ascii_lower(englishCity);
  auto it = g_city_alias.find(key);
  if (it != g_city_alias.end()) {
    for (const auto &t : it->second)
      terms.push_back(t);
  }
  // Always include the original city text as a term as well.
  terms.push_back(englishCity);
  return terms;
}

// ------------------------------------------------------------------
// Build folded station name index once
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
// Build dropdown options from TOP_100_CITIES (English ASCII)
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
// Filter dropdown case-insensitive (folded)
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
    // RESIZE: Keyboard takes 1/2 screen height now
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
// Lightweight endpoint existence check for station ID
// ------------------------------------------------------------------
static bool station_endpoint_exists(const String &stationId) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url =
      "https://opendata-download-metobs.smhi.se/api/version/1.0/parameter/1/"
      "station/";
  url += stationId;
  url += "/";

  if (!http.begin(client, url))
    return false;
  int code = http.GET();
  http.end();

  if (code > 0) {
    Serial.printf("Station %s endpoint check => HTTP %d\n", stationId.c_str(),
                  code);
    return (code >= 200 && code < 400);
  }
  return false;
}

// ------------------------------------------------------------------
// Find candidate stations by alias terms (English->Swedish) using folded index
// Prefer starts-with and shorter names first
// ------------------------------------------------------------------
static void find_city_candidates(const String &englishCity,
                                 std::vector<int> &out) {
  build_station_index_once();

  out.clear();
  auto terms = city_search_terms(englishCity);

  // Convert each term to folded lower once
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

  // Sort by: starts-with any alias first, then shorter name
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
// Try candidates until one returns real data
// ------------------------------------------------------------------
static int ensure_station_has_data_from_candidates(const std::vector<int> &cand,
                                                   int param_idx,
                                                   const String &cityKey) {
  for (int idx : cand) {
    if (!station_endpoint_exists(gStations[(size_t)idx].id))
      continue;
    if (weather.update_weather_data(idx, param_idx, "latest-months")) {
      stationValidity[cityKey] = true;
      Serial.printf("Using station %s (ID %s)\n",
                    gStations[(size_t)idx].name.c_str(),
                    gStations[(size_t)idx].id.c_str());

      TodayForecast_OnStationSelected(idx);
      current_station_idx = idx;
      return idx;
    }
  }
  return -1;
}

// ------------------------------------------------------------------
// Selection changed: try best station for the chosen city
// ------------------------------------------------------------------
static void selection_changed(lv_event_t *e) {
  (void)e;
  char buf[128];
  buf[0] = '\0';
  lv_dropdown_get_selected_str(city_dropdown, buf, sizeof(buf));
  String city(buf);
  if (city.isEmpty())
    return;

  int param_idx = lv_dropdown_get_selected(param_dropdown);

  if (stationValidity[city]) {
    std::vector<int> cand;
    find_city_candidates(city, cand);
    if (!cand.empty()) {
      weather.update_weather_data(cand[0], param_idx, "latest-months");
    }
    return;
  }

  std::vector<int> cand;
  find_city_candidates(city, cand);
  if (cand.empty()) {
    Serial.printf("City %s not matched to station names.\n", buf);
    return;
  }

  int ok_idx = ensure_station_has_data_from_candidates(cand, param_idx, city);
  if (ok_idx >= 0) {
    weather.update_weather_data(ok_idx, param_idx,
                                "latest-months"); // final refresh
    current_station_idx = ok_idx;
    return;
  }

  Serial.printf("No working station found for %s among %u candidates\n", buf,
                (unsigned)cand.size());
}

// ------------------------------------------------------------------
// Save/Reset Defaults
// ------------------------------------------------------------------
static void save_btn_event_cb(lv_event_t *e) {
  if (current_station_idx < 0 || current_station_idx >= (int)gStations.size()) {
    Serial.println("Cannot save: Invalid station index");
    return;
  }

  Preferences prefs;
  if (prefs.begin("weather", false)) {
    prefs.putString("station_id", gStations[current_station_idx].id);
    prefs.putInt("param_idx", lv_dropdown_get_selected(param_dropdown));
    prefs.end();
    Serial.println("Settings saved to Preferences.");

    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    lv_label_set_text(label, "Saved!");
  } else {
    Serial.println("Failed to open Preferences for saving.");
  }
}

static void reset_btn_event_cb(lv_event_t *e) {
  Preferences prefs;
  String st_id = "";
  int p_idx = 0;

  if (prefs.begin("weather", true)) {
    st_id = prefs.getString("station_id", "");
    p_idx = prefs.getInt("param_idx", 0);
    prefs.end();
    Serial.println("Settings loaded from Preferences.");
  } else {
    Serial.println("Failed to open Preferences for reading.");
  }

  // Find station index
  int new_idx = 0; // Default to 0 if not found
  if (!st_id.isEmpty()) {
    for (size_t i = 0; i < gStations.size(); ++i) {
      if (gStations[i].id == st_id) {
        new_idx = (int)i;
        break;
      }
    }
  }

  // Apply settings
  current_station_idx = new_idx;
  if (param_dropdown)
    lv_dropdown_set_selected(param_dropdown, p_idx);

  // Update weather data
  weather.update_weather_data(new_idx, p_idx, "latest-months");
  TodayForecast_OnStationSelected(new_idx);

  // Try to update city dropdown text if possible (optional, best effort)
  if (city_dropdown && new_idx >= 0 && new_idx < (int)gStations.size()) {
    // We can't easily select by text if it's not in the filtered list,
    // but we can try to set the text manually to show the user what's selected.
    // Note: This might be overwritten if the user types in the search box.
    // For now, just updating the data is the critical part.
  }

  lv_obj_t *btn = lv_event_get_target(e);
  lv_obj_t *label = lv_obj_get_child(btn, 0);
  lv_label_set_text(label, "Reset!");
}

// ------------------------------------------------------------------
// Create Settings tile (dropdown + search + params)
// ------------------------------------------------------------------
static void create_settings_tile() {
  // Search box
  search_box = lv_textarea_create(t5);
  lv_textarea_set_one_line(search_box, true);
  lv_textarea_set_placeholder_text(search_box, "Search city (English OK)...");
  lv_obj_set_size(search_box, 220, 40);
  lv_obj_align(search_box, LV_ALIGN_TOP_MID, 0, 15);
  lv_obj_add_event_cb(search_box, ta_event_cb, LV_EVENT_ALL, NULL);

  // City dropdown
  city_dropdown = lv_dropdown_create(t5);
  lv_obj_align(city_dropdown, LV_ALIGN_TOP_MID, 0, 70);
  lv_dropdown_set_options(city_dropdown, "Loading cities...\n");

  // Parameter dropdown
  param_dropdown = lv_dropdown_create(t5);
  lv_dropdown_set_options_static(param_dropdown,
                                 "Temperature\nPrecipitation\nWind");
  lv_obj_align(param_dropdown, LV_ALIGN_TOP_MID, 0, 130);

  // Event handlers
  lv_obj_add_event_cb(city_dropdown, selection_changed, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_add_event_cb(param_dropdown, selection_changed, LV_EVENT_VALUE_CHANGED,
                      NULL);

  // Build initial list
  settings_update_city_options();

  // Buttons
  lv_obj_t *save_btn = lv_btn_create(t5);
  // RESIZE: Bigger button
  lv_obj_set_size(save_btn, 140, 60);
  lv_obj_align(save_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
  lv_obj_add_event_cb(save_btn, save_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *save_label = lv_label_create(save_btn);
  lv_label_set_text(save_label, "Set Default");
  lv_obj_center(save_label);

  lv_obj_t *reset_btn = lv_btn_create(t5);
  // RESIZE: Bigger button
  lv_obj_set_size(reset_btn, 140, 60);
  lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
  lv_obj_add_event_cb(reset_btn, reset_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *reset_label = lv_label_create(reset_btn);
  lv_label_set_text(reset_label, "Reset");
  lv_obj_center(reset_label);
}

// ------------------------------------------------------------------
// Sync UI state with loaded defaults (called from main)
// ------------------------------------------------------------------
static void settings_sync_state(int station_idx, int param_idx) {
  current_station_idx = station_idx;
  if (param_dropdown)
    lv_dropdown_set_selected(param_dropdown, param_idx);
}