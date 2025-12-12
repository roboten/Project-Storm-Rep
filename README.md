# PA1484 – Software Development:  Project Storm

## Introduction

PA1484 – Software Development

## Introduction

Project Storm is a weather visualization system built for an ESP32-based
touchscreen device. The system retrieves meteorological data from the
SMHI Open Data API and presents it in a clear, interactive, and
user-friendly interface.

The project solves the problem of accessing detailed Swedish weather data
in a compact, embedded form factor without relying on a phone or computer.
Instead of raw data or text-heavy interfaces, the application visualizes
weather trends using charts, icons, and forecasts optimized for a
touchscreen display.

### Main features

- Live weather data from SMHI
- Interactive temperature charts
- 7-day weather forecast with icons
- City-based station selection
- Parameter selection (temperature, wind, precipitation, etc.)
- Persistent settings stored in flash memory
- Touchscreen-based navigation using tiles and sliders

### Technologies used

- ESP32 microcontroller
- Touchscreen AMOLED display
- LVGL (Light and Versatile Graphics Library)
- SMHI Open Data API
- PlatformIO
- Arduino framework
- Wi‑Fi (HTTPS)

## Getting started

This section explains how to set up the project and prepare your development
environment.

### Prerequisites

Before starting, make sure you have:

- An ESP32-based board (tested with LilyGo AMOLED)
- A compatible touchscreen display
- USB cable for flashing the device
- Wi‑Fi access (2.4 GHz)
- VS Code
- PlatformIO extension installed in VS Code

You also need an active internet connection to fetch weather data from SMHI.

### Installation

1. Clone the repository
2. Open the project in VS Code
   - Launch VS Code
   - Open the project folder
   - PlatformIO will automatically detect the project
3. Configure Wi‑Fi credentials
   Open project.ino and update:
   const char *WIFI_SSID = "YOUR_WIFI_NAME";
   const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
4. Install dependencies
   PlatformIO will automatically download all required libraries:
   - LVGL
   - ArduinoJson
   - HTTPClient
   - ESP32 WiFi libraries

## Building and running

### Build and upload

1. Connect the ESP32 to your computer using USB
2. In VS Code:
	- Click PlatformIO: Build
	- Click PlatformIO: Upload
Once uploaded, reboot the device.

### Startup procedure

1. ESP32 boots and initializes the display
2. Wi‑Fi connection is established
3. Time is synchronized via NTP
4. Weather stations are loaded from SMHI
5. Previously saved settings (city & parameter) are restored
6. Weather data and forecast are fetched automatically

### User interaction

The interface is fully touchscreen-based and organized using horizontal tiles.

Navigation
- Swipe left/right to switch between screens
- Slider to scroll through historical weather data
- Dropdown menus to select city and weather parameters
- On-screen keyboard for city search
- Buttons to save or reset default settings

Screens
1. Splash screen
2. 7‑day forecast
3. Temperature chart
4. Settings screen

## Features

- [x] US1.1C:  As a user, I want to see a starting screen to display the current program
      version and group number on the first screen
- [x] US1.2C: As a user, I want to see the weather forecast for the next 7 days for the
      selected city on the second screen in terms of temperature and weather conditions
      with symbols (e.g., clear sky, rain, snow, thunder) per day at 12:00.
- [x] US1.3: As a user, I want to have a screen to view weather forecast data.
- [x] US2.1: As a user, I want to be able to navigate between different screens (like forecast
      screen) by sliding a finger over the touch screen.
- [x] US3.1: As a user, I want to have a screen to view historical weather data.
- [x] US3.2D: As a user, on the third screen I want to view the latest months (SMHI API
      period: latest-months) of historical hourly data for selected weather parameter in the
      selected city, using a slider to interact with the historical graph by scrolling where a
      depleted slider corresponds to the oldest datapoint and a full slider corresponds to the
      latest datapoint.
- [x] US4.1: As a user, on the fourth screen, I want to access a single settings screen to
      configure both the city and weather parameter options.
- [x] US4.2B: As a user, I want to select from four weather parameters, namely
      temperature (1), humidity (6), wind speed (4), and Air pressure (9), using a dropdown
      list, to customize the historical graph.
- [x] US4.3B: As a user, I want to select from five different cities, namely
      Karlskrona(65090), Stockholm(97400), Göteborg(72420), Malmö(53300), and
      Kiruna(180940), using a dropdown, to view their weather data for the historical data
      and starting screen forecast.
- [x] US4.4: As a user, I want to reset the selected city and weather parameter to default
      using a button.
- [x] US4.5: As a user, I want to set my default city and weather parameter to the current
      selection using a button, so they are automatically selected when I start the device.
- [x] US4.6: As a user, I want the device to store my default city and weather parameter so
      that they are retained even after a restart.

## License

This project was developed as part of the course
**PA1484 – Software Development.**
