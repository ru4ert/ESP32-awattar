#pragma once
/**
 * lv_drivers.h – LVGL Display + Touch Treiber für ESP32-Awattar
 */
#include <lvgl.h>
#include <TFT_eSPI.h>

// Draw-Buffer Größe: 1/10 Screen = 320*24 Pixel
#define LV_BUF_LINES 24

void lvgl_init(TFT_eSPI &tft_ref);
void lvgl_update();  // Muss in loop() aufgerufen werden
