#pragma once
#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
// Panel-Farbkompensation (HW-458 / ST7789-Clone)
//
// Gemessen (Fotos Juli 2026): mit der bewährten main-Konfiguration zeigt das
// Panel jede Farbe INVERTIERT und mit VERTAUSCHTEM Rot/Blau-Kanal an:
//
//     Anzeige = invert( swapRB( gesendet ) )
//
// Register-Fixes (INVON/INVOFF, MADCTL-BGR-Bit) verhalten sich auf diesem
// Controller nicht normgerecht und haben beim LVGL-Versuch die Orientierung
// zerschossen. Deshalb wird hier NICHTS am Displaytreiber geändert, sondern
// jede Farbe in Software vorkompensiert:
//
//     gesendet = invert( swapRB( ziel ) )   →   Anzeige = ziel
//
// Alle C_*-Konstanten in main.cpp sind damit wieder ECHTE RGB565-Farben.
// ─────────────────────────────────────────────────────────────────────────────
constexpr uint16_t PANEL(uint16_t target) {
    return (uint16_t)~(uint16_t)(((target & 0xF800) >> 11)   // R → B-Position
                               |  (target & 0x07E0)          // G bleibt
                               | ((target & 0x001F) << 11)); // B → R-Position
}
