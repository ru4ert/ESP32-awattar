#pragma once
/**
 * lv_drivers.h – LVGL Display + Touch Treiber für ESP32-Awattar
 */
#include <lvgl.h>
#include <TFT_eSPI.h>

// Draw-Buffer Größe: 1/10 Screen = 320*24 Pixel
#define LV_BUF_LINES 24

// ── Laufzeit-Display-Konfiguration (Panel-Clone, siehe tft_config.h) ─────────
struct DispCfg {
    uint8_t rot;   // 0-7 (4-7 = gespiegelte Varianten)
    bool    inv;   // Farb-Invertierung (INVON/INVOFF)
    bool    bgr;   // MADCTL BGR-Bit (Farbreihenfolge)
};

void    lvgl_init(TFT_eSPI &tft_ref, const DispCfg &cfg);
void    lvgl_update();                    // Muss in loop() aufgerufen werden
void    display_apply(const DispCfg &cfg); // Rotation/Inv/BGR live umschalten
DispCfg display_cfg();                     // aktuelle Konfiguration
