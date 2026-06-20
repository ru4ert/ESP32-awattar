// ============================================================
//  aWATTar ESP32 Strompreis Monitor – LVGL Edition
//  Board : ESP32-2432S028R (Cheap Yellow Display)
//  UI    : LVGL v8.3
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <time.h>
#include "secrets.h"
#include "lv_drivers.h"
#include "ui.h"

#ifdef SIMULATION
// ── Simulation: FT6206 Capacitive Touch via I2C (board-ili9341-cap-touch) ──
#include <Wire.h>
#include <Adafruit_FT6206.h>
Adafruit_FT6206 ctp;
bool ctpAvailable = false;
#else
// ── Hardware: XPT2046 Resistive Touch via SPI ────────────────────────────────
#include <XPT2046_Touchscreen.h>
#define T_CS   33
#define T_IRQ  36
#define T_MOSI 32
#define T_MISO 39
#define T_CLK  25
SPIClass           touchSPI(VSPI);
XPT2046_Touchscreen ts(T_CS, T_IRQ);
#endif
TFT_eSPI           tft;

// ── Interner State ────────────────────────────────────────────────────────────
bool needsFetch = false;  // Touch-Refresh-Button setzt dieses Flag

static lv_obj_t *scr_dashboard = nullptr;
static lv_obj_t *scr_chart     = nullptr;

TFT_eSPI tft;

// ── Konstanten ────────────────────────────────────────────────────────────────
const char* AWATTAR_URL = "https://api.awattar.at/v1/marketdata";
const char* TZ_VIENNA   = "CET-1CEST,M3.5.0,M10.5.0/3";
const char* NTP1        = "pool.ntp.org";
const char* NTP2        = "at.pool.ntp.org";

#define FETCH_INTERVAL  3600000UL
#define UPDATE_INTERVAL    1000UL   // UI jede Sekunde aktualisieren

// ── Touch Debug ──────────────────────────────────────────────
#ifdef SIMULATION
#define TOUCH_DEBUG  0
#else
#define TOUCH_DEBUG  0   // Kalibrierung abgeschlossen
#endif

#define FETCH_INTERVAL 3600000UL   // 1 h in ms
#define MINUTE_TICK      60000UL

// RGB565 Farben
#define C_GREEN   0x07E0
#define C_YELLOW  0xFFE0
#define C_RED     0xF800
#define C_HDR     0x1082
#define C_BG      0x0861
#define C_GRAY    0x4208
#define C_WHITE   0xFFFF
#define C_CYAN    0x07FF
#define C_SILVER  0xBDF7  // Hellgrau (bekannt funktionierend)
#define C_DKGREEN 0x03E0  // Dunkelgrün = C_GREEN bei halber Helligkeit

// ── Daten ────────────────────────────────────────────────────
struct HourSlot { time_t ts; float ct; };  // ct/kWh

#define MAX_SLOTS 48
HourSlot  slots[MAX_SLOTS];
int       slotCount    = 0;
bool      tomorrowAvail = false;
bool      isFetching    = false;

// ── UI-Zustand ───────────────────────────────────────────────
enum Screen { DASHBOARD, CHART };
Screen currentScreen = DASHBOARD;
bool   showToday     = true;
bool   needsRedraw   = true;
int    tooltipIdx    = -1;

unsigned long lastFetch    = 0;
unsigned long lastMinute   = 0;
unsigned long lastActivity = 0;
unsigned long sleepTime    = 0;
bool          displayOn    = true;

#define SLEEP_TIMEOUT  30000UL  // 30 Sekunden

// ── Prototypen ───────────────────────────────────────────────
void     connectWiFi();
void     syncNTP();
bool     fetchPrices();
void     drawHeader();
void     drawDashboard();
void     drawChart();
void     handleTouch();
uint16_t priceColor(float ct);
int      currentSlotIdx();
void     getFilteredSlots(HourSlot** out, int& cnt);

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("=== SETUP START ===");

    // Backlight – NUR auf echter Hardware, nicht im Simulator!
    // Im Simulator ist GPIO21 = SDA (I2C für FT6206 Touch)
#ifndef SIMULATION
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);
#endif

