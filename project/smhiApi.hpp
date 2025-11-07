#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <WiFiClientSecure.h>
#include <vector>

const char* param_codes[] = {"1","2","3"};  // 1=Temp, 2=Nederbörd, 3=Vind
struct DataPoint {
    String date;
    float temp;
};

struct Station {
    const char* name;
    const char* id;
};
Station stations[] = {
    {"Karlskrona", "65020"},
    {"Stockholm",  "98200"},
    {"Göteborg",   "97400"}
};

static std::vector<DataPoint> weatherData;
class SMHI_API {
public:
  // Constructor
SMHI_API(const char* apiUrl);
bool CONNECT();
  // Fetch weather data from SMHI API
void update_weather_data(int city_idx, int param_idx, String period);
  //Parse the weather data
void parseWeatherData(const String& jsonData);
private:
  //URL to SMHI API
const char* apiUrl;
  //Vector for weather data
};

SMHI_API::SMHI_API(const char* apiUrl) : apiUrl(apiUrl) {}

void SMHI_API::update_weather_data(int city_idx,int param_idx, String period){
    String url=apiUrl;
    url+=param_codes[param_idx];
    url+="/station/";
    url+=stations[city_idx].id;
    url+="/period/"+ period +"/data.json";

    Serial.printf("Fetching data: %s\n",url.c_str());

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    if(https.begin(client,url)){
        int httpCode = https.GET();
        if(httpCode==200){
            String payload = https.getString();
            parseWeatherData(payload);
            Serial.println("Weather data fetched OK.");
        } else {
            Serial.printf("HTTP GET failed, code: %d\n",httpCode);
        }
        https.end();
    } else {
        Serial.println("Failed to begin HTTPS request");
    }
}
void SMHI_API::parseWeatherData(const String& jsonData){
    DynamicJsonDocument doc(60000);
    DeserializationError err = deserializeJson(doc,jsonData);
    if(err){
        Serial.print("JSON parse error: ");
        Serial.println(err.c_str());
        return;
    }

    weatherData.clear();

    if(doc.containsKey("value")){
        JsonArray arr = doc["value"];
        for(JsonObject v: arr){
            DataPoint dp;
            dp.date = v["date"].as<String>().substring(0,10);
            dp.temp = v["value"].as<float>();
            weatherData.push_back(dp);
        }
    } else if(doc.containsKey("timeSeries")){
        JsonArray ts = doc["timeSeries"];
        for(JsonObject rec : ts){
            String t = rec["validTime"].as<String>();
            JsonArray pars = rec["parameters"];
            for(JsonObject p : pars){
                if(String(p["name"].as<const char*>())=="t"){
                    float val = p["values"][0].as<float>();
                    DataPoint dp;
                    dp.date = t.substring(0,10);
                    dp.temp = val;
                    weatherData.push_back(dp);
                    break;
                }
            }
        }
    } else {
        Serial.println("No known JSON key for data array!");
        return;
    }

    Serial.printf("Parsed %d datapoints.\n",weatherData.size());
}
