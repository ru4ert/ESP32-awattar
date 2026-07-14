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
#include <math.h>
#include "secrets.h"
#include "panel_color.h"          // PANEL(): Farb-Kompensation fürs Clone-Panel
#include "img_emoji.h"            // 🔋 / 🛢 als RGB565-Bitmaps (24x24)
#include "font_de.h"              // Montserrat mit Umlauten/Ø/€ (eigener Renderer)
#include "tarif.h"                // Börse → Gesamtpreis (Netz, SNAP, Abgaben, USt)
#include "qrcode.h"               // QR-Code für den Doku-Link (Detail-Ansicht)

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

// Strommix-API (Fraunhofer ISE, kein API-Key); "at" ↔ "de" je Standort
const char* ENERGY_URL     = "https://api.energy-charts.info/public_power";
const char* ENERGY_COUNTRY = "at";

// Doku-Seite zur Preiszusammensetzung (QR in der Detail-Ansicht).
// GitHub Pages: Repo-Settings → Pages → Branch main, Ordner /docs
// Max. 42 Zeichen (QR Version 3, ECC M)!
const char* DOKU_URL = "https://ru4ert.github.io/ESP32-awattar/";

// Dynamische Dimensionen – liest echte tft-Werte nach init()+setRotation()
#define SCREEN_W  (tft.width())
#define SCREEN_H  (tft.height())
#define HDR_H      24
#define FTR_H      26

// ── Touch Debug ──────────────────────────────────────────────
#define TOUCH_DEBUG  0   // 0 = normal, 1 = Debug-Punkt

#define FETCH_INTERVAL 3600000UL   // 1 h in ms
#define MINUTE_TICK      60000UL

// Ab diesem Erneuerbaren-Anteil (%) gilt der Strom als "Öko" (🔋 statt 🛢)
#define RENEW_THRESHOLD  75

// RGB565 Farben – als ECHTE Zielfarben notiert, PANEL() kompensiert das
// invertierende/R-B-tauschende Clone-Panel (siehe include/panel_color.h)
#define C_GREEN   PANEL(0x07E0)
#define C_YELLOW  PANEL(0xFFE0)
#define C_RED     PANEL(0xF800)
#define C_HDR     PANEL(0x1082)
#define C_BG      PANEL(0x0861)
#define C_GRAY    PANEL(0x4208)
#define C_WHITE   PANEL(0xFFFF)
#define C_BLACK   PANEL(0x0000)
#define C_CYAN    PANEL(0x07FF)
#define C_SILVER  PANEL(0xBDF7)
#define C_DKGREEN PANEL(0x03E0)
#define C_SNAPBG  PANEL(0x11C3)  // dezentes Dunkelgrün: SNAP-Fenster im Chart

// ── Daten ────────────────────────────────────────────────────
struct HourSlot { time_t ts; float ct; };  // ct/kWh

#define MAX_SLOTS 48
HourSlot  slots[MAX_SLOTS];
int       slotCount    = 0;
bool      tomorrowAvail = false;
bool      isFetching    = false;
int       renewShare    = -1;     // Anteil Erneuerbare in % (energy-charts)
bool      renewValid    = false;

// ── UI-Zustand ───────────────────────────────────────────────
enum Screen { DASHBOARD, CHART, DETAIL };
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
void     drawDetail();
void     handleTouch();
uint16_t priceColor(float totalCt, float spotCt);
int      currentSlotIdx();
void     getFilteredSlots(HourSlot** out, int& cnt);
void     fetchRenewableShare();

// ─────────────────────────────────────────────────────────────
//  Eigener UTF-8-Font-Renderer (Fonts: font_de.h)
//  Antialiasing durch Blending gegen die bekannte Hintergrundfarbe.
//  y ist immer die BASELINE, nicht die Oberkante!
// ─────────────────────────────────────────────────────────────
static uint32_t utf8Next(const char*& s) {
    uint8_t c = (uint8_t)*s;
    if (!c) return 0;
    s++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && ((uint8_t)*s & 0xC0) == 0x80)
        return ((uint32_t)(c & 0x1F) << 6) | ((uint8_t)*s++ & 0x3F);
    if ((c & 0xF0) == 0xE0 && ((uint8_t)s[0] & 0xC0) == 0x80
                           && ((uint8_t)s[1] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(c & 0x0F) << 12)
                    | (((uint32_t)((uint8_t)s[0] & 0x3F)) << 6)
                    |  ((uint8_t)s[1] & 0x3F);
        s += 2;
        return cp;
    }
    return '?';  // ungültige Sequenz
}

