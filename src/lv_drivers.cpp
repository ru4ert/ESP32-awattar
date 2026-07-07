/**
 * lv_drivers.cpp – LVGL Display + Touch Treiber
 *
 * Display: TFT_eSPI (VSPI in Sim / HSPI auf Hardware)
 * Touch:   FT6206 via I2C (Simulator) / XPT2046 via SPI (Hardware)
 *
 * Rotation, Invertierung und Farbreihenfolge sind zur LAUFZEIT umschaltbar
 * (display_apply), weil das verbaute Panel ein Clone mit abweichender
 * MADCTL-Interpretation ist – kalibrierbar über den Setup-Screen.
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

static TFT_eSPI  *_tft  = nullptr;
static lv_disp_t *_disp = nullptr;
static DispCfg    _cfg  = { 3, true, true };

// ── Draw Buffer (2× für Ping-Pong) ───────────────────────────────────────────
static lv_color_t buf1[320 * LV_BUF_LINES];
static lv_color_t buf2[320 * LV_BUF_LINES];
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

// ── Laufzeit-Konfiguration anwenden ───────────────────────────────────────────
// MADCTL-Basisbytes wie TFT_eSPI ILI9341_Rotation.h (MY=0x80 MX=0x40 MV=0x20)
static const uint8_t MADCTL_BASE[8] = {
    0x40, 0x20, 0x80, 0xE0,   // Rotation 0-3
    0xC0, 0x60, 0x00, 0xA0    // Rotation 4-7 (gespiegelt)
};

void display_apply(const DispCfg &cfg) {
    _cfg = cfg;
    _cfg.rot &= 7;

    // setRotation setzt Breite/Höhe + MADCTL (mit Compile-Zeit-Farbreihenfolge),
    // danach überschreiben wir MADCTL mit dem gewünschten BGR-Bit
    _tft->setRotation(_cfg.rot);
    _tft->startWrite();
    _tft->writecommand(TFT_MADCTL);
    _tft->writedata(MADCTL_BASE[_cfg.rot] | (_cfg.bgr ? 0x08 : 0x00));
    _tft->endWrite();

    _tft->invertDisplay(_cfg.inv);

    // LVGL über neue Auflösung informieren (Landscape ↔ Portrait)
    if (_disp) {
        _disp->driver->hor_res = _tft->width();
        _disp->driver->ver_res = _tft->height();
        lv_disp_drv_update(_disp, _disp->driver);
        lv_obj_invalidate(lv_scr_act());
    }

    Serial.printf("[DISP] rot=%d inv=%d bgr=%d -> %dx%d MADCTL=0x%02X\n",
                  _cfg.rot, _cfg.inv, _cfg.bgr, _tft->width(), _tft->height(),
                  MADCTL_BASE[_cfg.rot] | (_cfg.bgr ? 0x08 : 0x00));
}

DispCfg display_cfg() { return _cfg; }

// ── Touch Read Callback ───────────────────────────────────────────────────────
static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    int16_t tx = -1, ty = -1;
    bool touched = false;
    int W = _tft->width(), H = _tft->height();

#ifdef SIMULATION
    if (ctp.touched()) {
        TS_Point p = ctp.getPoint();
        // FT6206 Portrait → Landscape (Rotation 1): x↔y, y invertiert
        tx = (int16_t)map(p.y, 0, 320, 0, W);
        ty = (int16_t)map(p.x, 240, 0, 0, H);
        touched = true;
    }
#else
    if (ts.tirqTouched() && ts.touched()) {
        TS_Point p = ts.getPoint();
        // Glas-Achsen (kalibriert in Rotation 3): L = lange Achse, S = kurze Achse
        int L = map(p.x, 3790, 210, 0, 319);
        int S = map(p.y, 3900, 245, 0, 239);
        switch (_cfg.rot & 3) {
            case 3: tx = L;        ty = S;        break;  // kalibrierte Basis
            case 1: tx = 319 - L;  ty = 239 - S;  break;  // 180° dazu
            case 0: tx = 239 - S;  ty = L;        break;  // Portrait
            case 2: tx = S;        ty = 319 - L;  break;  // Portrait 180°
        }
        touched = true;
    }
#endif

    data->state = touched ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    if (touched) {
        data->point.x = constrain(tx, 0, W - 1);
        data->point.y = constrain(ty, 0, H - 1);
    }
}

// ── Öffentliche Init-Funktion ─────────────────────────────────────────────────
void lvgl_init(TFT_eSPI &tft_ref, const DispCfg &cfg) {
    _tft = &tft_ref;

    lv_init();

    // Draw Buffer registrieren
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 320 * LV_BUF_LINES);

    // Display Treiber registrieren
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res   = _tft->width();
    disp_drv.ver_res   = _tft->height();
    disp_drv.flush_cb  = disp_flush;
    disp_drv.draw_buf  = &draw_buf;
    _disp = lv_disp_drv_register(&disp_drv);

    // Panel-Konfiguration (Rotation/Inv/BGR) anwenden
    display_apply(cfg);

    // Touch initialisieren
#ifdef SIMULATION
    Wire.begin(21, 22);
    ctp.begin(40);
#else
    touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
    ts.begin(touchSPI);
    ts.setRotation(3);  // Rohwerte bleiben fix, Mapping erfolgt oben pro Rotation
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
