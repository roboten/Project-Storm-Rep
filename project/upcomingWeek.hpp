#ifndef UPCOMING_WEEK_HPP
#define UPCOMING_WEEK_HPP

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <time.h>

// Structure to hold daily weather data from SMHI
struct DailyWeather {
    String date;          // Date in YYYY-MM-DD format
    float tempMin;        // Minimum temperature in °C
    float tempMax;        // Maximum temperature in °C
    float humidity;       // Relative humidity in %
    float precipitation;  // Precipitation in mm
    int symbolCode;       // SMHI weather symbol code (as int)
};

// Structure to map station IDs to coordinates
struct StationCoordinates {
    const char* id;
    const char* lat;
    const char* lon;
    const char* name;
};

// Array of station coordinates
static const StationCoordinates stationCoords[] = {
    {"65020", "56.1616", "15.5860", "Karlskrona"},
    {"98200", "59.3293", "18.0686", "Stockholm"},
    {"97400", "57.7089", "11.9746", "Göteborg"}
};

// Class to handle weather forecast using SMHI API
class UpcomingWeek {
public:
    /**
     * @brief Constructor for UpcomingWeek class using coordinates
     * @param lat Latitude as a string (e.g., "59.3293")
     * @param lon Longitude as a string (e.g., "18.0686")
     */
    UpcomingWeek(const char* lat, const char* lon);

    /**
     * @brief Updates the location for the forecast using coordinates
     * @param lat New latitude as a string
     * @param lon New longitude as a string
     */
    void updateLocation(const char* lat, const char* lon);

    /**
     * @brief Fetches the 7-day weather forecast from SMHI
     * @return true if successful, false otherwise
     */
    bool fetchForecast();

    /**
     * @brief Prints the forecast to serial (for debugging)
     */
    void printForecast();

    /**
     * @brief Returns the forecast data for the upcoming 7 days
     * @return const reference to the forecast vector
     */
    const std::vector<DailyWeather>& getForecast() const;

private:
    String latitude;     // Current latitude
    String longitude;    // Current longitude
    String stationId;    // Current station ID (if available)
    String stationName;  // Current station name (if available)
    std::vector<DailyWeather> forecast;  // Stores the forecast data

    /**
     * @brief Connects to SMHI API and fetches data
     * @return true if successful, false otherwise
     */
    bool connectToSMHI();

    /**
     * @brief Parses the SMHI API JSON response for the upcoming 7 days
     * @param response JSON response from SMHI
     * @return true if parsing succeeded, false otherwise
     */
    bool parseSMHIResponse(const String& response);

    /**
     * @brief Filters the forecast to only include the upcoming 7 days
     */
    void filterUpcomingWeek();

    /**
     * @brief Finds station ID for given coordinates
     * @return true if station found, false otherwise
     */
    bool findStationForCoordinates();
};

// Implementation of UpcomingWeek class methods

/**
 * @brief Constructor for UpcomingWeek class
 * @param lat Latitude as a string
 * @param lon Longitude as a string
 */
UpcomingWeek::UpcomingWeek(const char* lat, const char* lon)
    : latitude(lat), longitude(lon) {
    findStationForCoordinates();
}

/**
 * @brief Updates the location for the forecast
 * @param lat New latitude as a string
 * @param lon New longitude as a string
 */
void UpcomingWeek::updateLocation(const char* lat, const char* lon) {
    latitude = lat;
    longitude = lon;
    findStationForCoordinates();
    Serial.printf("Location updated to Lat: %s, Lon: %s\n", latitude.c_str(), longitude.c_str());
    if (!stationName.isEmpty()) {
        Serial.printf("Station: %s (ID: %s)\n", stationName.c_str(), stationId.c_str());
    }
}

/**
 * @brief Finds station ID for the current coordinates
 * @return true if station found, false otherwise
 */
bool UpcomingWeek::findStationForCoordinates() {
    // Try to find a matching station for these coordinates
    for (size_t i = 0; i < sizeof(stationCoords) / sizeof(stationCoords[0]); i++) {
        if (latitude == stationCoords[i].lat && longitude == stationCoords[i].lon) {
            stationId = stationCoords[i].id;
            stationName = stationCoords[i].name;
            return true;
        }
    }

    // If no exact match found, just use the coordinates directly
    stationId = "";
    stationName = "";
    return false;
}

/**
 * @brief Connects to SMHI API and fetches data
 * @return true if successful, false otherwise
 */
