/**
 * ui_chart.cpp – 24h Balken-Chart Screen
 *
 * Layout (320×240):
 * ┌─────────────────────────────────┐
 * │ Header: Heute / Morgen          │ 26px
 * ├─────────────────────────────────┤
 * │                                 │
 * │   Balken-Chart 24 Stunden       │
 * │   (farbkodiert, aktuell hervor) │
 * │                                 │
 * ├─────────────────────────────────┤
 * │ [< Dashboard]  [Morgen >]       │ 28px
 * └─────────────────────────────────┘
 */
#include "ui.h"
#include <Arduino.h>

static lv_obj_t *scr_chart      = nullptr;
static lv_obj_t *lbl_chart_hdr  = nullptr;
static lv_obj_t *chart_obj      = nullptr;
static lv_obj_t *btn_tmrw       = nullptr;
static lv_chart_series_t *ser   = nullptr;
static bool _showToday           = true;

bool ui_chart_is_today() { return _showToday; }

// ── Event Callbacks ───────────────────────────────────────────────────────────
static void cb_back(lv_event_t *e)  { ui_show_dashboard(); }
static void cb_today(lv_event_t *e) { ui_chart_update(true);  }
static void cb_tomorrow(lv_event_t *e) {
    if (!tomorrowAvail) return;  // Daten kommen meist erst ab ~14:00
    ui_chart_update(false);
}

// ── Tooltip: Tap auf Balken zeigt Stunde + Preis im Header ───────────────────
static void chart_pressed_event(lv_event_t *e) {
    uint32_t id = lv_chart_get_pressed_point(chart_obj);
    if (id == LV_CHART_POINT_NONE) return;

    int i = (int)id + (_showToday ? 0 : 24);
    if (i >= slotCount) return;

    struct tm st; localtime_r(&slots[i].ts, &st);
    char buf[40];
    snprintf(buf, sizeof(buf), "%02d-%02d Uhr: %.1f ct",
             st.tm_hour, (st.tm_hour + 1) % 24, slots[i].ct);
    lv_label_set_text(lbl_chart_hdr, buf);
    lv_obj_set_style_text_color(lbl_chart_hdr, price_color(slots[i].ct), 0);
}

// ── Draw-Event für farbige Balken ─────────────────────────────────────────────
static void chart_draw_event(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS) return;

    int offset = _showToday ? 0 : 24;
    int i = dsc->id + offset;
    if (i >= slotCount) return;

    float ct = slots[i].ct;
    lv_color_t col = price_color(ct);
    dsc->rect_dsc->bg_color = col;

    // Aktuelle Stunde hervorheben (nur in der Heute-Ansicht sinnvoll)
    time_t now = time(nullptr);
    struct tm t; localtime_r(&now, &t);
    if (_showToday && dsc->id == t.tm_hour) {
        dsc->rect_dsc->border_color = UI_COLOR_WHITE;
        dsc->rect_dsc->border_width = 2;
    }
}