static const DEGlyph* deGlyph(const DEFont& f, uint32_t cp) {
    for (uint16_t i = 0; i < f.count; i++)
        if (f.glyphs[i].cp == cp) return &f.glyphs[i];
    return nullptr;
}

int deWidth(const DEFont& f, const char* txt) {
    int w = 0; uint32_t cp;
    const char* s = txt;
    while ((cp = utf8Next(s)) != 0) {
        const DEGlyph* g = deGlyph(f, cp);
        if (!g) g = deGlyph(f, '?');
        if (g) w += g->adv;
    }
    return w;
}

void drawDE(int x, int yBase, const char* txt, const DEFont& f,
            uint16_t fg, uint16_t bgCol) {
    // fg/bg sind PANEL-kompensiert; fürs Blending zurück in Zielfarben
    // (PANEL ist selbstinvers), am Ende pro Pixel wieder kompensieren.
    uint16_t fgT = PANEL(fg), bgT = PANEL(bgCol);
    uint8_t fr = (fgT >> 11) << 3, fg8 = ((fgT >> 5) & 0x3F) << 2, fb = (fgT & 0x1F) << 3;
    uint8_t br = (bgT >> 11) << 3, bg8 = ((bgT >> 5) & 0x3F) << 2, bb = (bgT & 0x1F) << 3;

    static uint16_t pixBuf[24 * 24];
    uint32_t cp;
    const char* s = txt;
    while ((cp = utf8Next(s)) != 0) {
        const DEGlyph* g = deGlyph(f, cp);
        if (!g) g = deGlyph(f, '?');
        if (!g) continue;
        if (g->w > 0 && g->h > 0 && g->w <= 24 && g->h <= 24) {
            const uint8_t* bm = f.bitmap + g->off;
            int n = g->w * g->h;
            for (int i = 0; i < n; i++) {
                uint8_t a = bm[i];
                uint8_t r  = ((uint16_t)fr  * a + (uint16_t)br  * (255 - a)) / 255;
                uint8_t gr = ((uint16_t)fg8 * a + (uint16_t)bg8 * (255 - a)) / 255;
                uint8_t b  = ((uint16_t)fb  * a + (uint16_t)bb  * (255 - a)) / 255;
                pixBuf[i] = PANEL(((uint16_t)(r >> 3) << 11)
                                | ((uint16_t)(gr >> 2) << 5)
                                |  (uint16_t)(b >> 3));
            }
            tft.pushImage(x + g->ox, yBase - g->oy - g->h, g->w, g->h, pixBuf);
        }
        x += g->adv;
    }
}

void drawDECentered(int yBase, const char* txt, const DEFont& f,
                    uint16_t fg, uint16_t bgCol) {
    drawDE((SCREEN_W - deWidth(f, txt)) / 2, yBase, txt, f, fg, bgCol);
}

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
    tft.setRotation(3);   // ST7789 Landscape 180° (USB links)
    tft.setSwapBytes(true);  // pushImage: uint16-Werte in Panel-Byte-Reihenfolge
    tft.fillScreen(C_BLACK);
    tft.setTextColor(C_WHITE, C_BLACK);
    tft.drawString("Starte...", 10, 110, 2);

    // Touch (eigener SPI-Bus)
    touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
    ts.begin(touchSPI);
    ts.setRotation(3);

    // Dimensionen nach setRotation() loggen
    Serial.printf("Display: %d x %d px\n", tft.width(), tft.height());

    connectWiFi();
    syncNTP();

    tft.fillScreen(C_BLACK);
    tft.drawString("Lade Preise...", 10, 110, 2);
    isFetching = true; drawHeader(); isFetching = false;

    fetchPrices();
    fetchRenewableShare();
    lastFetch = millis();
    needsRedraw = true;
}

