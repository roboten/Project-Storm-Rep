#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <lvgl.h>

// --------------------------------------------------------------------
// Wi-Fi credentials (⚠️ DELETE before committing to GitHub)
// --------------------------------------------------------------------
static const char* WIFI_SSID = "";
static const char* WIFI_PASSWORD = "";

// --------------------------------------------------------------------
LilyGo_Class amoled;

// UI objects
static lv_obj_t* tileview;
static lv_obj_t* t1;
static lv_obj_t* t2;
static lv_obj_t* t1_label;
static lv_obj_t* t2_label;
static lv_obj_t* splash_screen;  // temporary boot screen
static bool wifi_connected = false;  // track WiFi connection status
static bool main_screen_loaded = false;  // track if main screen is loaded
static bool splash_deleted = false;  // track if splash screen is deleted

// Forward declaration for boot timer callback
static void switch_to_main(lv_timer_t* timer);

// --------------------------------------------------------------------
// Timer callback: switch from splash to main tileview
// --------------------------------------------------------------------
static void switch_to_main(lv_timer_t* timer)
{
  LV_UNUSED(timer);
  
  Serial.println("Switching to main screen...");
  
  // Set flag to indicate main screen is being loaded
  main_screen_loaded = true;
  
  // Load main interface with fade-in animation (cannot swipe back)
  lv_scr_load_anim(tileview, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
  
  Serial.println("Main screen loaded.");
}

// --------------------------------------------------------------------
// Connect to Wi-Fi (non-blocking)
// --------------------------------------------------------------------
static void connect_wifi_non_blocking()
{
  static bool wifi_started = false;
  
  if (!wifi_started) {
    Serial.printf("Connecting to WiFi SSID: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifi_started = true;
  }
  
  if (WiFi.status() == WL_CONNECTED && !wifi_connected) {
    Serial.println("WiFi connected.");
    wifi_connected = true;
  } else if (WiFi.status() != WL_CONNECTED && millis() > 15000 && !wifi_connected) {
    Serial.println("WiFi could not connect (timeout).");
  }
}

// --------------------------------------------------------------------
// Create UI: includes splash screen and main interface
// --------------------------------------------------------------------
static void create_ui()
{
  //
  // --- Splash Screen ---
  //
  splash_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(splash_screen, lv_color_white(), 0);

  lv_obj_t* splash_label = lv_label_create(splash_screen);
  lv_label_set_text(splash_label, "Group 1\nVersion 0.1");
  lv_obj_set_style_text_font(splash_label, &lv_font_montserrat_28, 0);
  lv_obj_center(splash_label);

  // Display the splash immediately
  lv_scr_load(splash_screen);

  //
  // --- Main UI (Tileview) - created but not yet displayed ---
  //
  tileview = lv_tileview_create(NULL);
  
  // Add two horizontal tiles
  t1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_HOR);
  t2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);

  // --- Tile #1 (Light mode) ---
  lv_obj_set_style_bg_color(t1, lv_color_white(), 0);
  
  t1_label = lv_label_create(t1);
  lv_label_set_text(t1_label, "Tile 1");
  lv_obj_set_style_text_color(t1_label, lv_color_black(), 0);
  lv_obj_center(t1_label);

  // --- Tile #2 (Light mode) ---
  lv_obj_set_style_bg_color(t2, lv_color_white(), 0);
  
  t2_label = lv_label_create(t2);
  lv_label_set_text(t2_label, "Main Screen");
  lv_obj_set_style_text_color(t2_label, lv_color_black(), 0);
  lv_obj_center(t2_label);

  //
  // --- Timer: after 3 seconds, show main UI ---
  //
  lv_timer_t* boot_timer = lv_timer_create(switch_to_main, 3000, NULL);
  lv_timer_set_repeat_count(boot_timer, 1);  // one-shot timer
  
  Serial.println("UI created.");
}

// --------------------------------------------------------------------
// Arduino setup()
// --------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(200);
  
  Serial.println("Starting setup...");

  if (!amoled.begin()) {
    Serial.println("Failed to init LilyGO AMOLED.");
    while (true)
      delay(1000);
  }

  Serial.println("AMOLED initialized.");

  beginLvglHelper(amoled);  // init LVGL for this board

  Serial.println("LVGL initialized.");

  // Process one frame
  lv_timer_handler();
  delay(200);

  create_ui();
  
  Serial.println("Setup complete.");
}

// --------------------------------------------------------------------
// Arduino loop()
// --------------------------------------------------------------------
void loop()
{
  lv_timer_handler();
  
  // Delete splash screen after the animation completes
  if (main_screen_loaded && !splash_deleted) {
    static uint32_t animation_start_time = 0;
    
    if (animation_start_time == 0) {
      animation_start_time = millis();
    }
    
    if (millis() - animation_start_time > 600) {
      Serial.println("Deleting splash screen...");
      lv_obj_del_async(splash_screen);
      splash_deleted = true;
      Serial.println("Splash screen deleted.");
    }
  }
  
  // Non-blocking WiFi connection
  connect_wifi_non_blocking();
  
  delay(5);
}