#ifdef SIMULATION
    // ── I2C MUSS vor tft.init() initialisiert werden! (Wokwi requirement) ──
    // FT6206 I2C auf D21 (SDA) / D22 (SCL)
    Wire.begin(21, 22);
    delay(50);
    Serial.println("[SIM] Wire.begin(21,22) done");
#endif

    // Display
    Serial.println("tft.init()...");
    tft.init();
    Serial.println("tft.init() done");

#ifdef SIMULATION
    tft.setRotation(1);   // ILI9341 Landscape (standard, Wokwi-kompatibel)
#else
    tft.setRotation(0);   // Hardware: Landscape (CYD HW-458)
    tft.fillScreen(TFT_BLACK);  // Residual VRAM löschen (ILI9341 Quirk)
#endif

    Serial.printf("Display: %d x %d px\n", tft.width(), tft.height());

#ifdef SIMULATION
    // Farb-Blitz um zu prüfen ob SPI funktioniert
    Serial.println("Farb-Test...");
    tft.fillScreen(0xF800);   // ROT
    delay(300);
    tft.fillScreen(0x07E0);   // GRÜN
    delay(300);
    tft.fillScreen(TFT_BLACK);
    Serial.println("Farb-Test fertig");
#endif

    tft.setTextColor(C_WHITE, TFT_BLACK);
    tft.drawString("Starte...", 10, 60, 2);
    Serial.println("drawString done");

    // Touch initialisieren
#ifdef SIMULATION
    // FT6206 auf I2C (Wire wurde bereits oben gestartet)
    ctpAvailable = ctp.begin(40);
    if (!ctpAvailable) {
        Serial.println("[SIM] FT6206 nicht gefunden - Touch deaktiviert");
    } else {
        Serial.println("[SIM] FT6206 OK");
    }
#else
    touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
    ts.begin(touchSPI);
    ts.setRotation(0);  // Hardware: test rotation 0
#endif

    connectWiFi();
    syncNTP();

    tft.fillScreen(TFT_BLACK);
    tft.drawString("Lade Preise...", 10, 60, 2);
    isFetching = true; drawHeader(); isFetching = false;

    fetchPrices();
    lastFetch    = millis();
    lastActivity = millis();  // FIX: verhindert sofortigen Display-Sleep nach Boot
    needsRedraw  = true;
    Serial.println("=== SETUP DONE ===");
}

void ui_show_chart() {
    ui_chart_update(true);  // Standard: Heute
    lv_scr_load_anim(scr_chart, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
void connectWiFi() {
#ifdef SIMULATION
    Serial.println("[SIM] WiFi übersprungen");
    return;
#endif
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) delay(500);
    Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "OK" : "FEHLER");
}

// ── NTP ───────────────────────────────────────────────────────────────────────
void syncNTP() {
#ifdef SIMULATION
    struct timeval tv = { 1749981600L, 0 };  // 2025-06-15 10:00 CEST
    settimeofday(&tv, nullptr);
    setenv("TZ", TZ_VIENNA, 1);
    tzset();
    Serial.println("[SIM] Zeit: 15.06.2025 10:00 CEST");
    return;
#endif
    configTzTime(TZ_VIENNA, NTP1, NTP2);
    struct tm ti;
    int tries = 0;
    while (!getLocalTime(&ti) && tries++ < 20) delay(500);
}

