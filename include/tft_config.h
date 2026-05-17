// TFT_eSPI Konfiguration für ESP32-2432S028R (Cheap Yellow Display)
// Diese Datei wird via build_flags "-include include/tft_config.h" eingebunden.
// USER_SETUP_LOADED=1 verhindert, dass die Library ihre eigene User_Setup.h lädt.

#pragma once

// ── Treiber ──────────────────────────────────────────────────────────────────
#define ILI9341_DRIVER

// ── Display SPI Pins (HSPI) ──────────────────────────────────────────────────
#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1   // Reset nicht verwendet
#define TFT_BL    21   // Backlight

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
