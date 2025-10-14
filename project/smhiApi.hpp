#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>

class SMHI_API {
public:
  // Constructor
  SMHI_API(const char* apiUrl);
  bool CONNECT();
  // Fetch weather data from SMHI API
  DynamicJsonDocument fetchWeatherData();

private:
  const char* apiUrl;
};

SMHI_API::SMHI_API(const char* apiUrl) : apiUrl(apiUrl) {}

DynamicJsonDocument SMHI_API::fetchWeatherData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return false;
  }

  HTTPClient http;
  http.begin(apiUrl);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP request failed with error: %d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.printf("JSON parsing failed: %s\n", error.c_str());
    return;
  }

  
  return doc;
}
  // This is how to Extract data from JSON
  //data.temperature = doc["timeSeries"][0]["parameters"][10]["values"][0];
  //data.weatherSymbol = doc["timeSeries"][0]["parameters"][18]["values"][0];
  //data.precipitation = doc["timeSeries"][0]["parameters"][6]["values"][0];
  //data.windSpeed = doc["timeSeries"][0]["parameters"][3]["values"][0];
  
  //Example
  //struct WeatherData {
    //float temperature;
    //int weatherSymbol;
    //float precipitation;
    //float windSpeed;
  //};
  