// ── API Fetch ─────────────────────────────────────────────────────────────────
bool fetchPrices() {
#ifdef SIMULATION
    time_t now = time(nullptr);
    struct tm t; localtime_r(&now, &t);
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
    time_t midnight = mktime(&t);

    const float prices[48] = {
        // Heute
        -2.1f, -1.5f, -0.8f,  0.5f,  1.2f,  2.3f,
         4.1f,  8.7f, 12.4f, 10.2f,  8.9f,  7.6f,
         6.8f,  7.2f,  9.1f, 14.3f, 18.7f, 22.1f,
        19.8f, 15.4f, 12.1f,  9.8f,  7.2f,  4.5f,
        // Morgen
         1.2f, -0.5f, -1.8f, -2.4f, -1.1f,  0.9f,
         3.7f,  7.8f, 11.2f,  9.4f,  8.1f,  6.9f,
         5.8f,  6.3f,  8.2f, 13.1f, 16.9f, 20.5f,
        17.3f, 13.8f, 10.9f,  8.4f,  5.7f,  3.2f,
    };
    slotCount = 48;
    tomorrowAvail = true;
    for (int i = 0; i < 48; i++) {
        slots[i].ts = midnight + i * 3600;
        slots[i].ct = prices[i];
    }
    Serial.println("[SIM] Mock-Preise geladen");
    return true;
#else
    // ── Echter API-Call ────────────────────────────────────────────────────────
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    time_t now = time(nullptr);
    char url[128];
    snprintf(url, sizeof(url), "%s?start=%llu000&end=%llu000",
             AWATTAR_URL, (unsigned long long)(now / 3600 * 3600),
             (unsigned long long)(now / 3600 * 3600 + 48 * 3600));

    http.begin(url);
    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    JsonDocument doc;
    if (deserializeJson(doc, http.getStream())) { http.end(); return false; }
    http.end();

    slotCount = 0;
    tomorrowAvail = false;

    for (JsonObject e : doc["data"].as<JsonArray>()) {
        if (slotCount >= MAX_SLOTS) break;
        time_t ts = (time_t)(e["start_timestamp"].as<long long>() / 1000LL);
        if (ts < todayMid || ts >= dayAfter) continue;

        slots[slotCount].ts = ts;
        slots[slotCount].ct = e["marketprice"].as<float>() / 10.0f; // EUR/MWh → ct/kWh
        slotCount++;
        if (ts >= tomorrowMid) tomorrowAvail = true;
    }

    Serial.printf("Slots geladen: %d, Morgen: %d\n", slotCount, tomorrowAvail);
    return true;
}

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────
// Dashboard-Hintergrund
uint16_t priceColor(float ct) {
    if (ct < 0)    return 0x0340;  // Dunkelgrün (Negativ)
    if (ct < 5.0)  return C_GREEN; // Helles Grün
    if (ct < 15.0) return 0x4208;  // Dunkelgrau (Mittel)
    return C_RED;                   // Rot (Teuer)
}

// Chart-Balken
uint16_t barColor(float ct) {
    if (ct < 0)    return C_CYAN;   // Negativ  → erscheint blau
    if (ct < 5.0)  return C_GREEN;  // Günstig  → erscheint grün
    if (ct < 15.0) return C_GRAY;   // Mittel   → erscheint grau ✓
    return C_RED;                    // Teuer    → erscheint gelb (Hardware-Limit)
}

int currentSlotIdx() {
    time_t now = time(nullptr);
    for (int i = slotCount - 1; i >= 0; i--)
        if (slots[i].ts <= now) return i;
    return -1;
}

// Filtert Slots für Heute/Morgen-Ansicht
void getFilteredSlots(HourSlot** out, int& cnt) {
    struct tm tmNow; getLocalTime(&tmNow);
    tmNow.tm_hour = 0; tmNow.tm_min = 0; tmNow.tm_sec = 0;
    time_t tomorrowMid = mktime(&tmNow) + 86400;

    cnt = 0;
    for (int i = 0; i < slotCount && cnt < 24; i++) {
        bool isToday = slots[i].ts < tomorrowMid;
        if ((showToday && isToday) || (!showToday && !isToday))
            out[cnt++] = &slots[i];
    }
}

// ─────────────────────────────────────────────────────────────
//  Toast-Overlay (2 Sekunden blockierend)
// ─────────────────────────────────────────────────────────────
void showToast(const char* msg) {
    const int TW = 220, TH = 32;
    int tx = (SCREEN_W - TW) / 2;
    int ty = (SCREEN_H - TH) / 2;
    // Hintergrund + Rahmen
    tft.fillRoundRect(tx, ty, TW, TH, 6, 0x2945);
    tft.drawRoundRect(tx, ty, TW, TH, 6, C_YELLOW);
    // Text zentriert
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_YELLOW, 0x2945);
    tft.drawString(msg, SCREEN_W / 2, SCREEN_H / 2, 2);
    tft.setTextDatum(TL_DATUM);
    delay(2000);
    needsRedraw = true;
}

