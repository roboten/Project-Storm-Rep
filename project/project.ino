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

// --------------------------------------------------------------------
// --- Konstanter och globala variabler ---
// --------------------------------------------------------------------
static const char* WIFI_SSID = "Nothing";
static const char* WIFI_PASSWORD = "hacM3Plz";

struct DataPoint {
    String date;
    float temp;
};
static std::vector<DataPoint> weatherData;

struct Station {
    const char* name;
    const char* id;
};
Station stations[] = {
    {"Karlskrona", "65020"},
    {"Stockholm",  "98200"},
    {"Göteborg",   "97400"}
};
const char* param_codes[] = {"1","2","3"};  // 1=Temp, 2=Nederbörd, 3=Vind

LilyGo_Class amoled;

// --- UI-objekt ---
static lv_obj_t* tileview;
static lv_obj_t* t1;
static lv_obj_t* t2;
static lv_obj_t* t3;
static lv_obj_t* t4;
static lv_obj_t* chart;
static lv_chart_series_t* series;
static lv_obj_t* slider;
static lv_obj_t* splash_screen;
static lv_obj_t* city_dropdown;
static lv_obj_t* param_dropdown;

static bool wifi_connected = false;
static bool main_screen_loaded = false;
static bool splash_deleted = false;
static bool initial_data_fetched = false;  // Ny flagga för automatisk hämtning

// --------------------------------------------------------------------
// --- Framåtdeklarationer ---
// --------------------------------------------------------------------
static void switch_to_main(lv_timer_t* timer);
static void connect_wifi_non_blocking();
static void create_ui();
void update_chart_from_slider(lv_event_t* e);
void setup_weather_screen();
void parseWeatherData(const String& jsonData);
void create_settings_tile();
void update_weather_data(int city_idx, int param_idx);