bool UpcomingWeek::connectToSMHI() {
    if (latitude.isEmpty() || longitude.isEmpty()) {
        Serial.println("No coordinates set");
        return false;
    }

    String url = "https://opendata-download-metfcst.smhi.se/api/category/pmp3g/version/2/geotype/point/lon/";
    url += longitude;
    url += "/lat/";
    url += latitude;
    url += "/data.json";

    Serial.print("Fetching data from URL: ");
    Serial.println(url);

    WiFiClientSecure client;
    client.setInsecure(); // Bypass SSL certificate verification for testing

    HTTPClient https;
    if (!https.begin(client, url)) {
        Serial.println("Failed to initialize HTTP client");
        return false;
    }

    Serial.println("Making HTTP GET request...");
    int httpCode = https.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("HTTP GET request failed, error: %s\n", https.errorToString(httpCode).c_str());
        https.end();
        return false;
    }

    String payload = https.getString();
    https.end();

    Serial.println("HTTP GET request successful");
    Serial.print("Payload length: ");
    Serial.println(payload.length());

    return parseSMHIResponse(payload);
}

/**
 * @brief Parses the JSON response from the SMHI API
 * @param response JSON response from SMHI
 * @return true if parsing succeeded, false otherwise
 */
bool UpcomingWeek::parseSMHIResponse(const String& response) {
    Serial.println("Parsing SMHI response...");

    DynamicJsonDocument doc(32768); // Increased buffer size for larger JSON responses
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        return false;
    }

    if (!doc.containsKey("timeSeries")) {
        Serial.println("No 'timeSeries' key in JSON.");
        return false;
    }

    JsonArray timeSeries = doc["timeSeries"];
    Serial.printf("Found %d time series entries\n", timeSeries.size());

    forecast.clear();

    for (JsonObject timeSeriesObj : timeSeries) {
        if (!timeSeriesObj.containsKey("validTime") || !timeSeriesObj.containsKey("parameters")) {
            Serial.println("Missing validTime or parameters in time series entry.");
            continue;
        }

        String date = timeSeriesObj["validTime"].as<String>().substring(0, 10);
        JsonObject parametersObj = timeSeriesObj["parameters"];

        if (!parametersObj.containsKey("11") || !parametersObj.containsKey("12") ||
            !parametersObj.containsKey("4") || !parametersObj.containsKey("18") ||
            !parametersObj.containsKey("19")) {
            Serial.println("Missing required parameters in JSON.");
            continue;
        }

        DailyWeather dw;
        dw.date = date;
        dw.tempMin = parametersObj["11"]["values"][0];
        dw.tempMax = parametersObj["12"]["values"][0];
        dw.humidity = parametersObj["4"]["values"][0];
        dw.precipitation = parametersObj["18"]["values"][0];
        dw.symbolCode = parametersObj["19"]["values"][0];

        forecast.push_back(dw);
    }

    if (forecast.empty()) {
        Serial.println("No forecast data parsed");
        return false;
    }

    filterUpcomingWeek();
    return true;
}

/**
 * @brief Filters the forecast to only include the upcoming 7 days
 */
void UpcomingWeek::filterUpcomingWeek() {
    // Get current date and time
    time_t now;
    time(&now);
    struct tm *timeinfo = localtime(&now);
    timeinfo->tm_hour = 0;
    timeinfo->tm_min = 0;
    timeinfo->tm_sec = 0;
    timeinfo->tm_mday += 1; // Start from tomorrow
    mktime(timeinfo);

    // Format the date for tomorrow
    char tomorrow[11];
    strftime(tomorrow, sizeof(tomorrow), "%Y-%m-%d", timeinfo);

    // Vector to store the upcoming week's forecast
    std::vector<DailyWeather> upcomingWeek;
    bool foundTomorrow = false;

    // Filter forecast to only include the upcoming 7 days
    for (const auto& day : forecast) {
        if (day.date == String(tomorrow) || foundTomorrow) {
            foundTomorrow = true;
            upcomingWeek.push_back(day);
            if (upcomingWeek.size() >= 7) {
                break;
            }
        }
    }

    // Update the forecast with the filtered data
    forecast = upcomingWeek;
}

/**
 * @brief Fetches the 7-day weather forecast from SMHI
 * @return true if successful, false otherwise
 */
bool UpcomingWeek::fetchForecast() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        return false;
    }

    return connectToSMHI();
}

/**
 * @brief Prints the forecast to serial (for debugging)
 */
void UpcomingWeek::printForecast() {
    if (forecast.empty()) {
        Serial.println("No forecast data to print");
        return;
    }

    String location = stationName.isEmpty() ? String(latitude + ", " + longitude) : stationName;
    Serial.println("Forecast for: " + location);

    for (const auto& day : forecast) {
        Serial.println("Date: " + day.date);
        Serial.printf("Temp: %.1f°C - %.1f°C\n", day.tempMin, day.tempMax);
        Serial.printf("Humidity: %.1f%%\n", day.humidity);
        Serial.printf("Precipitation: %.1fmm\n", day.precipitation);
        Serial.printf("Symbol: %d\n", day.symbolCode);
        Serial.println();
    }
}

/**
 * @brief Returns the forecast data for the upcoming 7 days
 * @return const reference to the forecast vector
 */
const std::vector<DailyWeather>& UpcomingWeek::getForecast() const {
    return forecast;
}

#endif // UPCOMING_WEEK_HPP
