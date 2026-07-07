/**
 * ui_setup.cpp – Display-Kalibrier-Screen
 *
 * Das Panel ist ein Clone mit nicht-standardkonformer MADCTL-Interpretation.
 * Hier lassen sich Rotation (0-7), Farb-Invertierung und R/B-Reihenfolge live
 * durchschalten, bis Testbild + Orientierung stimmen. "Speichern" legt die
 * Werte im NVS ab (Namespace "disp"), sie werden beim Boot geladen.
 *
 * Der Screen ist absichtlich komplett in Prozent-Maßen aufgebaut, damit er in
 * jeder (auch falscher) Orientierung bedienbar bleibt.
 */
#include "ui.h"
#include "lv_drivers.h"
#include <Arduino.h>
#include <Preferences.h>

static lv_obj_t *lbl_state = nullptr;

// ── Anzeige des aktuellen Zustands ────────────────────────────────────────────
static void refresh_state_label() {
    if (!lbl_state) return;
    DispCfg c = display_cfg();
    lv_label_set_text_fmt(lbl_state, "Rotation %d  |  Invert %s  |  %s",
                          c.rot, c.inv ? "AN" : "AUS", c.bgr ? "BGR" : "RGB");
}

// ── Callbacks ─────────────────────────────────────────────────────────────────
static void cb_rotate(lv_event_t *e) {
    DispCfg c = display_cfg();
    c.rot = (c.rot + 1) & 7;
    display_apply(c);
    refresh_state_label();
}

static void cb_invert(lv_event_t *e) {
    DispCfg c = display_cfg();
    c.inv = !c.inv;
    display_apply(c);
    refresh_state_label();
}

static void cb_rgb(lv_event_t *e) {
    DispCfg c = display_cfg();
    c.bgr = !c.bgr;
    display_apply(c);
    refresh_state_label();
}

static void cb_save(lv_event_t *e) {
    DispCfg c = display_cfg();
    Preferences prefs;
    prefs.begin("disp", false);
    prefs.putUChar("rot", c.rot);
    prefs.putBool("inv", c.inv);
    prefs.putBool("bgr", c.bgr);
    prefs.end();
    Serial.printf("[DISP] gespeichert: rot=%d inv=%d bgr=%d\n", c.rot, c.inv, c.bgr);
    ui_show_dashboard();
}

// ── Hilfsbauer ────────────────────────────────────────────────────────────────
static void make_color_bar(lv_obj_t *parent, lv_color_t col, const char *txt,
                           lv_color_t txtcol) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(31), 34);
    lv_obj_set_style_bg_color(bar, col, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lv_label_create(bar);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_de_14, 0);
    lv_obj_set_style_text_color(l, txtcol, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
}

static lv_obj_t* make_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb,
                          lv_color_t bg) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(48), 40);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(btn);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_de_14, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return btn;
}

// ── Screen erstellen ──────────────────────────────────────────────────────────
void ui_setup_create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, 6, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 6, 0);

    // Orientierungs-Marker + Titel
    lv_obj_t *hdr = lv_label_create(parent);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_de_14, 0);
    lv_obj_set_style_text_color(hdr, UI_COLOR_WHITE, 0);
    lv_obj_set_width(hdr, LV_PCT(100));
    lv_label_set_text(hdr, LV_SYMBOL_UP " OBEN LINKS  —  DISPLAY-SETUP");

    // Farb-Testbalken
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_color_bar(row, lv_color_hex(0xFF0000), "ROT",  UI_COLOR_WHITE);
    make_color_bar(row, lv_color_hex(0x00FF00), "GRÜN", lv_color_hex(0x000000));
    make_color_bar(row, lv_color_hex(0x0000FF), "BLAU", UI_COLOR_WHITE);

    // Zustand
    lbl_state = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl_state, &lv_font_montserrat_de_12, 0);
    lv_obj_set_style_text_color(lbl_state, UI_COLOR_SILVER, 0);
    refresh_state_label();

    // Buttons (2×2, groß – muss auch bei falscher Touch-Zuordnung treffbar sein)
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 6, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_btn(grid, LV_SYMBOL_REFRESH " Drehen",  cb_rotate, lv_color_hex(0x1565C0));
    make_btn(grid, "Invertieren",                cb_invert, lv_color_hex(0x37474F));
    make_btn(grid, "R/B tauschen",               cb_rgb,    lv_color_hex(0x37474F));
    make_btn(grid, LV_SYMBOL_OK " Speichern",    cb_save,   lv_color_hex(0x1B5E20));
}