// ─────────────────────────────────────────────────────────────
//  Header (auf beiden Screens)
// ─────────────────────────────────────────────────────────────
void drawHeader() {
    tft.fillRect(0, 0, SCREEN_W, HDR_H, C_HDR);

    // Uhrzeit
    struct tm ti; getLocalTime(&ti);
    char buf[9]; sprintf(buf, "%02d:%02d", ti.tm_hour, ti.tm_min);
    tft.setTextColor(C_WHITE, C_HDR);
    tft.drawString(buf, 6, 5, 2);

    // WLAN-Balken
    int rssi = WiFi.RSSI();
    int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
    for (int i = 0; i < 4; i++) {
        int bh = 4 + i * 4;
        tft.fillRect(SCREEN_W - 28 + i * 6, HDR_H - 2 - bh, 4, bh,
                     i < bars ? C_WHITE : C_GRAY);
    }

    // Lade-Spinner
    if (isFetching) {
        tft.setTextColor(C_YELLOW, C_HDR);
        tft.drawString("~", SCREEN_W / 2 - 4, 5, 2);
    }

    // Sleep-Button (Power-Icon) links neben WiFi-Balken
    uint16_t sleepCol = displayOn ? C_WHITE : C_YELLOW;
    int sx = SCREEN_W - 46;  // Mitte des Icons
    tft.drawCircle(sx, 11, 5, sleepCol);
    tft.drawFastVLine(sx, 6, 4, sleepCol);   // Strich oben
}

// ─────────────────────────────────────────────────────────────
//  DASHBOARD
// ─────────────────────────────────────────────────────────────
void drawDashboard() {
    int cur = currentSlotIdx();
    float ct = (cur >= 0) ? slots[cur].ct : 0.0f;
    uint16_t bg = priceColor(ct);

    // Ganzen Screen löschen (verhindert Artefakte)
    tft.fillScreen(bg);
    drawHeader();

    // Großer Preis (Font 7 = 7-Segment)
    char pBuf[10];
    sprintf(pBuf, ct < 0 ? "%.1f" : "%.1f", ct);
    tft.setTextColor(C_WHITE, bg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(pBuf, SCREEN_W / 2, HDR_H + 65, 7);

    // Einheit
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_WHITE, bg);
    tft.drawString("ct / kWh", SCREEN_W / 2, HDR_H + 120, 2);

    // Preis-Label (Günstig/Moderat/Teuer)
    const char* label = (ct < 5) ? "GUENSTIG" : (ct < 15) ? "MODERAT" : "TEUER";
    tft.setTextColor(C_WHITE, bg);
    tft.drawString(label, SCREEN_W / 2, HDR_H + 140, 2);

    // Refresh-Button
    tft.setTextDatum(TL_DATUM);
    tft.drawRect(SCREEN_W - 50, HDR_H + 4, 46, 18, C_WHITE);
    tft.setTextColor(C_WHITE, bg);
    tft.drawString("Refresh", SCREEN_W - 48, HDR_H + 7, 1);

    // Footer – Trend
    tft.fillRect(0, SCREEN_H - FTR_H, SCREEN_W, FTR_H, C_HDR);
    tft.setTextColor(C_WHITE, C_HDR);
    if (cur >= 0 && cur + 1 < slotCount) {
        float nextCt = slots[cur + 1].ct;
        char fBuf[48];
        sprintf(fBuf, "Naechste Std: %.1f ct %s", nextCt,
                nextCt > ct ? " (steigt ^)" : " (sinkt v)");
        tft.drawString(fBuf, 8, SCREEN_H - FTR_H + 8, 2);
    } else {
        tft.drawString("Kein Trend verfuegbar", 8, SCREEN_H - FTR_H + 8, 2);
    }

    // Tap-Hint
    tft.setTextColor(C_GRAY, bg);
    tft.drawString("Tippen fuer Chart ->", 8, SCREEN_H - FTR_H - 14, 1);
}