// ── Screen erstellen ──────────────────────────────────────────────────────────
void ui_chart_create(lv_obj_t *parent) {
    scr_chart = parent;
    lv_obj_set_style_bg_color(scr_chart, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr_chart, LV_OPA_COVER, 0);

    // ─── Header ───────────────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_chart);
    lv_obj_set_size(hdr, 320, 26);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, UI_COLOR_HDR, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);

    lbl_chart_hdr = lv_label_create(hdr);
    lv_obj_set_style_text_font(lbl_chart_hdr, &lv_font_montserrat_de_16, 0);
    lv_obj_set_style_text_color(lbl_chart_hdr, UI_COLOR_WHITE, 0);
    lv_obj_align(lbl_chart_hdr, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lbl_chart_hdr, "HEUTE");

    // ─── Chart ────────────────────────────────────────────────────────────────
    chart_obj = lv_chart_create(scr_chart);
    lv_obj_set_size(chart_obj, 312, 172);
    lv_obj_align(chart_obj, LV_ALIGN_TOP_MID, 0, 30);
    lv_chart_set_type(chart_obj, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(chart_obj, 24);
    lv_chart_set_range(chart_obj, LV_CHART_AXIS_PRIMARY_Y, -500, 3000);  // -5 bis 30 ct (×100)
    lv_chart_set_div_line_count(chart_obj, 5, 0);  // 5 horizontale Linien

    lv_obj_set_style_bg_color(chart_obj, UI_COLOR_HDR, 0);
    lv_obj_set_style_bg_opa(chart_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chart_obj, lv_color_hex(0x2A3F55), 0);
    lv_obj_set_style_border_width(chart_obj, 1, 0);
    lv_obj_set_style_radius(chart_obj, 4, 0);
    lv_obj_set_style_line_color(chart_obj, lv_color_hex(0x1B2A3B), LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart_obj, 4, 0);

    ser = lv_chart_add_series(chart_obj, UI_COLOR_GREEN, LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_add_event_cb(chart_obj, chart_draw_event, LV_EVENT_DRAW_PART_BEGIN, nullptr);
    lv_obj_add_event_cb(chart_obj, chart_pressed_event, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(chart_obj, chart_pressed_event, LV_EVENT_PRESSING, nullptr);

    // ─── Footer ───────────────────────────────────────────────────────────────
    lv_obj_t *ftr = lv_obj_create(scr_chart);
    lv_obj_set_size(ftr, 320, 28);
    lv_obj_align(ftr, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ftr, UI_COLOR_HDR, 0);
    lv_obj_set_style_bg_opa(ftr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ftr, 0, 0);
    lv_obj_set_style_pad_all(ftr, 2, 0);
    lv_obj_set_style_radius(ftr, 0, 0);

    // Zurück-Button
    lv_obj_t *btn_back = lv_btn_create(ftr);
    lv_obj_set_size(btn_back, 110, 22);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x37474F), 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_add_event_cb(btn_back, cb_back, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *btn_back_lbl = lv_label_create(btn_back);
    lv_obj_set_style_text_font(btn_back_lbl, &lv_font_montserrat_de_12, 0);
    lv_label_set_text(btn_back_lbl, LV_SYMBOL_LEFT " Dashboard");
    lv_obj_align(btn_back_lbl, LV_ALIGN_CENTER, 0, 0);

    // Heute/Morgen Toggle
    lv_obj_t *btn_today = lv_btn_create(ftr);
    lv_obj_set_size(btn_today, 52, 22);
    lv_obj_align(btn_today, LV_ALIGN_RIGHT_MID, -62, 0);
    lv_obj_set_style_bg_color(btn_today, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_radius(btn_today, 4, 0);
    lv_obj_add_event_cb(btn_today, cb_today, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_today = lv_label_create(btn_today);
    lv_obj_set_style_text_font(lbl_today, &lv_font_montserrat_de_12, 0);
    lv_label_set_text(lbl_today, "Heute");
    lv_obj_align(lbl_today, LV_ALIGN_CENTER, 0, 0);

    btn_tmrw = lv_btn_create(ftr);
    lv_obj_set_size(btn_tmrw, 58, 22);
    lv_obj_align(btn_tmrw, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(btn_tmrw, lv_color_hex(0x1B5E20), 0);
    lv_obj_set_style_radius(btn_tmrw, 4, 0);
    lv_obj_add_event_cb(btn_tmrw, cb_tomorrow, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_tmrw = lv_label_create(btn_tmrw);
    lv_obj_set_style_text_font(lbl_tmrw, &lv_font_montserrat_de_12, 0);
    lv_label_set_text(lbl_tmrw, "Morgen");
    lv_obj_align(lbl_tmrw, LV_ALIGN_CENTER, 0, 0);
}

// ── Chart-Daten aktualisieren ─────────────────────────────────────────────────
void ui_chart_update(bool showToday) {
    if (!chart_obj || !ser) return;
    _showToday = showToday;

    int offset = showToday ? 0 : 24;
    lv_chart_set_all_value(chart_obj, ser, LV_CHART_POINT_NONE);

    float mn = 0.0f, mx = 5.0f, sum = 0.0f;
    int   cnt = 0;
    for (int i = 0; i < 24 && (offset + i) < slotCount; i++) {
        float ct = slots[offset + i].ct;
        ser->y_points[i] = (int)(ct * 100);
        if (ct < mn) mn = ct;
        if (ct > mx) mx = ct;
        sum += ct; cnt++;
    }

    // Y-Achse dynamisch in 5-ct-Schritten (0 immer enthalten, kein Clipping >30 ct)
    int lo = (int)(floorf(mn / 5.0f) * 500.0f);
    int hi = (int)(ceilf (mx / 5.0f) * 500.0f);
    if (hi <= lo) hi = lo + 500;
    lv_chart_set_range(chart_obj, LV_CHART_AXIS_PRIMARY_Y, lo, hi);

    // Header: Ansicht + Tagesdurchschnitt (Ø kommt aus dem neuen DE-Font)
    char hdr[48];
    if (cnt > 0)
        snprintf(hdr, sizeof(hdr), "%s   Ø %.1f ct",
                 showToday ? "HEUTE" : "MORGEN", sum / cnt);
    else
        snprintf(hdr, sizeof(hdr), "%s   keine Daten",
                 showToday ? "HEUTE" : "MORGEN");
    lv_label_set_text(lbl_chart_hdr, hdr);
    lv_obj_set_style_text_color(lbl_chart_hdr, UI_COLOR_WHITE, 0);

    // Morgen-Button ausgrauen solange die API noch keine Daten liefert
    if (btn_tmrw) {
        if (tomorrowAvail) lv_obj_clear_state(btn_tmrw, LV_STATE_DISABLED);
        else               lv_obj_add_state(btn_tmrw, LV_STATE_DISABLED);
    }

    lv_chart_refresh(chart_obj);
}
