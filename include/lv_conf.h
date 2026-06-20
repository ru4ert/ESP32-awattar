/**
 * lv_conf.h – LVGL v8.3 Konfiguration für ESP32-2432S028R (CYD)
 * Display: ILI9341, 320×240, 16-bit Farbe
 */

#if 1  /* Aktivieren, damit LVGL diese Datei einliest */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   Farbtiefe
 *====================*/
#define LV_COLOR_DEPTH 16
/* RGB565 Byte-Swap für TFT_eSPI nötig */
#define LV_COLOR_16_SWAP 1

/*====================
   Speicher
 *====================*/
/* LVGL intern Heap (für Widgets, Styles, etc.) */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (32U * 1024U)  /* 32 KB – reduziert um DRAM-Overflow zu beheben */

/*====================
   HAL (Tick)
 *====================*/
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/*====================
   Display Auflösung
 *====================*/
#define LV_HOR_RES_MAX 320
#define LV_VER_RES_MAX 240

/*====================
   Features
 *====================*/
#define LV_USE_ANIMATION 1
#define LV_USE_SHADOW    0  /* Kein RAM für Schatten */
#define LV_USE_BLEND_MODES 0
#define LV_USE_OPA_SCALE 1
#define LV_USE_IMG_TRANSFORM 0
#define LV_USE_GROUP   1    /* Für Touch-Fokus */
#define LV_USE_GPU     0
#define LV_USE_FILESYSTEM 0
#define LV_USE_USER_DATA  1

/*====================
   Logging
 *====================*/
#define LV_USE_LOG 0

/*====================
   Assert
 *====================*/
#define LV_USE_ASSERT_NULL     1
#define LV_USE_ASSERT_MALLOC   1
#define LV_USE_ASSERT_STYLE    0
#define LV_USE_ASSERT_OBJ      0
#define LV_USE_ASSERT_MEM_INTEGRITY 0

/*====================
   Widgets (nur was wir brauchen)
 *====================*/
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1  // Wird von Keyboard/Msgbox benötigt
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   0
#define LV_USE_CHART      1
#define LV_USE_DROPDOWN   0
#define LV_USE_IMG        1  // Wird von animimg benötigt
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   1  // Wird von Keyboard/Spinbox benötigt
#define LV_USE_TABLE      0

/*====================
   Extra Widgets (deaktiviert um RAM zu sparen)
 *====================*/
#define LV_USE_ANIMIMG    0
#define LV_USE_CALENDAR   0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   0
#define LV_USE_LED        0
#define LV_USE_LIST       1
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

/*====================
   Themes
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1       /* Dunkles Theme */
#define LV_THEME_DEFAULT_GROW 0
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

#define LV_USE_THEME_SIMPLE   0
#define LV_USE_THEME_MONO     0

/*====================
   Layouts
 *====================*/
#define LV_USE_FLEX   1
#define LV_USE_GRID   0

/*====================
   Fonts
 *====================*/
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 1

#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_FONT_UNSCII_8  0
#define LV_FONT_UNSCII_16 0

/*====================
   Sonstiges
 *====================*/
#define LV_SPRINTF_CUSTOM 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

#endif /* LV_CONF_H */
#endif /* End of "Content enable" */
