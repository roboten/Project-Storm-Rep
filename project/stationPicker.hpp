#pragma once
#include "stations.hpp" // generated full SMHI station list (id, name, lat, lon)
#include <Arduino.h>
#include <algorithm>
#include <math.h>
#include <vector>


// Global station list used across app (populated in loop(), defined in
// project.ino)
extern std::vector<StationInfo> gStations;

// ------------------------------------------------------------------
// Top 100 Swedish Cities by Population
// Used for city dropdown in settings page
// Uses Swedish names in ASCII-friendly format for compatibility
// (Göteborg -> Goteborg, Malmö -> Malmo, Västerås -> Vasteras)
// ------------------------------------------------------------------
static const char *TOP_100_CITIES[] = {
    "Stockholm",    "Goteborg",    "Malmo",          "Uppsala",
    "Vasteras",     "Orebro",      "Linkoping",      "Helsingborg",
    "Jonkoping",    "Norrkoping",  "Lund",           "Umea",
    "Gavle",        "Boras",       "Sodertalje",     "Eskilstuna",
    "Halmstad",     "Vaxjo",       "Karlstad",       "Sundsvall",
    "Lulea",        "Trollhattan", "Ostersund",      "Borlange",
    "Kristianstad", "Kalmar",      "Skovde",         "Karlskrona",
    "Uddevalla",    "Nykoping",    "Falun",          "Skelleftea",
    "Pitea",        "Varberg",     "Landskrona",     "Motala",
    "Norrtalje",    "Kungsbacka",  "Varnamo",        "Angelholm",
    "Eslov",        "Visby",       "Lerum",          "Alingsas",
    "Sandviken",    "Kungalv",     "Katrineholm",    "Hassleholm",
    "Vetlanda",     "Ystad",       "Enkoping",       "Hudiksvall",
    "Lidkoping",    "Mora",        "Kristinehamn",   "Trelleborg",
    "Harnosand",    "Nassjo",      "Saffle",         "Mariestad",
    "Nykvarn",      "Huskvarna",   "Vanersborg",     "Vallentuna",
    "Sollentuna",   "Taby",        "Solna",          "Sundbyberg",
    "Danderyd",     "Jarfalla",    "Upplands Vasby", "Haninge",
    "Tyreso",       "Nynashamn",   "Tumba",          "Nacka",
    "Lidingo",      "Sigtuna",     "Akalla",         "Kista",
    "Bromma",       "Hagersten",   "Skogas",         "Boden",
    "Kiruna",       "Gislaved",    "Hedemora",       "Arvika",
    "Oskarshamn",   "Bastad",      "Avesta",         "Koping",
    "Staffanstorp", "Hoganas",     "Partille",       "Habo",
    "Eda",          "Sodertalje",  "Upplands-Bro",   "Sigtuna Kommun"};
static const size_t TOP_100_COUNT =
    sizeof(TOP_100_CITIES) / sizeof(TOP_100_CITIES[0]);

// ------------------------------------------------------------------
// Haversine Distance Calculation
// Calculate great-circle distance between two lat/lon points in kilometers
// Used for finding nearest weather stations to a given location
// ------------------------------------------------------------------
static float distance_km(float lat1, float lon1, float lat2, float lon2) {
  const float DEG2RAD = 0.01745329252f; // Pi/180
  float dLat = (lat2 - lat1) * DEG2RAD;
  float dLon = (lon2 - lon1) * DEG2RAD;
  lat1 *= DEG2RAD;
  lat2 *= DEG2RAD;
  // Haversine formula
  float a = sinf(dLat / 2) * sinf(dLat / 2) +
            cosf(lat1) * cosf(lat2) * sinf(dLon / 2) * sinf(dLon / 2);
  return 6371.0f * 2.0f * asinf(sqrtf(a)); // Earth radius = 6371 km
}

// ------------------------------------------------------------------
// Station List Loader
// Loads all stations from stations.hpp into global gStations vector
// Original parameters (radius, max_stations) are unused - loads all stations
// for comprehensive city/station search and validation
// ------------------------------------------------------------------
static bool fetch_and_select_top_stations(float /*unused*/, int /*unused*/) {
  gStations.clear();
  gStations.reserve(STATION_COUNT);

  // Copy ALL stations for fuzzy matching and validation
  for (size_t i = 0; i < STATION_COUNT; ++i) {
    gStations.push_back(STATIONS[i]);
  }

  Serial.printf("Loaded %u stations from stations.hpp\n",
                (unsigned)STATION_COUNT);
  Serial.printf("Top 100 cities array loaded for dropdown\n");
  return !gStations.empty();
}