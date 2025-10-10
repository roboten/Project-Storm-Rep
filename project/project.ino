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
static bool t2_dark = false;     // start tile #2 in light mode

// Forward declaration for boot timer callback
static void switch_to_main(lv_timer_t* timer);

// --------------------------------------------------------------------
// Theme/color utility
// --------------------------------------------------------------------
static void apply_tile_colors(lv_obj_t* tile, lv_obj_t* label, bool dark)
{
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(tile, dark ? lv_color_black() : lv_color_white(), 0);
  lv_obj_set_style_text_color(label, dark ? lv_color_white() : lv_color_black(), 0);
}

// --------------------------------------------------------------------
// Event: toggle color on tile #2 when tapped
// --------------------------------------------------------------------
static void on_tile2_clicked(lv_event_t* e)
{
  LV_UNUSED(e);
  t2_dark = !t2_dark;
  apply_tile_colors(t2, t2_label, t2_dark);
}

// --------------------------------------------------------------------
// Timer callback: switch from splash to main tileview
// --------------------------------------------------------------------
static void switch_to_main(lv_timer_t* timer)
{
  LV_UNUSED(timer);

  // Load main interface with fade-in animation (cannot swipe back)
  lv_scr_load_anim(tileview, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);

  // Free splash screen memory (optional but clean)
  lv_obj_del(splash_screen);
}

// --------------------------------------------------------------------
// Create UI: includes splash screen and main interface
// --------------------------------------------------------------------
static void create_ui()
{
  //
  // --- Splash Screen ---
  //
  splash_screen = lv_obj_create(NULL);  // separate screen (not inside tileview)
  lv_obj_set_style_bg_color(splash_screen, lv_color_white(), 0);

  lv_obj_t* splash_label = lv_label_create(splash_screen);
  lv_label_set_text(splash_label, "Group 1\nVersion 0.1");
  lv_obj_set_style_text_font(splash_label, &lv_font_montserrat_28, 0);
  lv_obj_center(splash_label);

  // Display the splash immediately
  lv_scr_load(splash_screen);

  //
  // --- Main UI (Tileview) ---
  //
  tileview = lv_tileview_create(NULL);
  lv_obj_set_size(tileview, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
  lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);

  // Add two horizontal tiles
  t1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_HOR);
  t2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);

  // --- Tile #1 content ---
  t1_label = lv_label_create(t1);
  lv_label_set_text(t1_label, "Tile 1");
  lv_obj_center(t1_label);

  // --- Tile #2 content ---
  t2_label = lv_label_create(t2);
  lv_label_set_text(t2_label, "Main Screen");
  lv_obj_center(t2_label);

  apply_tile_colors(t2, t2_label, /*dark=*/false);
  lv_obj_add_flag(t2, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(t2, on_tile2_clicked, LV_EVENT_CLICKED, NULL);

  //
  // --- Timer: after 3 seconds, show main UI ---
  //
  lv_timer_t* boot_timer = lv_timer_create(switch_to_main, 3000, NULL);
  lv_timer_set_repeat_count(boot_timer, 1);  // one-shot timer
}

// --------------------------------------------------------------------
// Connect to Wi-Fi
// --------------------------------------------------------------------
static void connect_wifi()
{
  Serial.printf("Connecting to WiFi SSID: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(250);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected.");
  } else {
    Serial.println("WiFi could not connect (timeout).");
  }
}

// --------------------------------------------------------------------
// Arduino setup()
// --------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(200);

  if (!amoled.begin()) {
    Serial.println("Failed to init LilyGO AMOLED.");
    while (true)
      delay(1000);
  }

  beginLvglHelper(amoled);  // init LVGL for this board

  // ⚙️ Wait a bit after initializing LVGL helper
  lv_timer_handler();  // process one frame
  delay(200);

  create_ui();
  lv_timer_handler();  // make sure splash screen is drawn

  connect_wifi();
}

// --------------------------------------------------------------------
// Arduino loop()
// --------------------------------------------------------------------
void loop()
{
  lv_timer_handler();
  delay(5);
}