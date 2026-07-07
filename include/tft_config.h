// TFT_eSPI Konfiguration für ESP32-2432S028R (Cheap Yellow Display)
// Diese Datei wird via build_flags "-include include/tft_config.h" eingebunden.
// USER_SETUP_LOADED=1 verhindert, dass die Library ihre eigene User_Setup.h lädt.

#pragma once

// ── Treiber ──────────────────────────────────────────────────────────────────
#define ILI9341_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#ifdef SIMULATION
// ── Wokwi Simulator: VSPI Pins (D18/D23/D19) ─────────────────────────────────
// Entspricht diagram.json – identisch mit funktionierendem hello-wowki Projekt.
#define TFT_MISO  19
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST    4
#define TFT_BL    -1   // kein Backlight in Sim
#else
// ── Hardware: ESP32-2432S028R HSPI Pins ──────────────────────────────────────
#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1   // Reset nicht verwendet
#define TFT_BL    21   // Backlight

// ACHTUNG Panel-Quirk: Das verbaute Panel ist ein Clone, dessen MADCTL-Bits
// (Rotation/Spiegelung/Farbreihenfolge) und Invertierung nicht der ILI9341-
// Norm entsprechen. Deshalb werden Rotation, Invertierung und R/B-Reihenfolge
// NICHT hier zur Compile-Zeit gesetzt, sondern zur Laufzeit über den
// Kalibrier-Screen (Zahnrad im Dashboard) konfiguriert und im NVS gespeichert.
// Siehe display_apply() in src/lv_drivers.cpp.
#endif

// ── Touch (XPT2046 auf VSPI) ─────────────────────────────────────────────────
#define TOUCH_CS  33

// ── Fonts laden ──────────────────────────────────────────────────────────────
#define LOAD_GLCD    // Font 1
#define LOAD_FONT2   // Font 2
#define LOAD_FONT4   // Font 4
#define LOAD_FONT6   // Font 6 (groß)
#define LOAD_FONT7   // Font 7 (7-Segment, für Preis-Anzeige)
#define LOAD_FONT8   // Font 8
#define LOAD_GFXFF   // Freetype Fonts

// ── SPI Geschwindigkeit ───────────────────────────────────────────────────────
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
