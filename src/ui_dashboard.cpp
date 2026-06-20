/**
 * ui_dashboard.cpp – Haupt-Dashboard Screen
 *
 * Layout (320×240 Landscape):
 * ┌─────────────────────────────────┐
 * │  Header: Zeit + Datum  [Fetch]  │ 24px
 * ├─────────────────────────────────┤
 * │                                 │
 * │   ┌──────────┐  ┌────────────┐ │
 * │   │ JETZT    │  │ Nächste Std│ │
 * │   │ 18.7 ct  │  │ Liste      │ │
 * │   │ ████████ │  │            │ │
 * │   └──────────┘  └────────────┘ │
 * │                                 │
 * ├─────────────────────────────────┤
 * │ [< Heute]    [Chart >]          │ 28px
 * └─────────────────────────────────┘
 */
#include "ui.h"
#include <Arduino.h>

// ── Widgets (persistent) ──────────────────────────────────────────────────────
static lv_obj_t *scr_dash   = nullptr;
static lv_obj_t *lbl_time   = nullptr;
static lv_obj_t *lbl_date   = nullptr;
static lv_obj_t *lbl_fetch  = nullptr;
static lv_obj_t *lbl_price  = nullptr;
static lv_obj_t *lbl_unit   = nullptr;
static lv_obj_t *bar_price  = nullptr;
static lv_obj_t *lbl_trend  = nullptr;
static lv_obj_t *list_next  = nullptr;

// ── Event Callbacks ───────────────────────────────────────────────────────────
static void cb_chart(lv_event_t *e) { ui_show_chart(); }
static void cb_fetch(lv_event_t *e) {
    extern bool needsFetch;
    needsFetch = true;
}

// ── Hilfsfunktionen ───────────────────────────────────────────────────────────
// Findet den Slot für die aktuelle Stunde
static int current_slot_idx() {
    time_t now = time(nullptr);
    struct tm t; localtime_r(&now, &t);
    t.tm_min = 0; t.tm_sec = 0;
    time_t hour_start = mktime(&t);
    for (int i = 0; i < slotCount; i++) {
        if (slots[i].ts == hour_start) return i;
    }
    return -1;
}

