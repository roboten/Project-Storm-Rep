#pragma once
#include <lvgl.h>

/**
 * Weather Icon Drawing System
 * Renders weather condition icons using simple geometric shapes
 * Based on SMHI Wsymb2 weather symbol codes (1-27)
 * Icons are composed of: sun, clouds, rain, snow, lightning, sleet
 */

// ------------------------------------------------------------------
// Sun Icon Helper
// Draws a circular sun at the specified size and color
// ------------------------------------------------------------------
static void draw_sun(lv_obj_t *parent, int size, int color_hex) {
  lv_obj_t *sun = lv_obj_create(parent);
  lv_obj_set_size(sun, size, size);
  lv_obj_set_style_radius(sun, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(sun, lv_color_hex(color_hex), 0);
  lv_obj_set_style_border_width(sun, 0, 0);
  lv_obj_align(sun, LV_ALIGN_CENTER, 0, 0);
}

// ------------------------------------------------------------------
// Cloud Icon Helper
// Draws a cloud using multiple overlapping circles for a puffy appearance
// Composed of: center body, left puff, right puff, and bottom filler
// ------------------------------------------------------------------
static void draw_cloud(lv_obj_t *parent, int size, int color_hex) {
  // Main cloud body (center)
  lv_obj_t *c1 = lv_obj_create(parent);
  lv_obj_set_size(c1, size * 0.6, size * 0.6);
  lv_obj_set_style_radius(c1, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(c1, lv_color_hex(color_hex), 0);
  lv_obj_set_style_border_width(c1, 0, 0);
  lv_obj_align(c1, LV_ALIGN_CENTER, 0, 2);

  // Left puff
  lv_obj_t *c2 = lv_obj_create(parent);
  lv_obj_set_size(c2, size * 0.4, size * 0.4);
  lv_obj_set_style_radius(c2, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(c2, lv_color_hex(color_hex), 0);
  lv_obj_set_style_border_width(c2, 0, 0);
  lv_obj_align(c2, LV_ALIGN_CENTER, -size * 0.3, 5);

  // Right puff
  lv_obj_t *c3 = lv_obj_create(parent);
  lv_obj_set_size(c3, size * 0.45, size * 0.45);
  lv_obj_set_style_radius(c3, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(c3, lv_color_hex(color_hex), 0);
  lv_obj_set_style_border_width(c3, 0, 0);
  lv_obj_align(c3, LV_ALIGN_CENTER, size * 0.3, 4);

  // Bottom flat-ish part filler
  lv_obj_t *c4 = lv_obj_create(parent);
  lv_obj_set_size(c4, size * 0.7, size * 0.3);
  lv_obj_set_style_radius(c4, 10, 0);
  lv_obj_set_style_bg_color(c4, lv_color_hex(color_hex), 0);
  lv_obj_set_style_border_width(c4, 0, 0);
  lv_obj_align(c4, LV_ALIGN_CENTER, 0, 8);
}

// ------------------------------------------------------------------
// Rain Icon Helper
// Draws rain as blue vertical drops below the cloud
// ------------------------------------------------------------------
static void draw_rain(lv_obj_t *parent, int size) {
  for (int i = 0; i < 3; i++) {
    lv_obj_t *drop = lv_obj_create(parent);
    lv_obj_set_size(drop, 4, 10);                               // Bigger drops
    lv_obj_set_style_bg_color(drop, lv_color_hex(0x209CEE), 0); // Blue rain
    lv_obj_set_style_border_width(drop, 0, 0);
    lv_obj_set_style_radius(drop, 2, 0);
    lv_obj_align(drop, LV_ALIGN_CENTER, (i - 1) * 8, size / 2);
  }
}

// ------------------------------------------------------------------
// Lightning Icon Helper
// Draws a lightning bolt using a rotated yellow rectangle
// ------------------------------------------------------------------
static void draw_lightning(lv_obj_t *parent, int size) {
  // Use a small rotated box for a simple bolt segment
  lv_obj_t *l1 = lv_obj_create(parent);
  lv_obj_set_size(l1, 5, 18); // Bigger bolt
  lv_obj_set_style_bg_color(l1, lv_color_hex(0xFFD700), 0);
  lv_obj_set_style_border_width(l1, 0, 0);
  lv_obj_set_style_transform_angle(l1, 300, 0);
  lv_obj_align(l1, LV_ALIGN_CENTER, 0, 5);
}

// ------------------------------------------------------------------
// Snow Icon Helper
// Draws snow as white circular flakes
// ------------------------------------------------------------------
static void draw_snow(lv_obj_t *parent, int size) {
  for (int i = 0; i < 3; i++) {
    lv_obj_t *flake = lv_obj_create(parent);
    lv_obj_set_size(flake, 6, 6); // Bigger flakes
    lv_obj_set_style_bg_color(flake, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(flake, 0, 0);
    lv_obj_set_style_radius(flake, 3, 0);
    lv_obj_align(flake, LV_ALIGN_CENTER, (i - 1) * 10, size / 2);
  }
}

// ------------------------------------------------------------------
// Sleet Icon Helper
// Draws sleet as a mix of rain drops (blue) and snow flakes (white)
// ------------------------------------------------------------------
static void draw_sleet(lv_obj_t *parent, int size) {
  // Rain drops
  for (int i = 0; i < 2; i++) {
    lv_obj_t *drop = lv_obj_create(parent);
    lv_obj_set_size(drop, 4, 10);
    lv_obj_set_style_bg_color(drop, lv_color_hex(0x209CEE), 0);
    lv_obj_set_style_border_width(drop, 0, 0);
    lv_obj_set_style_radius(drop, 2, 0);
    lv_obj_align(drop, LV_ALIGN_CENTER, (i - 1) * 12 - 4, size / 2);
  }
  // Snow flakes
  for (int i = 0; i < 2; i++) {
    lv_obj_t *flake = lv_obj_create(parent);
    lv_obj_set_size(flake, 6, 6);
    lv_obj_set_style_bg_color(flake, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(flake, 0, 0);
    lv_obj_set_style_radius(flake, 3, 0);
    lv_obj_align(flake, LV_ALIGN_CENTER, (i - 1) * 12 + 4, size / 2 + 2);
  }
}

/**
 * Main Weather Icon Renderer
 * Maps SMHI Wsymb2 symbol codes to visual representations
 *
 * Symbol codes:
 * 1: Clear sky
 * 2-4: Partly cloudy (sun + cloud)
 * 5-7: Cloudy/Overcast/Fog
 * 8-10, 18-20: Rain
 * 11, 21: Thunderstorm
 * 12-14, 22-24: Sleet
 * 15-17, 25-27: Snow
 */
static void draw_weather_icon(lv_obj_t *parent, int s, int size) {
  if (s == 1) {
    // Clear sky - sun only
    draw_sun(parent, (int)(size * 0.75), 0xFFD700);
  } else if (s >= 2 && s <= 4) {
    // Partly cloudy - sun partially obscured by cloud
    draw_sun(parent, (int)(size * 0.75), 0xFFD700);

    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, size, size);
    lv_obj_set_style_bg_opa(c, LV_OPA_0, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_align(c, LV_ALIGN_CENTER, 8, 8);
    draw_cloud(c, size, 0xFFFFFF);
  } else if (s >= 5 && s <= 7) {
    // Cloudy / Overcast / Fog
    draw_cloud(parent, size, 0xBBBBBB);
  } else if ((s >= 8 && s <= 10) || (s >= 18 && s <= 20)) {
    // Rain (light to heavy)
    draw_cloud(parent, size, 0x888888);
    draw_rain(parent, size);
  } else if (s == 11 || s == 21) {
    // Thunderstorm
    draw_cloud(parent, size, 0x555555);
    draw_rain(parent, size);
    draw_lightning(parent, size);
  } else if ((s >= 12 && s <= 14) || (s >= 22 && s <= 24)) {
    // Sleet (mixed rain and snow)
    draw_cloud(parent, size, 0x888888);
    draw_sleet(parent, size);
  } else if ((s >= 15 && s <= 17) || (s >= 25 && s <= 27)) {
    // Snow (light to heavy)
    draw_cloud(parent, size, 0xBBBBBB);
    draw_snow(parent, size);
  } else {
    // Unknown symbol - show question mark
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, "?");
    lv_obj_center(l);
  }
}