// ─────────────────────────────────────────────────────────────
//  CHART
// ─────────────────────────────────────────────────────────────
void drawChart() {
    tft.fillScreen(C_BG);
    drawHeader();

    // Layout
    const int CX = 28, CY = HDR_H + 8;
    const int CW = SCREEN_W - CX - 4;
    const int CH = SCREEN_H - HDR_H - FTR_H - 20;
    // Rechten Rand explizit löschen (ILI9341 zeigt manchmal Artefakte)
    tft.fillRect(CX + CW, CY, SCREEN_W - (CX + CW), CH, C_BG);

    // Slots holen
    HourSlot* view[24]; int cnt = 0;
    getFilteredSlots(view, cnt);

    if (cnt == 0) {
        tft.setTextColor(C_WHITE, C_BG);
        tft.drawString("Keine Daten", 80, SCREEN_H / 2, 2);
        goto buttons;
    }

    {
        struct tm t2; localtime_r(&now, &t2);
        t2.tm_hour = 0; t2.tm_min = 0; t2.tm_sec = 0;
        today_midnight = mktime(&t2);
    }

    for (JsonObject item : doc["data"].as<JsonArray>()) {
        if (slotCount >= MAX_SLOTS) break;
        time_t ts = (time_t)(item["start_timestamp"].as<long long>() / 1000);
        float ct  = item["marketprice"].as<float>() / 10.0f;
        slots[slotCount++] = { ts, ct };
        if (ts >= today_midnight + 24 * 3600) tomorrowAvail = true;
    }
    return slotCount > 0;
#endif
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("=== aWATTar LVGL Start ===");

#ifndef SIMULATION
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);  // Backlight an
#endif

    // Display init
    tft.init();
#ifdef SIMULATION
    // FT6206 Capacitive Touch (I2C)
    if (!ctpAvailable || !ctp.touched()) return;
    TS_Point p = ctp.getPoint();
    // FT6206: Pixel-Koordinaten 0-240 / 0-320, bei Landscape x↔y tauschen
    tx = map(p.y, 0, 320, 0, SCREEN_W);
    ty = map(p.x, 240, 0, 0, SCREEN_H);  // invertiert: oben/unten korrigiert
#else
    // XPT2046 Resistive Touch (SPI)
    if (!ts.tirqTouched() || !ts.touched()) return;
    TS_Point p = ts.getPoint();
    tx = map(p.y, 245, 3900, 0, SCREEN_W);   // raw.Y → screen.X (links/rechts)
    ty = map(p.x, 3790, 210, 0, SCREEN_H);   // raw.X → screen.Y (oben/unten, invertiert)
#endif

    // FIX: lastActivity bei JEDEM Touch aktualisieren (auch bei raw(0,0))
    lastActivity = millis();

    // Display aufwecken falls es schläft
    if (!displayOn) {
        displayOn   = true;
        digitalWrite(21, HIGH);
        needsRedraw = true;
        return;  // erstes Touch nur zum Aufwecken
    }

    tx = constrain(tx, 0, SCREEN_W - 1);
    ty = constrain(ty, 0, SCREEN_H - 1);
    Serial.printf("Touch: raw(%d,%d) -> screen(%d,%d)\n", p.x, p.y, tx, ty);

    // Screens erstellen
    scr_dashboard = lv_obj_create(nullptr);
    scr_chart     = lv_obj_create(nullptr);

    ui_dashboard_create(scr_dashboard);
    ui_chart_create(scr_chart);

    // Dashboard als Start-Screen laden
    lv_scr_load(scr_dashboard);

    // WiFi + Zeit + erste Preise holen
    connectWiFi();
    syncNTP();
    isFetching = true; lvgl_update();
    fetchPrices();
    isFetching  = false;
    lastFetch   = millis();

    ui_dashboard_update();
    Serial.println("=== SETUP DONE ===");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    // LVGL Tasks (Touch + Redraw)
    lvgl_update();

    // UI jede Sekunde aktualisieren (Zeit, Preis)
    if (now - lastUpdate >= UPDATE_INTERVAL) {
        lastUpdate = now;
        if (lv_scr_act() == scr_dashboard) {
            ui_dashboard_update();
        }
    }

    // Stündlicher Fetch
    if (now - lastFetch >= FETCH_INTERVAL || needsFetch) {
        needsFetch  = false;
        isFetching  = true;
        fetchPrices();
        isFetching  = false;
        lastFetch   = now;
        ui_dashboard_update();
    }

    delay(5);  // LVGL braucht ~5ms Tick-Auflösung
}
