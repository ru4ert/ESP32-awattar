// ============================================================
//  aWATTar ESP32 Strompreis Monitor
//  Board : ESP32-2432S028R (Cheap Yellow Display)
//  Author: AI-assisted build
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <time.h>
#include "secrets.h"

// ── Touch-SPI (VSPI – getrennt vom Display) ──────────────────
#define T_CS   33
#define T_IRQ  36
#define T_MOSI 32
#define T_MISO 39
#define T_CLK  25

SPIClass           touchSPI(VSPI);
XPT2046_Touchscreen ts(T_CS, T_IRQ);
TFT_eSPI           tft;

// ── Konstanten ───────────────────────────────────────────────
const char* AWATTAR_URL = "https://api.awattar.at/v1/marketdata";
const char* TZ_VIENNA   = "CET-1CEST,M3.5.0,M10.5.0/3";  // auto DST
const char* NTP1        = "pool.ntp.org";
const char* NTP2        = "at.pool.ntp.org";

#define SCREEN_W  320
#define SCREEN_H  240
#define HDR_H      24
#define FTR_H      26

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

unsigned long lastFetch  = 0;
unsigned long lastMinute = 0;

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

    // Backlight
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    // Display
    tft.init();
    tft.setRotation(1);   // Landscape, USB auf kurzer Seite links
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(C_WHITE, TFT_BLACK);
    tft.drawString("Starte...", 10, 110, 2);

    // Touch (eigener SPI-Bus)
    touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
    ts.begin(touchSPI);
    ts.setRotation(1);

    connectWiFi();
    syncNTP();

    tft.fillScreen(TFT_BLACK);
    tft.drawString("Lade Preise...", 10, 110, 2);
    isFetching = true; drawHeader(); isFetching = false;

    fetchPrices();
    lastFetch = millis();
    needsRedraw = true;
}

// ─────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    if (now - lastFetch >= FETCH_INTERVAL) {
        isFetching = true;
        fetchPrices();
        isFetching  = false;
        lastFetch   = now;
        needsRedraw = true;
    }

    if (now - lastMinute >= MINUTE_TICK) {
        lastMinute  = now;
        needsRedraw = true;
    }

    handleTouch();

    if (needsRedraw) {
        needsRedraw = false;
        if (currentScreen == DASHBOARD) drawDashboard();
        else                             drawChart();
    }

    delay(50);
}

// ─────────────────────────────────────────────────────────────
//  WiFi
// ─────────────────────────────────────────────────────────────
void connectWiFi() {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("WiFi verbinden...", 10, 110, 2);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40)
        delay(500);

    if (WiFi.status() == WL_CONNECTED)
        Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());
    else
        Serial.println("WiFi FEHLER");
}

// ─────────────────────────────────────────────────────────────
//  NTP mit automatischer Sommerzeit (Europe/Vienna)
// ─────────────────────────────────────────────────────────────
void syncNTP() {
    configTzTime(TZ_VIENNA, NTP1, NTP2);
    struct tm ti;
    int tries = 0;
    while (!getLocalTime(&ti) && tries++ < 20) delay(500);
    if (tries < 20) {
        char buf[32]; strftime(buf, sizeof(buf), "%H:%M %d.%m.%Y", &ti);
        Serial.printf("NTP OK: %s\n", buf);
    }
}

