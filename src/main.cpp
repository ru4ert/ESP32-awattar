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
#include <time.h>
#include "secrets.h"

#ifdef SIMULATION
// ── Simulation: FT6206 Capacitive Touch via I2C (board-ili9341-cap-touch) ──
#include <Wire.h>
#include <Adafruit_FT6206.h>
Adafruit_FT6206 ctp;
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

// ── Konstanten ───────────────────────────────────────────────
const char* AWATTAR_URL = "https://api.awattar.at/v1/marketdata";
const char* TZ_VIENNA   = "CET-1CEST,M3.5.0,M10.5.0/3";  // auto DST
const char* NTP1        = "pool.ntp.org";
const char* NTP2        = "at.pool.ntp.org";

// Dynamische Dimensionen – liest echte tft-Werte nach init()+setRotation()
#define SCREEN_W  (tft.width())
#define SCREEN_H  (tft.height())
#define HDR_H      24
#define FTR_H      26

// ── Touch Debug ──────────────────────────────────────────────
#ifdef SIMULATION
#define TOUCH_DEBUG  0   // Koordinaten OK, jetzt echte Touch-Events
#else
#define TOUCH_DEBUG  0   // Hardware: normal
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

    // Backlight
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    // Display
    Serial.println("tft.init()...");
    tft.init();
    Serial.println("tft.init() done");

#ifdef SIMULATION
    tft.setRotation(1);   // ILI9341 Landscape (standard, Wokwi-kompatibel)
#else
    tft.setRotation(3);   // Hardware: Landscape 180° (USB links)
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
    // FT6206 Capacitive Touch via I2C auf D21 (SDA) / D22 (SCL)
    Wire.begin(21, 22);
    if (!ctp.begin(40)) {
        Serial.println("[SIM] FT6206 nicht gefunden!");
    } else {
        Serial.println("[SIM] FT6206 OK");
    }
#else
    touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
    ts.begin(touchSPI);
    ts.setRotation(3);  // Hardware: Landscape 180°
#endif

    connectWiFi();
    syncNTP();

    tft.fillScreen(TFT_BLACK);
    tft.drawString("Lade Preise...", 10, 60, 2);
    isFetching = true; drawHeader(); isFetching = false;

    fetchPrices();
    lastFetch = millis();
    needsRedraw = true;
    Serial.println("=== SETUP DONE ===");
}

// ─────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

#ifdef SIMULATION
    lastActivity = now;  // kein Display-Sleep im Simulator