// ─────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    // ── Display-Schlaf─Timer ── (View komplett auf Default zurücksetzen)
    if (displayOn && now - lastActivity >= SLEEP_TIMEOUT) {
        displayOn     = false;
        sleepTime     = millis();
        currentScreen = DASHBOARD;
        showToday     = true;
        tooltipIdx    = -1;
        digitalWrite(21, LOW);
        drawDashboard();     // unsichtbar vorzeichnen → beim Aufwachen steht
        needsRedraw = false; // sofort die Default-View, nicht die alte
    }

    if (now - lastFetch >= FETCH_INTERVAL) {
        isFetching = true;
        fetchPrices();
        fetchRenewableShare();
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
        if      (currentScreen == DASHBOARD) drawDashboard();
        else if (currentScreen == CHART)     drawChart();
        else                                 drawDetail();
    }

    delay(50);
}

// ─────────────────────────────────────────────────────────────
//  WiFi
// ─────────────────────────────────────────────────────────────
void connectWiFi() {
    tft.fillScreen(C_BLACK);
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
//  Strommix: Anteil Erneuerbare an der Erzeugung (energy-charts)
// ─────────────────────────────────────────────────────────────
void fetchRenewableShare() {
    if (WiFi.status() != WL_CONNECTED) { renewValid = false; return; }

    // API akzeptiert nur ISO-Daten; start=end=heute liefert den bisherigen
    // Tag als 15-min-Serie (~6 KB)
    struct tm tmD; getLocalTime(&tmD);
    char day[12];
    strftime(day, sizeof(day), "%Y-%m-%d", &tmD);
    char url[160];
    snprintf(url, sizeof(url), "%s?country=%s&start=%s&end=%s",
             ENERGY_URL, ENERGY_COUNTRY, day, day);

    HTTPClient http;
    http.begin(url);
    int code = http.GET();
    if (code != 200) { http.end(); renewValid = false; return; }
    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) { renewValid = false; return; }

    float ren = 0, fossil = 0, directShare = -1;
    for (JsonObject pt : doc["production_types"].as<JsonArray>()) {
        const char* name = pt["name"] | "";
        JsonArray data = pt["data"].as<JsonArray>();

        float v = NAN;  // letzten gültigen Messwert der Serie nehmen
        for (int i = (int)data.size() - 1; i >= 0; i--) {
            if (!data[i].isNull()) { v = data[i].as<float>(); break; }
        }
        if (isnan(v)) continue;

        // Falls die API den Anteil direkt liefert, hat der Vorrang
        if (strcmp(name, "Renewable share of generation") == 0) {
            directShare = v;
            continue;
        }
        if (v < 0) v = 0;  // z.B. Pumpspeicher im Pump-Betrieb

        bool isRen = strstr(name, "Solar") || strstr(name, "Wind")
                  || strstr(name, "Biomass") || strstr(name, "Geothermal")
                  || (strstr(name, "Hydro") && !strstr(name, "pumped"));
        bool isFos = strstr(name, "Fossil") || strstr(name, "Nuclear")
                  || strstr(name, "Waste")  || strstr(name, "Other");
        if (isRen)      ren    += v;
        else if (isFos) fossil += v;
        // Rest (Load, Residual load, ...) ignorieren
    }

    if (directShare >= 0)         renewShare = (int)(directShare + 0.5f);
    else if (ren + fossil > 0.0f) renewShare = (int)(ren * 100.0f / (ren + fossil) + 0.5f);
    else { renewValid = false; return; }

    renewValid = true;
    Serial.printf("Erneuerbaren-Anteil (%s): %d%%\n", ENERGY_COUNTRY, renewShare);
}

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────
// Preis-Stufen auf den GESAMTpreis (Schwellen in tarif.h); negative
// Börsenpreise bleiben als "geschenkt" (türkis) markiert.
// Dashboard-Hintergrund (dunkle Varianten, damit weißer Text lesbar bleibt)
uint16_t priceColor(float totalCt, float spotCt) {
    if (spotCt < 0)              return PANEL(0x038E);  // Dunkles Türkis
    if (totalCt < T_TOTAL_CHEAP) return PANEL(0x0340);  // Dunkles Grün
    if (totalCt <= T_TOTAL_HIGH) return PANEL(0xA400);  // Dunkles Gold
    return PANEL(0xA000);                               // Dunkles Rot
}

