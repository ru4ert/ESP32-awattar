#pragma once
/**
 * ui.h – Gemeinsame UI-Deklarationen für LVGL Screens
 */
#include <lvgl.h>
#include <time.h>

// ── Datenstruktur (geteilt mit main.cpp) ─────────────────────────────────────
struct HourSlot { time_t ts; float ct; };
#define MAX_SLOTS 48

extern HourSlot  slots[];
extern int       slotCount;
extern bool      tomorrowAvail;
extern bool      isFetching;

// Strommix (energy-charts.info): Anteil Erneuerbare in % an der Erzeugung
extern int       renewShare;
extern bool      renewValid;

// Emoji-Bilder (echte Farb-Emojis als RGB565-Bitmaps, src/img/)
LV_IMG_DECLARE(img_emoji_battery);   // 🔋 erneuerbar
LV_IMG_DECLARE(img_emoji_oil);       // 🛢 fossil

// ── Farb-Palette (LVGL lv_color_hex) ─────────────────────────────────────────
#define UI_COLOR_BG       lv_color_hex(0x0D1B2A)   // Dunkelblau
#define UI_COLOR_HDR      lv_color_hex(0x1B2A3B)   // Etwas heller
#define UI_COLOR_GREEN    lv_color_hex(0x00C853)
#define UI_COLOR_YELLOW   lv_color_hex(0xFFD600)
#define UI_COLOR_RED      lv_color_hex(0xFF1744)
#define UI_COLOR_WHITE    lv_color_hex(0xFFFFFF)
#define UI_COLOR_SILVER   lv_color_hex(0xB0BEC5)
#define UI_COLOR_CYAN     lv_color_hex(0x00E5FF)

// Preis-Schwellenwerte (ct/kWh)
#define PRICE_CHEAP  8.0f
#define PRICE_HIGH  18.0f

// Farbe je nach Preis
static inline lv_color_t price_color(float ct) {
    if (ct <= 0.0f)        return UI_COLOR_CYAN;
    if (ct < PRICE_CHEAP)  return UI_COLOR_GREEN;
    if (ct < PRICE_HIGH)   return UI_COLOR_YELLOW;
    return UI_COLOR_RED;
}

// ── Screen-Funktionen ─────────────────────────────────────────────────────────
void ui_dashboard_create(lv_obj_t *parent);
void ui_dashboard_update();   // Daten aktualisieren (nach Fetch)

void ui_chart_create(lv_obj_t *parent);
void ui_chart_update(bool showToday);
bool ui_chart_is_today();     // aktuelle Chart-Ansicht (für Refresh nach Fetch)

void ui_setup_create(lv_obj_t *parent);   // Display-Kalibrierung

// ── Haupt-Screen wechseln ─────────────────────────────────────────────────────
void ui_show_dashboard();
void ui_show_chart();
void ui_show_setup();