#endif

    // ── Display-Schlaf─Timer ──
    if (displayOn && now - lastActivity >= SLEEP_TIMEOUT) {
        displayOn     = false;
        sleepTime     = millis();
        currentScreen = DASHBOARD;
        tooltipIdx    = -1;
        digitalWrite(21, LOW);
    }

    if (now - lastFetch >= FETCH_INTERVAL) {
        isFetching = true;
        fetchPrices();
        isFetching  = false;
        lastFetch   = now;
        needsRedraw = true;
    }

    if (displayOn && now - lastMinute >= MINUTE_TICK) {
        lastMinute  = now;
        needsRedraw = true;
    }

    handleTouch();

    if (displayOn && needsRedraw) {
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
#ifdef SIMULATION
    Serial.println("[SIM] WiFi übersprungen");
    return;
#endif
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
#ifdef SIMULATION
    // Fake-Zeit: 2025-06-15 10:00:00 CEST (UTC+2) = 1749981600 UTC
    struct timeval tv = { 1749981600L, 0 };
    settimeofday(&tv, nullptr);
    setenv("TZ", TZ_VIENNA, 1);
    tzset();
    Serial.println("[SIM] Zeit gesetzt: 15.06.2025 10:00 CEST");
    return;
#endif
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
#ifdef SIMULATION
    // Mitternacht des gefakten Tages berechnen
    time_t now = time(nullptr);
    struct tm t; localtime_r(&now, &t);
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
    time_t midnight = mktime(&t);

    // Realistische Österreich-Preise in ct/kWh für 48h
    const float prices[48] = {
        // Heute (Stunden 00-23)
        -2.1f, -1.5f, -0.8f,  0.5f,  1.2f,  2.3f,   // 00-05
         4.1f,  8.7f, 12.4f, 10.2f,  8.9f,  7.6f,   // 06-11
         9.0f,  7.2f,  9.1f, 14.3f, 18.7f, 22.1f,   // 12-17
        19.8f, 15.4f, 12.1f,  9.8f,  7.2f,  4.5f,   // 18-23
        // Morgen (Stunden 00-23)
         1.2f, -0.5f, -1.8f, -2.4f, -1.1f,  0.9f,   // 00-05
         3.7f,  7.8f, 11.2f,  9.4f,  8.1f,  6.9f,   // 06-11
         7.4f,  8.8f, 11.6f, 16.9f, 21.3f, 25.4f,   // 12-17
        22.6f, 17.8f, 13.2f, 10.4f,  8.3f,  5.1f    // 18-23
    };

    slotCount     = 0;
    tomorrowAvail = false;
    for (int i = 0; i < 48; i++) {
        slots[i].ts = midnight + (time_t)(i * 3600);
        slots[i].ct = prices[i];
        slotCount++;
        if (i >= 24) tomorrowAvail = true;
    }
    Serial.printf("[SIM] %d Mock-Slots geladen, morgen=%d\n", slotCount, tomorrowAvail);
    return true;
#endif
    if (WiFi.status() != WL_CONNECTED) { connectWiFi(); }
    if (WiFi.status() != WL_CONNECTED) return false;

    // Zeitfenster: heute Mitternacht bis übermorgen Mitternacht
    struct tm tmNow2; getLocalTime(&tmNow2);
    tmNow2.tm_hour = 0; tmNow2.tm_min = 0; tmNow2.tm_sec = 0;
    long long startMs = (long long)mktime(&tmNow2) * 1000LL;
    long long endMs   = startMs + 172800000LL;  // +48h

    char apiUrl[128];
    snprintf(apiUrl, sizeof(apiUrl),
             "%s?start=%lld&end=%lld", AWATTAR_URL, startMs, endMs);

    HTTPClient http;
    http.begin(apiUrl);
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
        float avg = sum / cnt;

        // Feste Y-Achse in 5ct-Schritten (inkl. 0 wenn nötig)
        float yMin   = floorf(min(minP, 0.0f) / 5.0f) * 5.0f;
        float yMax   = ceilf(maxP / 5.0f) * 5.0f;
        if (yMax <= yMin) yMax = yMin + 5.0f;
        float yRange = yMax - yMin;

        // Y-Achse: Grid-Linien + Labels alle 5ct
        char yb[8];
        for (float yv = yMin; yv <= yMax + 0.1f; yv += 5.0f) {
            int yPx = CY + CH - (int)((yv - yMin) / yRange * CH);
            if (yPx < CY || yPx > CY + CH) continue;
            uint16_t gc = (fabsf(yv) < 0.1f) ? C_YELLOW : C_GRAY;
            for (int x = CX; x < CX + CW; x += 8) tft.drawPixel(x, yPx, gc);
            sprintf(yb, "%d", (int)yv);
            tft.setTextColor(gc, C_BG);
            tft.drawString(yb, 0, yPx - 4, 1);
        }

        // Balken
        float bw = (float)CW / cnt;
        time_t nowT = time(nullptr);
        struct tm* tmN = localtime(&nowT);
        int curHour = tmN->tm_hour;
        int curMin  = tmN->tm_min;

        for (int i = 0; i < cnt; i++) {
            float pr  = view[i]->ct;
            int   bh  = max(2, (int)((pr - yMin) / yRange * CH));
            int   bx  = CX + (int)(i * bw);
            int   by  = CY + CH - bh;
            int   bwi = max(1, (int)bw - 1);

            uint16_t col     = barColor(pr);
            uint16_t fillCol = col;
            if (i == tooltipIdx) {
                uint8_t r = min(31, ((col >> 11) & 0x1F) + 8);
                uint8_t g = min(63, ((col >> 5)  & 0x3F) + 16);
                uint8_t b = min(31, (col & 0x1F) + 8);
                fillCol = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
            }
            tft.fillRect(bx, by, bwi, bh, fillCol);

            struct tm* st = localtime(&view[i]->ts);
            if (showToday && st->tm_hour == curHour)
                tft.drawRect(bx - 1, by - 1, bwi + 2, bh + 2, C_WHITE);
            if (i == tooltipIdx)
                tft.drawRect(bx - 1, by - 1, bwi + 2, bh + 2, C_YELLOW);

            if (i % 4 == 0) {
                char hb[3]; sprintf(hb, "%02d", st->tm_hour);
                tft.setTextColor(C_GRAY, C_BG);
                tft.drawString(hb, bx, CY + CH + 2, 1);
            }
        }

        // Aktuelle Zeit: senkrechter Cyan-Strich (heute, auf 15 min gerundet)
        if (showToday) {
            for (int i = 0; i < cnt; i++) {
                struct tm* si = localtime(&view[i]->ts);
                if (si->tm_hour == curHour) {
                    int bx   = CX + (int)(i * bw);
                    int qMin = (curMin / 15) * 15;
                    int nowX = bx + (int)(qMin / 60.0f * bw);
                    for (int y = CY; y < CY + CH; y += 4)
                        tft.drawFastVLine(nowX, y, 2, C_CYAN);
                    break;
                }
            }
        }

        // Durchschnitts-Linie (gestrichelt weiß)
        int avgY = CY + CH - (int)((avg - yMin) / yRange * CH);
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
    int tx, ty;

#ifdef SIMULATION
    // FT6206 Capacitive Touch (I2C)
    if (!ctp.touched()) return;
    TS_Point p = ctp.getPoint();
    // FT6206: Pixel-Koordinaten 0-240 / 0-320, bei Landscape x↔y tauschen
    tx = map(p.y, 0, 320, 0, SCREEN_W);
    ty = map(p.x, 240, 0, 0, SCREEN_H);  // invertiert: oben/unten korrigiert
#else
    // XPT2046 Resistive Touch (SPI)
    if (!ts.tirqTouched() || !ts.touched()) return;
    TS_Point p = ts.getPoint();
    tx = map(p.x, 3790, 210, 0, SCREEN_W);
    ty = map(p.y, 3900, 245, 0, SCREEN_H);
#endif

    tx = constrain(tx, 0, SCREEN_W - 1);
    ty = constrain(ty, 0, SCREEN_H - 1);
    Serial.printf("Touch: raw(%d,%d) -> screen(%d,%d)\n", p.x, p.y, tx, ty);

#if TOUCH_DEBUG
    tft.fillCircle(tx, ty, 6, C_RED);
    tft.drawCircle(tx, ty, 7, C_WHITE);
    char dbgBuf[40];
    sprintf(dbgBuf, "raw(%d,%d)", p.x, p.y);
    tft.fillRect(0, SCREEN_H - FTR_H - 14, 160, 14, C_HDR);
    tft.setTextColor(C_YELLOW, C_HDR);
    tft.drawString(dbgBuf, 2, SCREEN_H - FTR_H - 13, 1);
    sprintf(dbgBuf, "px(%d,%d)", tx, ty);
    tft.fillRect(0, SCREEN_H - FTR_H - 7, 120, 8, C_HDR);
    tft.drawString(dbgBuf, 2, SCREEN_H - FTR_H - 6, 1);
    delay(500);
    return;
#endif

    // Sleep-Button (Header, neben WiFi)
    if (ty < HDR_H && tx > SCREEN_W - 52 && tx < SCREEN_W - 32) {
        displayOn = !displayOn;
        if (displayOn) {
            lastActivity = millis();
            digitalWrite(21, HIGH);
            needsRedraw = true;
        } else {
            sleepTime     = millis();
            currentScreen = DASHBOARD;
            tooltipIdx    = -1;
            digitalWrite(21, LOW);
        }
        return;
    }

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
            } else if (tx < 152) {                // Morgen
                if (tomorrowAvail) {
                    showToday = false; tooltipIdx = -1; needsRedraw = true;
                } else {
                    showToast("Morgen noch nicht verfuegbar");
                }
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