// ── Screen erstellen ──────────────────────────────────────────────────────────
void ui_dashboard_create(lv_obj_t *parent) {
    scr_dash = parent;
    lv_obj_set_style_bg_color(scr_dash, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr_dash, LV_OPA_COVER, 0);

    // ─── Header ───────────────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_dash);
    lv_obj_set_size(hdr, 320, 26);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, UI_COLOR_HDR, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);

    lbl_time = lv_label_create(hdr);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_time, UI_COLOR_WHITE, 0);
    lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 6, 0);
    lv_label_set_text(lbl_time, "00:00");

    lbl_date = lv_label_create(hdr);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_date, UI_COLOR_SILVER, 0);
    lv_obj_align(lbl_date, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lbl_date, "01.01.2025");

    lbl_fetch = lv_label_create(hdr);
    lv_obj_set_style_text_font(lbl_fetch, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_fetch, UI_COLOR_CYAN, 0);
    lv_obj_align(lbl_fetch, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_label_set_text(lbl_fetch, LV_SYMBOL_REFRESH);
    lv_obj_add_flag(lbl_fetch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lbl_fetch, cb_fetch, LV_EVENT_CLICKED, nullptr);

    // ─── Linke Spalte: Aktueller Preis ────────────────────────────────────────
    lv_obj_t *card_now = lv_obj_create(scr_dash);
    lv_obj_set_size(card_now, 148, 178);
    lv_obj_align(card_now, LV_ALIGN_TOP_LEFT, 4, 30);
    lv_obj_set_style_bg_color(card_now, UI_COLOR_HDR, 0);
    lv_obj_set_style_bg_opa(card_now, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_now, lv_color_hex(0x2A3F55), 0);
    lv_obj_set_style_border_width(card_now, 1, 0);
    lv_obj_set_style_radius(card_now, 8, 0);
    lv_obj_set_style_pad_all(card_now, 8, 0);

    lv_obj_t *lbl_now_title = lv_label_create(card_now);
    lv_obj_set_style_text_font(lbl_now_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_now_title, UI_COLOR_SILVER, 0);
    lv_obj_align(lbl_now_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(lbl_now_title, "AKTUELL");

    lbl_price = lv_label_create(card_now);
    lv_obj_set_style_text_font(lbl_price, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_price, UI_COLOR_GREEN, 0);
    lv_obj_align(lbl_price, LV_ALIGN_CENTER, 0, -10);
    lv_label_set_text(lbl_price, "--");

    lbl_unit = lv_label_create(card_now);
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_unit, UI_COLOR_SILVER, 0);
    lv_obj_align(lbl_unit, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(lbl_unit, "ct/kWh");

    // Preis-Balken
    bar_price = lv_bar_create(card_now);
    lv_obj_set_size(bar_price, 120, 10);
    lv_obj_align(bar_price, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_bar_set_range(bar_price, -500, 3000);  // -5 bis 30 ct (×100)
    lv_bar_set_value(bar_price, 0, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_price, lv_color_hex(0x2A3F55), 0);
    lv_obj_set_style_bg_color(bar_price, UI_COLOR_GREEN, LV_PART_INDICATOR);

    lbl_trend = lv_label_create(card_now);
    lv_obj_set_style_text_font(lbl_trend, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_trend, UI_COLOR_SILVER, 0);
    lv_obj_align(lbl_trend, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_label_set_text(lbl_trend, "");

    // ─── Rechte Spalte: Nächste Stunden ───────────────────────────────────────
    lv_obj_t *card_next = lv_obj_create(scr_dash);
    lv_obj_set_size(card_next, 160, 178);
    lv_obj_align(card_next, LV_ALIGN_TOP_RIGHT, -4, 30);
    lv_obj_set_style_bg_color(card_next, UI_COLOR_HDR, 0);
    lv_obj_set_style_bg_opa(card_next, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_next, lv_color_hex(0x2A3F55), 0);
    lv_obj_set_style_border_width(card_next, 1, 0);
    lv_obj_set_style_radius(card_next, 8, 0);
    lv_obj_set_style_pad_all(card_next, 6, 0);

    lv_obj_t *lbl_next_title = lv_label_create(card_next);
    lv_obj_set_style_text_font(lbl_next_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_next_title, UI_COLOR_SILVER, 0);
    lv_obj_align(lbl_next_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(lbl_next_title, "NÄCHSTE 5 STUNDEN");

    list_next = lv_obj_create(card_next);
    lv_obj_set_size(list_next, 148, 150);
    lv_obj_align(list_next, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(list_next, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_next, 0, 0);
    lv_obj_set_style_pad_all(list_next, 0, 0);
    lv_obj_set_flex_flow(list_next, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_next, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list_next, 2, 0);

    // ─── Footer ───────────────────────────────────────────────────────────────
    lv_obj_t *ftr = lv_obj_create(scr_dash);
    lv_obj_set_size(ftr, 320, 28);
    lv_obj_align(ftr, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ftr, UI_COLOR_HDR, 0);
    lv_obj_set_style_bg_opa(ftr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ftr, 0, 0);
    lv_obj_set_style_pad_all(ftr, 2, 0);
    lv_obj_set_style_radius(ftr, 0, 0);

    lv_obj_t *btn_chart = lv_btn_create(ftr);
    lv_obj_set_size(btn_chart, 110, 22);
    lv_obj_align(btn_chart, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(btn_chart, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_radius(btn_chart, 4, 0);
    lv_obj_add_event_cb(btn_chart, cb_chart, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *btn_chart_lbl = lv_label_create(btn_chart);
    lv_obj_set_style_text_font(btn_chart_lbl, &lv_font_montserrat_12, 0);
    lv_label_set_text(btn_chart_lbl, "Chart " LV_SYMBOL_RIGHT);
    lv_obj_align(btn_chart_lbl, LV_ALIGN_CENTER, 0, 0);
}

// ── Daten aktualisieren ───────────────────────────────────────────────────────
void ui_dashboard_update() {
    if (!scr_dash) return;

    // Zeit + Datum
    time_t now = time(nullptr);
    struct tm t; localtime_r(&now, &t);
    char tbuf[10], dbuf[14];
    strftime(tbuf, sizeof(tbuf), "%H:%M", &t);
    strftime(dbuf, sizeof(dbuf), "%d.%m.%Y", &t);
    lv_label_set_text(lbl_time, tbuf);
    lv_label_set_text(lbl_date, dbuf);

    // Fetch-Icon
    lv_label_set_text(lbl_fetch, isFetching ? LV_SYMBOL_LOOP : LV_SYMBOL_REFRESH);

    // Aktuellen Slot finden
    int idx = current_slot_idx();
    if (idx < 0 || slotCount == 0) {
        lv_label_set_text(lbl_price, "--");
        return;
    }

    float ct = slots[idx].ct;
    lv_color_t col = price_color(ct);

    // Preis anzeigen
    char pbuf[12];
    if (ct >= 10.0f || ct <= -10.0f)
        snprintf(pbuf, sizeof(pbuf), "%.1f", ct);
    else
        snprintf(pbuf, sizeof(pbuf), "%.2f", ct);
    lv_label_set_text(lbl_price, pbuf);
    lv_obj_set_style_text_color(lbl_price, col, 0);

    // Balken
    int bar_val = (int)(ct * 100);
    lv_bar_set_value(bar_price, bar_val, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_price, col, LV_PART_INDICATOR);

    // Trend (nächste Stunde)
    if (idx + 1 < slotCount) {
        float diff = slots[idx + 1].ct - ct;
        char tbuf2[20];
        snprintf(tbuf2, sizeof(tbuf2), "%+.1f ct", diff);
        lv_label_set_text(lbl_trend, tbuf2);
        lv_obj_set_style_text_color(lbl_trend,
            diff > 0 ? UI_COLOR_RED : UI_COLOR_GREEN, 0);
    }

    // Nächste 5 Stunden Liste
    lv_obj_clean(list_next);
    for (int i = idx + 1; i < slotCount && i <= idx + 5; i++) {
        struct tm st; localtime_r(&slots[i].ts, &st);
        char row[32];
        snprintf(row, sizeof(row), "%02d:00  %+.2f ct", st.tm_hour, slots[i].ct);

        lv_obj_t *row_lbl = lv_label_create(list_next);
        lv_obj_set_style_text_font(row_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(row_lbl, price_color(slots[i].ct), 0);
        lv_label_set_text(row_lbl, row);
        lv_obj_set_width(row_lbl, LV_PCT(100));
    }
}