// ─────────────────────────────────────────────────────────────
//  API Fetch
// ─────────────────────────────────────────────────────────────
bool fetchPrices() {
    if (WiFi.status() != WL_CONNECTED) { connectWiFi(); }
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    http.begin(AWATTAR_URL);
    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;

    // Heutigen Mitternacht & morgen Mitternacht ermitteln
    struct tm tmNow; getLocalTime(&tmNow);
    tmNow.tm_hour = 0; tmNow.tm_min = 0; tmNow.tm_sec = 0;
    time_t todayMid    = mktime(&tmNow);
    time_t tomorrowMid = todayMid + 86400;
    time_t dayAfter    = todayMid + 172800;

    slotCount     = 0;
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
uint16_t priceColor(float ct) {
    if (ct <= 0)   return 0x03EF; // Blau-Grün (Negativpreis)
    if (ct < 10.0) return C_GREEN;
    if (ct < 20.0) return C_YELLOW;
    return C_RED;
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
}

// ─────────────────────────────────────────────────────────────
//  DASHBOARD
// ─────────────────────────────────────────────────────────────
void drawDashboard() {
    int cur = currentSlotIdx();
    float ct = (cur >= 0) ? slots[cur].ct : 0.0f;
    uint16_t bg = priceColor(ct);

    tft.fillRect(0, HDR_H, SCREEN_W, SCREEN_H - HDR_H - FTR_H, bg);
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
    const char* label = (ct < 10) ? "GUENSTIG" : (ct < 20) ? "MODERAT" : "TEUER";
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

    // Slots holen
    HourSlot* view[24]; int cnt = 0;
    getFilteredSlots(view, cnt);

    if (cnt == 0) {
        tft.setTextColor(C_WHITE, C_BG);
        tft.drawString("Keine Daten", 80, SCREEN_H / 2, 2);
        goto buttons;
    }

    {
        // Min / Max / Avg
        float minP = view[0]->ct, maxP = view[0]->ct, sum = 0;
        for (int i = 0; i < cnt; i++) {
            if (view[i]->ct < minP) minP = view[i]->ct;
            if (view[i]->ct > maxP) maxP = view[i]->ct;
            sum += view[i]->ct;
        }
        float avg   = sum / cnt;
        float range = maxP - minP;
        if (range < 5.0f) range = 5.0f;

        // Y-Achse Labels
        tft.setTextColor(C_GRAY, C_BG);
        char yb[8];
        sprintf(yb, "%+.0f", maxP); tft.drawString(yb, 0, CY,           1);
        sprintf(yb, "%+.0f", minP); tft.drawString(yb, 0, CY + CH - 6,  1);

        // Balken
        float bw = (float)CW / cnt;
        time_t nowT = time(nullptr);
        struct tm* tmN = localtime(&nowT);
        int curHour = tmN->tm_hour;

        for (int i = 0; i < cnt; i++) {
            float pr  = view[i]->ct;
            int   bh  = max(2, (int)((pr - minP) / range * CH));
            int   bx  = CX + (int)(i * bw);
            int   by  = CY + CH - bh;
            int   bwi = max(1, (int)bw - 1);

            uint16_t col = priceColor(pr);
            tft.fillRect(bx, by, bwi, bh, col);

            // Aktueller Slot: weißer Rahmen
            struct tm* st = localtime(&view[i]->ts);
            if (showToday && st->tm_hour == curHour)
                tft.drawRect(bx - 1, by - 1, bwi + 2, bh + 2, C_WHITE);

            // Tooltip-Hervorhebung
            if (i == tooltipIdx)
                tft.drawRect(bx, by, bwi, bh, C_WHITE);

            // X-Labels alle 4 h
            if (i % 4 == 0) {
                char hb[3]; sprintf(hb, "%02d", st->tm_hour);
                tft.setTextColor(C_GRAY, C_BG);
                tft.drawString(hb, bx, CY + CH + 2, 1);
            }
        }

        // Durchschnitts-Linie (gestrichelt)
        int avgY = CY + CH - (int)((avg - minP) / range * CH);
        for (int x = CX; x < CX + CW; x += 6)
            tft.drawFastHLine(x, avgY, 3, C_WHITE);
        char avgb[12]; sprintf(avgb, "o%.1f", avg);
        tft.setTextColor(C_WHITE, C_BG);
        tft.drawString(avgb, CX + CW - 34, avgY - 7, 1);

        // Tooltip
        if (tooltipIdx >= 0 && tooltipIdx < cnt) {
            struct tm* st = localtime(&view[tooltipIdx]->ts);
            char ttb[32];
            sprintf(ttb, "%02d-%02dh: %.1f ct",
                    st->tm_hour, (st->tm_hour + 1) % 24,
                    view[tooltipIdx]->ct);
            tft.fillRect(CX, CY, 200, 13, C_HDR);
            tft.setTextColor(C_YELLOW, C_HDR);
            tft.drawString(ttb, CX + 2, CY + 2, 1);
        }
    }

buttons:
    // ── Footer-Buttons ───────────────────────────────────────
    tft.fillRect(0, SCREEN_H - FTR_H, SCREEN_W, FTR_H, C_HDR);

    // [Heute]
    uint16_t hCol = showToday ? C_WHITE : C_GRAY;
    tft.drawRect(2, SCREEN_H - FTR_H + 2, 70, 22, hCol);
    tft.setTextColor(hCol, C_HDR);
    tft.drawString("Heute", 14, SCREEN_H - FTR_H + 7, 2);

    // [Morgen]
    uint16_t mCol = tomorrowAvail ? (!showToday ? C_WHITE : C_GRAY) : C_GRAY;
    tft.drawRect(76, SCREEN_H - FTR_H + 2, 76, 22, mCol);
    tft.setTextColor(mCol, C_HDR);
    tft.drawString("Morgen", 84, SCREEN_H - FTR_H + 7, 2);

    // [Zurück]
    tft.drawRect(156, SCREEN_H - FTR_H + 2, 80, 22, C_CYAN);
    tft.setTextColor(C_CYAN, C_HDR);
    tft.drawString("< Zurueck", 160, SCREEN_H - FTR_H + 7, 2);
}

// ─────────────────────────────────────────────────────────────
//  TOUCH
// ─────────────────────────────────────────────────────────────
void handleTouch() {
    if (!ts.tirqTouched() || !ts.touched()) return;
    TS_Point p = ts.getPoint();

    // Rohwerte in Bildschirmkoordinaten umrechnen (Kalibrierung CYD Landscape)
    // Rohwerte typisch: x 200–3900, y 200–3900
    int tx = map(p.x, 200, 3900, 0, SCREEN_W);
    int ty = map(p.y, 200, 3900, 0, SCREEN_H);
    tx = constrain(tx, 0, SCREEN_W - 1);
    ty = constrain(ty, 0, SCREEN_H - 1);

    Serial.printf("Touch: raw(%d,%d) -> screen(%d,%d)\n", p.x, p.y, tx, ty);

    delay(200); // Entprellen

    if (currentScreen == DASHBOARD) {
        // Refresh-Button (oben rechts)
        if (tx > SCREEN_W - 50 && ty > HDR_H && ty < HDR_H + 22) {
            isFetching = true; needsRedraw = true;
            if (currentScreen == DASHBOARD) drawHeader();
            fetchPrices();
            isFetching = false;
            lastFetch  = millis();
            needsRedraw = true;
            return;
        }
        // Hauptfläche → Chart
        if (ty > HDR_H && ty < SCREEN_H - FTR_H) {
            currentScreen = CHART;
            tooltipIdx    = -1;
            needsRedraw   = true;
        }

    } else { // CHART
        int btnY = SCREEN_H - FTR_H;

        // Buttons
        if (ty >= btnY) {
            if (tx < 72) {                        // Heute
                showToday = true; tooltipIdx = -1; needsRedraw = true;
            } else if (tx < 152 && tomorrowAvail) { // Morgen
                showToday = false; tooltipIdx = -1; needsRedraw = true;
            } else if (tx >= 156) {               // Zurück
                currentScreen = DASHBOARD; needsRedraw = true;
            }
            return;
        }

        // Balken antippen → Tooltip
        const int CX = 28;
        const int CW = SCREEN_W - CX - 4;
        HourSlot* view[24]; int cnt = 0;
        getFilteredSlots(view, cnt);
        if (cnt > 0 && tx >= CX) {
            int idx = (int)((tx - CX) / ((float)CW / cnt));
            if (idx >= 0 && idx < cnt) {
                tooltipIdx = (tooltipIdx == idx) ? -1 : idx; // Toggle
                needsRedraw = true;
            }
        }
    }
}
