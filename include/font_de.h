#pragma once
#include <stdint.h>

// Eigenes, minimales Font-Format (aus Montserrat-Medium via lv_font_conv,
// bpp 8 = ein Alpha-Byte pro Pixel). Gerendert von drawDE() in main.cpp.
// Neu generieren: scratchpad/pack_font.js (Kommando siehe Git-Historie).

struct DEGlyph {
    uint16_t cp;    // Unicode-Codepoint
    uint16_t off;   // Offset ins Bitmap-Array
    uint8_t  w, h;  // Box-Größe in px
    int8_t   ox;    // X-Versatz ab Stiftposition
    int8_t   oy;    // Y-Versatz Box-UNTERKANTE relativ zur Baseline (+ = darüber)
    uint8_t  adv;   // Vorschub in px
};

struct DEFont {
    const DEGlyph *glyphs;
    const uint8_t *bitmap;
    uint16_t count;
    uint8_t  lineHeight;
    uint8_t  baseLine;
};

extern const DEFont FONT_DE14;
extern const DEFont FONT_DE10;
