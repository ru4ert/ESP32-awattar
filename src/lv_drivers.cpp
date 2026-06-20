/**
 * lv_drivers.cpp – LVGL Display + Touch Treiber
 *
 * Display: TFT_eSPI (ILI9341, VSPI in Sim / HSPI auf Hardware)
 * Touch:   FT6206 via I2C (Simulator) / XPT2046 via SPI (Hardware)
 */
#include "lv_drivers.h"
#include <Arduino.h>

#ifdef SIMULATION
  #include <Wire.h>
  #include <Adafruit_FT6206.h>
  static Adafruit_FT6206 ctp;
#else
  #include <XPT2046_Touchscreen.h>
  #define T_CS   33
  #define T_IRQ  36
  #define T_MOSI 32
  #define T_MISO 39
  #define T_CLK  25
  static SPIClass           touchSPI(VSPI);
  static XPT2046_Touchscreen ts(T_CS, T_IRQ);
#endif

static TFT_eSPI* _tft = nullptr;

// ── Draw Buffer (2× für DMA-ähnliches Ping-Pong) ─────────────────────────────
static lv_color_t buf1[LV_HOR_RES_MAX * LV_BUF_LINES];
static lv_color_t buf2[LV_HOR_RES_MAX * LV_BUF_LINES];
static lv_disp_draw_buf_t draw_buf;

// ── Display Flush Callback ────────────────────────────────────────────────────
static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    _tft->startWrite();
    _tft->setAddrWindow(area->x1, area->y1, w, h);
    _tft->pushColors((uint16_t *)&color_p->full, w * h, true);
    _tft->endWrite();

    lv_disp_flush_ready(disp);
}

// ── Touch Read Callback ───────────────────────────────────────────────────────
static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    int16_t tx = -1, ty = -1;
    bool touched = false;

#ifdef SIMULATION
    if (ctp.touched()) {
        TS_Point p = ctp.getPoint();
        // FT6206 Portrait → Landscape (Rotation 1): x↔y, y invertiert
        tx = (int16_t)map(p.y, 0, 320, 0, LV_HOR_RES_MAX);
        ty = (int16_t)map(p.x, 240, 0, 0, LV_VER_RES_MAX);
        touched = true;
    }
#else
    if (ts.tirqTouched() && ts.touched()) {
        TS_Point p = ts.getPoint();
        tx = (int16_t)map(p.x, 3790, 210, 0, LV_HOR_RES_MAX);
        ty = (int16_t)map(p.y, 3900, 245, 0, LV_VER_RES_MAX);
        touched = true;
    }
#endif

    data->state = touched ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    if (touched) {
        data->point.x = constrain(tx, 0, LV_HOR_RES_MAX - 1);
        data->point.y = constrain(ty, 0, LV_VER_RES_MAX - 1);
    }
}

// ── Öffentliche Init-Funktion ─────────────────────────────────────────────────
void lvgl_init(TFT_eSPI &tft_ref) {
    _tft = &tft_ref;

    lv_init();

    // Draw Buffer registrieren
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LV_HOR_RES_MAX * LV_BUF_LINES);

    // Display Treiber registrieren
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res   = LV_HOR_RES_MAX;
    disp_drv.ver_res   = LV_VER_RES_MAX;
    disp_drv.flush_cb  = disp_flush;
    disp_drv.draw_buf  = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Touch initialisieren
#ifdef SIMULATION
    Wire.begin(21, 22);
    ctp.begin(40);
#else
    touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
    ts.begin(touchSPI);
    ts.setRotation(3);
#endif

    // Touch Input Device registrieren
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

// ── Loop-Funktion ─────────────────────────────────────────────────────────────
void lvgl_update() {
    lv_timer_handler();
}