// Chart-Balken als ZIELfarbe (echtes RGB565) – PANEL() erst beim Zeichnen,
// damit die Auswahl-Aufhellung im echten Farbraum gerechnet werden kann
uint16_t barColorTarget(float totalCt, float spotCt) {
    if (spotCt < 0)              return 0x07FF;  // Cyan (Börse negativ)
    if (totalCt < T_TOTAL_CHEAP) return 0x07E0;  // Grün
    if (totalCt <= T_TOTAL_HIGH) return 0xFFE0;  // Gelb
    return 0xF800;                               // Rot
}

// 50 % Richtung Weiß – macht den angetippten Balken auch zwischen
// gleichfarbigen Nachbarn klar erkennbar
uint16_t lightenTarget(uint16_t c) {
    uint8_t r = ((c >> 11) & 0x1F), g = ((c >> 5) & 0x3F), b = (c & 0x1F);
    return ((uint16_t)((r + 31) / 2) << 11)
         | ((uint16_t)((g + 63) / 2) << 5)
         |  (uint16_t)((b + 31) / 2);
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
    tft.fillRoundRect(tx, ty, TW, TH, 6, PANEL(0x2945));
    tft.drawRoundRect(tx, ty, TW, TH, 6, C_YELLOW);
    // Text zentriert (UTF-8-fähig)
    drawDECentered(SCREEN_H / 2 + 5, msg, FONT_DE14, C_YELLOW, PANEL(0x2945));
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
    float ct  = (cur >= 0) ? slots[cur].ct : 0.0f;                    // Börse
    float tot = (cur >= 0) ? totalPrice(ct, slots[cur].ts) : 0.0f;    // Gesamt
    uint16_t bg = priceColor(tot, ct);

    // Ganzen Screen löschen (verhindert Artefakte)
    tft.fillScreen(bg);
    drawHeader();

    // Großer Preis = GESAMTPREIS inkl. Netz/Abgaben/USt (Font 7 = 7-Segment)
    char pBuf[10];
    sprintf(pBuf, "%.1f", tot);
    tft.setTextColor(C_WHITE, bg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(pBuf, SCREEN_W / 2, HDR_H + 65, 7);
    tft.setTextDatum(TL_DATUM);

    // Einheit + Börsenpreis-Referenz (+ SNAP-Hinweis im Rabattfenster)
    char uBuf[48];
    snprintf(uBuf, sizeof(uBuf), "ct/kWh gesamt | Börse %.1f%s",
             ct, (cur >= 0 && snapAktiv(slots[cur].ts)) ? " | SNAP" : "");
    drawDECentered(HDR_H + 126, uBuf, FONT_DE10, C_WHITE, bg);

    // Preis-Label (Stufen wie priceColor)
    const char* label = (ct < 0)                ? "GESCHENKT"
                      : (tot < T_TOTAL_CHEAP)   ? "GÜNSTIG"
                      : (tot <= T_TOTAL_HIGH)   ? "MODERAT" : "TEUER";
    drawDECentered(HDR_H + 148, label, FONT_DE14, C_WHITE, bg);

    // Refresh-Button
    tft.drawRect(SCREEN_W - 50, HDR_H + 4, 46, 18, C_WHITE);
    tft.setTextColor(C_WHITE, bg);
    tft.drawString("Refresh", SCREEN_W - 48, HDR_H + 7, 1);

    // Strommix-Badge: 🔋 erneuerbar / 🛢 fossil (links unter dem Header)
    // Zeigt den Anteil Erneuerbarer an der aktuellen Stromerzeugung (AT)
    if (renewValid) {
        bool renewable = renewShare >= RENEW_THRESHOLD;
        tft.fillRoundRect(6, HDR_H + 6, 84, 34, 6, C_HDR);
        tft.pushImage(11, HDR_H + 11, 24, 24,
                      renewable ? IMG_BATTERY_24 : IMG_OIL_24);
        char rBuf[8];
        snprintf(rBuf, sizeof(rBuf), "%d%%", renewShare);
        drawDE(41, HDR_H + 20, rBuf, FONT_DE14,
               renewable ? C_GREEN : C_SILVER, C_HDR);
        drawDE(41, HDR_H + 34, renewable ? "Öko" : "Fossil",
               FONT_DE10, C_SILVER, C_HDR);
    }

    // Footer – Trend (auf Gesamtpreis)
    tft.fillRect(0, SCREEN_H - FTR_H, SCREEN_W, FTR_H, C_HDR);
    if (cur >= 0 && cur + 1 < slotCount) {
        float nextCt = totalPrice(slots[cur + 1].ct, slots[cur + 1].ts);
        bool  steigt = nextCt > tot;
        char fBuf[48];
        sprintf(fBuf, "Nächste Std: %.1f ct", nextCt);
        drawDE(8, SCREEN_H - FTR_H + 18, fBuf, FONT_DE14, C_WHITE, C_HDR);
        // Trend-Pfeil (Dreieck) rechts
        int ax = SCREEN_W - 22, ayM = SCREEN_H - FTR_H / 2;
        if (steigt)
            tft.fillTriangle(ax - 6, ayM + 4, ax + 6, ayM + 4, ax, ayM - 5, C_RED);
        else
            tft.fillTriangle(ax - 6, ayM - 4, ax + 6, ayM - 4, ax, ayM + 5, C_GREEN);
    } else {
        drawDE(8, SCREEN_H - FTR_H + 18, "Kein Trend verfügbar",
               FONT_DE14, C_WHITE, C_HDR);
    }

    // Tap-Hint
    drawDE(8, SCREEN_H - FTR_H - 6, "Tippen für Chart", FONT_DE10, C_GRAY, bg);
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
        // Min / Max / Avg – alles auf GESAMTpreis (inkl. Netz/SNAP/USt)
        float totals[24];
        float minP = 1e9f, maxP = -1e9f, sum = 0;
        for (int i = 0; i < cnt; i++) {
            totals[i] = totalPrice(view[i]->ct, view[i]->ts);
            if (totals[i] < minP) minP = totals[i];
            if (totals[i] > maxP) maxP = totals[i];
            sum += totals[i];
        }
        float avg = sum / cnt;

        // SNAP-Fenster dezent grün hinterlegen (wie im aWATTar-Portal);
        // Grid und Balken zeichnen danach darüber
        float bwf = (float)CW / cnt;
        for (int i = 0; i < cnt; i++) {
            if (!snapAktiv(view[i]->ts)) continue;
            int x0 = CX + (int)(i * bwf);
            int x1 = CX + (int)((i + 1) * bwf);
            tft.fillRect(x0, CY, x1 - x0, CH, C_SNAPBG);
        }

        // Y-Achse in 5ct-Schritten (Gesamtpreise sind praktisch nie negativ,
        // daher kein 0-Zwang mehr – nutzt die Chart-Höhe besser aus)
        float yMin   = floorf(min(minP, 0.0f) < 0 ? min(minP, 0.0f) / 5.0f
                                                  : minP / 5.0f) * 5.0f;
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
            float pr  = totals[i];
            int   bh  = max(2, (int)((pr - yMin) / yRange * CH));
            int   bx  = CX + (int)(i * bw);
            int   by  = CY + CH - bh;
            int   bwi = max(1, (int)bw - 1);

            // Angetippter Balken: aufgehellte Füllung + weißer Rahmen
            uint16_t tcol = barColorTarget(pr, view[i]->ct);
            if (i == tooltipIdx) tcol = lightenTarget(tcol);
            tft.fillRect(bx, by, bwi, bh, PANEL(tcol));

            struct tm* st = localtime(&view[i]->ts);
            if (showToday && st->tm_hour == curHour)
                tft.drawRect(bx - 1, by - 1, bwi + 2, bh + 2, C_WHITE);
            if (i == tooltipIdx)
                tft.drawRect(bx - 1, by - 1, bwi + 2, bh + 2, C_WHITE);

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
        char avgb[14]; sprintf(avgb, "Ø %.1f", avg);
        drawDE(CX + CW - 40, avgY - 3, avgb, FONT_DE10, C_WHITE, C_BG);

        // Tooltip: Gesamtpreis + Börse (B) + SNAP-Hinweis
        if (tooltipIdx >= 0 && tooltipIdx < cnt) {
            struct tm* st = localtime(&view[tooltipIdx]->ts);
            char ttb[48];
            sprintf(ttb, "%02d-%02dh: %.1f ct (B %.1f%s)",
                    st->tm_hour, (st->tm_hour + 1) % 24,
                    totals[tooltipIdx], view[tooltipIdx]->ct,
                    snapAktiv(view[tooltipIdx]->ts) ? ", SNAP" : "");
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
    drawDE(14, SCREEN_H - FTR_H + 18, "Heute", FONT_DE14, hCol, C_HDR);

    // [Morgen]
    uint16_t mCol = tomorrowAvail ? (!showToday ? C_WHITE : C_GRAY) : C_GRAY;
    tft.drawRect(76, SCREEN_H - FTR_H + 2, 76, 22, mCol);
    drawDE(84, SCREEN_H - FTR_H + 18, "Morgen", FONT_DE14, mCol, C_HDR);

    // [Zurück]
    tft.drawRect(156, SCREEN_H - FTR_H + 2, 80, 22, C_CYAN);
    drawDE(162, SCREEN_H - FTR_H + 18, "< Zurück", FONT_DE14, C_CYAN, C_HDR);
}

// ─────────────────────────────────────────────────────────────
//  DETAIL – Preis-Aufschlüsselung des selektierten Balkens
// ─────────────────────────────────────────────────────────────
void drawDetail() {
    // Selektierten Slot aus der aktuellen Chart-Ansicht holen
    HourSlot* view[24]; int cnt = 0;
    getFilteredSlots(view, cnt);
    if (tooltipIdx < 0 || tooltipIdx >= cnt) {
        currentScreen = CHART; needsRedraw = true; return;
    }
    HourSlot* s = view[tooltipIdx];
    PriceParts p = priceParts(s->ct, s->ts);

    tft.fillScreen(C_BG);
    drawHeader();

    // Titel
    struct tm* st = localtime(&s->ts);
    char buf[48];
    snprintf(buf, sizeof(buf), "Preis-Details  %02d-%02d Uhr",
             st->tm_hour, (st->tm_hour + 1) % 24);
    drawDE(12, HDR_H + 20, buf, FONT_DE14, C_WHITE, C_BG);

    // Tabelle links (Label + Wert rechtsbündig), QR-Code rechts daneben
    const int XL = 16, XR = 200;
    int y = HDR_H + 40;
    auto row = [&](const char* lbl, float val, uint16_t col) {
        drawDE(XL, y, lbl, FONT_DE10, C_SILVER, C_BG);
        char v[12]; snprintf(v, sizeof(v), "%.2f", val);
        drawDE(XR - deWidth(FONT_DE10, v), y, v, FONT_DE10, col, C_BG);
        y += 15;
    };

    // QR-Code → Doku-Seite (Version 3 = 29x29 Module, 3 px pro Modul)
    {
        QRCode qr;
        uint8_t qrData[qrcode_getBufferSize(3)];
        if (qrcode_initText(&qr, qrData, 3, ECC_MEDIUM, DOKU_URL) == 0) {
            const int MOD = 3, QUIET = 4;
            const int QX = 212, QY = HDR_H + 38;
            int size = qr.size * MOD;
            // weiße Quiet-Zone (Scanner brauchen hellen Rand)
            tft.fillRect(QX - QUIET, QY - QUIET,
                         size + 2 * QUIET, size + 2 * QUIET, C_WHITE);
            for (int qy = 0; qy < qr.size; qy++)
                for (int qx = 0; qx < qr.size; qx++)
                    if (qrcode_getModule(&qr, qx, qy))
                        tft.fillRect(QX + qx * MOD, QY + qy * MOD,
                                     MOD, MOD, C_BLACK);
            drawDE(QX + 14, QY + size + 16, "Doku", FONT_DE10, C_SILVER, C_BG);
        }
    }

    row("Börse (EPEX Spot)",       p.spot,        C_WHITE);
    row("aWATTar (+3% +1,5)",      p.aufschlag,   C_WHITE);
    row(p.snap ? "Netznutzung (SNAP -20%)"
               : "Netznutzung",    p.netz,        p.snap ? C_GREEN : C_WHITE);
    row("Netzverlust",             p.netzverlust, C_WHITE);
    row("Elektrizitätsabgabe",     p.eabgabe,     C_WHITE);
    row("Ökostromförderbeitrag",   p.oeko,        C_WHITE);
    row("Gebrauchsabgabe (7%)",    p.gebrauchsabg, C_WHITE);

    tft.drawFastHLine(XL, y - 9, XR - XL, C_GRAY);
    y += 3;
    row("Netto",                   p.netto,       C_WHITE);
    row("USt (20%)",               p.ust,         C_WHITE);

    // Summe – groß, volle Breite, in der Preisstufen-Farbe
    tft.drawFastHLine(XL, y - 9, 300 - XL, C_GRAY);
    y += 8;
    uint16_t sumCol = PANEL(barColorTarget(p.brutto, p.spot));
    drawDE(XL, y, "Summe", FONT_DE14, C_WHITE, C_BG);
    snprintf(buf, sizeof(buf), "%.1f ct/kWh", p.brutto);
    drawDE(300 - deWidth(FONT_DE14, buf), y, buf, FONT_DE14, sumCol, C_BG);

    // Hinweis
    drawDE(XL, SCREEN_H - 5, "Tippen zum Schließen", FONT_DE10, C_GRAY, C_BG);
}

// ─────────────────────────────────────────────────────────────
//  TOUCH
// ─────────────────────────────────────────────────────────────
void handleTouch() {
    if (!ts.tirqTouched() || !ts.touched()) return;
    TS_Point p = ts.getPoint();

    // Display aufwecken falls schlafen
    if (!displayOn) {
        if (millis() - sleepTime < 1500) return; // Mindest-Schlafzeit
        displayOn    = true;
        lastActivity = millis();
        digitalWrite(21, HIGH);
        needsRedraw  = true;
        delay(200);
        return;
    }
    lastActivity = millis();

    // Kalibrierung aus gemessenen Eckwerten (raw_x: 210–3790, raw_y: 245–3900)
    int tx = map(p.x, 3790, 210, 0, SCREEN_W);
    int ty = map(p.y, 3900, 245, 0, SCREEN_H);
    tx = constrain(tx, 0, SCREEN_W - 1);
    ty = constrain(ty, 0, SCREEN_H - 1);

    Serial.printf("Touch: raw(%d,%d) -> screen(%d,%d)\n", p.x, p.y, tx, ty);

#if TOUCH_DEBUG
    // Roter Punkt an der berechneten Position zeichnen
    tft.fillCircle(tx, ty, 6, C_RED);
    tft.drawCircle(tx, ty, 7, C_WHITE);
    // Koordinaten-Label
    char dbgBuf[40];
    sprintf(dbgBuf, "raw(%d,%d)", p.x, p.y);
    tft.fillRect(0, SCREEN_H - FTR_H - 14, 160, 14, C_HDR);
    tft.setTextColor(C_YELLOW, C_HDR);
    tft.drawString(dbgBuf, 2, SCREEN_H - FTR_H - 13, 1);
    sprintf(dbgBuf, "px(%d,%d)", tx, ty);
    tft.fillRect(0, SCREEN_H - FTR_H - 7, 120, 8, C_HDR);
    tft.drawString(dbgBuf, 2, SCREEN_H - FTR_H - 6, 1);
    delay(500); // laenger warten damit Punkt sichtbar bleibt
    return;     // im Debug-Modus KEIN normaler Touch-Handler
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
            showToday     = true;
            tooltipIdx    = -1;
            digitalWrite(21, LOW);
            drawDashboard();     // wie beim Auto-Sleep: Default-View vorzeichnen
            needsRedraw = false;
        }
        return;
    }

    delay(200); // Entprellen

    // Detail-Ansicht: beliebiger Tap schließt sie
    if (currentScreen == DETAIL) {
        currentScreen = CHART;
        needsRedraw   = true;
        return;
    }

    if (currentScreen == DASHBOARD) {
        // Refresh-Button (oben rechts)
        if (tx > SCREEN_W - 50 && ty > HDR_H && ty < HDR_H + 22) {
            isFetching = true; needsRedraw = true;
            if (currentScreen == DASHBOARD) drawHeader();
            fetchPrices();
            fetchRenewableShare();
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
                    showToast("Morgen noch nicht verfügbar");
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
                if (tooltipIdx == idx) currentScreen = DETAIL;  // 2. Tap: Aufschlüsselung
                else                   tooltipIdx    = idx;
                needsRedraw = true;
            }
        }
    }
}