// --------------------------------------------------------------------
// --- Timer callback: byt från splash till huvudskärm ---
// --------------------------------------------------------------------
static void switch_to_main(lv_timer_t* timer) {
    LV_UNUSED(timer);
    main_screen_loaded = true;
    lv_scr_load_anim(tileview, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
}

// --------------------------------------------------------------------
// --- WiFi (icke-blockerande) ---
// --------------------------------------------------------------------
static void connect_wifi_non_blocking() {
    static bool wifi_started = false;
    static uint32_t start_time = 0;
    if(!wifi_started){
        Serial.printf("Connecting to WiFi SSID: %s\n", WIFI_SSID);
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        wifi_started = true;
        start_time = millis();
    }
    if(WiFi.status() == WL_CONNECTED && !wifi_connected){
        Serial.println("WiFi connected.");
        wifi_connected = true;
    } else if(WiFi.status() != WL_CONNECTED && millis() - start_time > 30000){
        Serial.println("WiFi could not connect (timeout), retrying...");
        WiFi.disconnect(true);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        start_time = millis();
    }
}

// --------------------------------------------------------------------
// --- UI: skapa tiles ---
// --------------------------------------------------------------------
static void create_ui(){
    // --- Splash ---
    splash_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(splash_screen, lv_color_white(), 0);
    lv_obj_t* splash_label = lv_label_create(splash_screen);
    lv_label_set_text(splash_label, "Group 1\nVersion 0.1");
    lv_obj_set_style_text_font(splash_label, &lv_font_montserrat_28, 0);
    lv_obj_center(splash_label);
    lv_scr_load(splash_screen);

    // --- Tileview ---
    tileview = lv_tileview_create(NULL);

    // Tile 1: Start/Info
    t1 = lv_tileview_add_tile(tileview, 0,0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(t1, lv_color_white(),0);
    lv_obj_t* t1_label = lv_label_create(t1);
    lv_label_set_text(t1_label,"Tile 1");
    lv_obj_set_style_text_color(t1_label, lv_color_black(),0);
    lv_obj_center(t1_label);

    // Tile 2: Huvudskärm
    t2 = lv_tileview_add_tile(tileview,1,0,LV_DIR_HOR);
    lv_obj_set_style_bg_color(t2, lv_color_white(),0);
    lv_obj_t* t2_label = lv_label_create(t2);
    lv_label_set_text(t2_label,"Main Screen");
    lv_obj_set_style_text_color(t2_label, lv_color_black(),0);
    lv_obj_center(t2_label);

    // Tile 3: Historikgraf
    t3 = lv_tileview_add_tile(tileview,2,0,LV_DIR_HOR);
    lv_obj_set_style_bg_color(t3, lv_color_white(),0);
    chart = lv_chart_create(t3);
    lv_obj_set_size(chart, lv_disp_get_hor_res(NULL)-20, lv_disp_get_ver_res(NULL)-80);
    lv_obj_align(chart, LV_ALIGN_TOP_MID,0,10);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    slider = lv_slider_create(t3);
    lv_obj_set_width(slider, lv_disp_get_hor_res(NULL)-40);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID,0,-10);
    lv_slider_set_range(slider,0,100);
    setup_weather_screen();

    // Tile 4: Inställningar
    t4 = lv_tileview_add_tile(tileview,3,0,LV_DIR_HOR);
    lv_obj_set_style_bg_color(t4, lv_color_white(),0);
    create_settings_tile();

    // Sätt default-val i dropdowns
    lv_dropdown_set_selected(city_dropdown,0);  // Karlskrona
    lv_dropdown_set_selected(param_dropdown,0); // Temperatur

    // Timer splash → main
    lv_timer_t* boot_timer = lv_timer_create(switch_to_main, 3000,NULL);
    lv_timer_set_repeat_count(boot_timer,1);
}

// --------------------------------------------------------------------
// --- Inställningstile ---
// --------------------------------------------------------------------
void create_settings_tile(){
    city_dropdown = lv_dropdown_create(t4);
    lv_dropdown_set_options_static(city_dropdown, "Karlskrona\nStockholm\nGöteborg");
    lv_obj_align(city_dropdown, LV_ALIGN_TOP_MID,0,20);

    param_dropdown = lv_dropdown_create(t4);
    lv_dropdown_set_options_static(param_dropdown, "Temperature\nPrecipitation\nWind");
    lv_obj_align(param_dropdown, LV_ALIGN_TOP_MID,0,70);

    auto cb = [](lv_event_t* e){
        int city_idx = lv_dropdown_get_selected(city_dropdown);
        int param_idx = lv_dropdown_get_selected(param_dropdown);
        update_weather_data(city_idx,param_idx);
    };
    lv_obj_add_event_cb(city_dropdown,cb,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_event_cb(param_dropdown,cb,LV_EVENT_VALUE_CHANGED,NULL);
}

// --------------------------------------------------------------------
// --- Uppdatera graf utifrån slider ---
// --------------------------------------------------------------------
void update_chart_from_slider(lv_event_t* e){
    if(weatherData.empty()) return;

    int sliderValue = lv_slider_get_value(slider);
    int total = weatherData.size();
    int max_points = lv_disp_get_hor_res(NULL)/10;
    int windowSize = min(max_points,total);
    if(windowSize==0) return;

    int start = map(sliderValue,0,100,0,max(0,total-windowSize));

    float minVal=999,maxVal=-999;
    for(auto &dp : weatherData){
        if(dp.temp<minVal) minVal=dp.temp;
        if(dp.temp>maxVal) maxVal=dp.temp;
    }
    lv_chart_set_range(chart,LV_CHART_AXIS_PRIMARY_Y,floor(minVal)-1,ceil(maxVal)+1);

    lv_chart_set_point_count(chart,windowSize);
    for(int i=0;i<windowSize;i++){
        if(start+i<total)
            lv_chart_set_value_by_id(chart,series,i,weatherData[start+i].temp);
        else
            lv_chart_set_value_by_id(chart,series,i,LV_CHART_POINT_NONE);
    }
    lv_chart_refresh(chart);
}

void setup_weather_screen(){
    lv_obj_add_event_cb(slider,update_chart_from_slider,LV_EVENT_VALUE_CHANGED,NULL);
    update_chart_from_slider(NULL);
}

// --------------------------------------------------------------------
// --- Parsning av SMHI-data ---
// --------------------------------------------------------------------
void parseWeatherData(const String& jsonData){
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
    update_chart_from_slider(NULL);
}

// --------------------------------------------------------------------
// --- Hämta data via HTTPS ---
// --------------------------------------------------------------------
void update_weather_data(int city_idx,int param_idx){
    String url="https://opendata-download-metobs.smhi.se/api/version/1.0/parameter/";
    url+=param_codes[param_idx];
    url+="/station/";
    url+=stations[city_idx].id;
    url+="/period/latest-months/data.json";

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

// --------------------------------------------------------------------
// --- Arduino setup & loop ---
// --------------------------------------------------------------------
void setup(){
    Serial.begin(115200);
    delay(200);
    Serial.println("Starting LilyGo AMOLED setup...");

    if(!amoled.begin()){
        Serial.println("Failed to init AMOLED");
        while(true) delay(1000);
    }
    amoled.setBrightness(255);
    amoled.setRotation(1);
    beginLvglHelper(amoled);
    lv_timer_handler();
    delay(500);

    create_ui();
    delay(500);
}

void loop(){
    lv_timer_handler();

    if(main_screen_loaded && !splash_deleted){
        static uint32_t animation_start_time=0;
        if(animation_start_time==0) animation_start_time=millis();
        if(millis()-animation_start_time>600){
            lv_obj_del_async(splash_screen);
            splash_deleted=true;
        }
    }

    connect_wifi_non_blocking();

    // --- Automatisk initial datahämtning ---
    if(wifi_connected && !initial_data_fetched){
        initial_data_fetched = true;
        update_weather_data(0,0); // Karlskrona, Temperatur
    }

    delay(5);
}
