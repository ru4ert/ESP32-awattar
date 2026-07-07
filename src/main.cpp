// ============================================================
//  aWATTar ESP32 Strompreis Monitor – LVGL Edition
//  Board : ESP32-2432S028R (CYD, ST7789-Panel-Variante)
//  UI    : LVGL v8.3 (Screens in ui_dashboard.cpp / ui_chart.cpp)
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <time.h>
#include <math.h>
#include <Preferences.h>
#include "secrets.h"
#include "lv_drivers.h"
#include "ui.h"

TFT_eSPI tft;

// ── Konstanten ────────────────────────────────────────────────────────────────
const char* AWATTAR_URL = "https://api.awattar.at/v1/marketdata";
const char* TZ_VIENNA   = "CET-1CEST,M3.5.0,M10.5.0/3";  // auto DST
const char* NTP1        = "pool.ntp.org";
const char* NTP2        = "at.pool.ntp.org";

#define FETCH_INTERVAL  3600000UL   // 1 h
#define UPDATE_INTERVAL    1000UL   // UI-Refresh (Uhrzeit etc.)
#define SLEEP_TIMEOUT     30000UL   // Display-Sleep nach 30 s Inaktivität

// Strommix-API (Fraunhofer ISE, kein API-Key nötig); "at" ↔ "de" je Standort
const char* ENERGY_URL     = "https://api.energy-charts.info/public_power";
const char* ENERGY_COUNTRY = "at";

// ── Daten (extern in ui.h deklariert) ────────────────────────────────────────
HourSlot  slots[MAX_SLOTS];
int       slotCount     = 0;
bool      tomorrowAvail = false;
bool      isFetching    = false;
bool      needsFetch    = false;   // wird vom Refresh-Button gesetzt
int       renewShare    = -1;      // Anteil Erneuerbare in % (aktuellste Messung)
bool      renewValid    = false;

// ── Screens ───────────────────────────────────────────────────────────────────
static lv_obj_t *scr_dashboard = nullptr;
static lv_obj_t *scr_chart     = nullptr;
static lv_obj_t *scr_setup     = nullptr;

static unsigned long lastFetch  = 0;
static unsigned long lastUpdate = 0;

// ── Screen-Wechsel (von den UI-Modulen aufgerufen) ───────────────────────────
void ui_show_dashboard() {
    ui_dashboard_update();
    lv_scr_load_anim(scr_dashboard, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
}

void ui_show_chart() {
    ui_chart_update(true);  // Standard: Heute
    lv_scr_load_anim(scr_chart, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

void ui_show_setup() {
    lv_scr_load_anim(scr_setup, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

// ── Display-Sleep (nur Hardware) ─────────────────────────────────────────────
#ifndef SIMULATION
static bool      displayOn     = true;
static lv_obj_t *sleep_overlay = nullptr;

static void wake_cb(lv_event_t *e) {
    displayOn = true;
    digitalWrite(TFT_BL, HIGH);
    // Overlay hat den Aufweck-Tap geschluckt – async löschen (wir stecken
    // gerade in dessen eigenem Event-Handler)
    lv_obj_del_async(sleep_overlay);
    sleep_overlay = nullptr;
}

static void displaySleep() {
    displayOn = false;
    digitalWrite(TFT_BL, LOW);
    lv_scr_load(scr_dashboard);  // beim Aufwachen wieder Dashboard zeigen
    // Transparentes Overlay fängt den ersten Touch ab, damit der
    // Aufweck-Tap keinen Button darunter auslöst
    sleep_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(sleep_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(sleep_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sleep_overlay, 0, 0);
    lv_obj_add_flag(sleep_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sleep_overlay, wake_cb, LV_EVENT_PRESSED, nullptr);
}
#endif

// ── WiFi ──────────────────────────────────────────────────────────────────────
void connectWiFi() {
#ifdef SIMULATION
    Serial.println("[SIM] WiFi übersprungen");
    return;
#else
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) delay(500);
    if (WiFi.status() == WL_CONNECTED)
        Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());
    else
        Serial.println("WiFi FEHLER");
#endif
}

// ── NTP mit automatischer Sommerzeit (Europe/Vienna) ─────────────────────────
void syncNTP() {
#ifdef SIMULATION
    struct timeval tv = { 1749981600L, 0 };  // 2025-06-15 10:00 CEST
    settimeofday(&tv, nullptr);
    setenv("TZ", TZ_VIENNA, 1);
    tzset();
    Serial.println("[SIM] Zeit: 15.06.2025 10:00 CEST");
    return;
#else
    configTzTime(TZ_VIENNA, NTP1, NTP2);
    struct tm ti;
    int tries = 0;
    while (!getLocalTime(&ti) && tries++ < 20) delay(500);
    if (tries < 20) {
        char buf[32]; strftime(buf, sizeof(buf), "%H:%M %d.%m.%Y", &ti);
        Serial.printf("NTP OK: %s\n", buf);
    }
#endif
}

// ── API Fetch ─────────────────────────────────────────────────────────────────
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
        -2.1f, -1.5f, -0.8f,  0.5f,  1.2f,  2.3f,
         4.1f,  8.7f, 12.4f, 10.2f,  8.9f,  7.6f,
         6.8f,  7.2f,  9.1f, 14.3f, 18.7f, 22.1f,
        19.8f, 15.4f, 12.1f,  9.8f,  7.2f,  4.5f,
        // Morgen (Stunden 00-23)
         1.2f, -0.5f, -1.8f, -2.4f, -1.1f,  0.9f,
         3.7f,  7.8f, 11.2f,  9.4f,  8.1f,  6.9f,
         5.8f,  6.3f,  8.2f, 13.1f, 16.9f, 20.5f,
        17.3f, 13.8f, 10.9f,  8.4f,  5.7f,  3.2f,
    };
    slotCount     = 48;
    tomorrowAvail = true;
    for (int i = 0; i < 48; i++) {
        slots[i].ts = midnight + (time_t)(i * 3600);
        slots[i].ct = prices[i];
    }
    Serial.println("[SIM] Mock-Preise geladen");
    return true;
#else
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
#endif
}

// ── Strommix: Anteil Erneuerbare an der aktuellen Erzeugung (energy-charts) ──
void fetchRenewableShare() {
#ifdef SIMULATION
    renewShare = 74;
    renewValid = true;
    Serial.println("[SIM] Erneuerbaren-Anteil: 74%");
    return;
#else
    if (WiFi.status() != WL_CONNECTED) { renewValid = false; return; }

    // Nur die letzten 2 h abfragen – hält die JSON-Antwort klein
    time_t now = time(nullptr);
    char url[160];
    snprintf(url, sizeof(url), "%s?country=%s&start=%lld&end=%lld",
             ENERGY_URL, ENERGY_COUNTRY,
             (long long)(now - 7200), (long long)now);

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

        float v = NAN;  // letzten gültigen Messwert nehmen
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
        // alles andere (Load, Residual load, ...) ignorieren
    }

    if (directShare >= 0)         renewShare = (int)(directShare + 0.5f);
    else if (ren + fossil > 0.0f) renewShare = (int)(ren * 100.0f / (ren + fossil) + 0.5f);
    else { renewValid = false; return; }

    renewValid = true;
    Serial.printf("Erneuerbaren-Anteil (%s): %d%%\n", ENERGY_COUNTRY, renewShare);
#endif
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("=== aWATTar LVGL Start ===");

#ifndef SIMULATION
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);  // Backlight an
#endif

    tft.init();

    // Display-Konfiguration aus NVS laden (kalibrierbar über Setup-Screen)
#ifdef SIMULATION
    DispCfg dispDefaults = { 1, false, true };  // Wokwi = echtes ILI9341
#else
    DispCfg dispDefaults = { 3, true,  true };  // Startpunkt für Clone-Panel
#endif
    Preferences prefs;
    prefs.begin("disp", true);
    DispCfg cfg;
    cfg.rot = prefs.getUChar("rot", dispDefaults.rot);
    cfg.inv = prefs.getBool("inv",  dispDefaults.inv);
    cfg.bgr = prefs.getBool("bgr",  dispDefaults.bgr);
    prefs.end();

    // LVGL + Treiber (wendet auch Rotation/Inv/BGR an)
    lvgl_init(tft, cfg);
    tft.fillScreen(TFT_BLACK);
    Serial.printf("Display: %d x %d px\n", tft.width(), tft.height());

    // Screens erstellen
    scr_dashboard = lv_obj_create(nullptr);
    scr_chart     = lv_obj_create(nullptr);
    scr_setup     = lv_obj_create(nullptr);
    ui_dashboard_create(scr_dashboard);
    ui_chart_create(scr_chart);
    ui_setup_create(scr_setup);
    lv_scr_load(scr_dashboard);
    lv_refr_now(nullptr);  // einmal zeichnen, bevor WiFi blockiert

    connectWiFi();
    syncNTP();

    isFetching = true;
    ui_dashboard_update();
    lv_refr_now(nullptr);
    fetchPrices();
    fetchRenewableShare();
    isFetching = false;
    lastFetch  = millis();

    ui_dashboard_update();
    Serial.println("=== SETUP DONE ===");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    lvgl_update();  // LVGL Tasks (Touch + Rendering)

    // UI jede Sekunde aktualisieren (Uhrzeit, Preis)
    if (now - lastUpdate >= UPDATE_INTERVAL) {
        lastUpdate = now;
        if (lv_scr_act() == scr_dashboard) ui_dashboard_update();
    }

    // Stündlicher Fetch + manueller Refresh-Button
    if (now - lastFetch >= FETCH_INTERVAL || needsFetch) {
        needsFetch = false;
        isFetching = true;
        ui_dashboard_update();
        lv_refr_now(nullptr);   // Spinner sofort zeigen (Fetch blockiert)
        fetchPrices();
        fetchRenewableShare();
        isFetching = false;
        lastFetch  = now;
        ui_dashboard_update();
        if (lv_scr_act() == scr_chart) ui_chart_update(ui_chart_is_today());
    }

#ifndef SIMULATION
    // Display-Sleep: LVGL trackt Touch-Inaktivität selbst
    if (displayOn && lv_disp_get_inactive_time(nullptr) >= SLEEP_TIMEOUT)
        displaySleep();
#endif

    delay(5);  // LVGL braucht ~5 ms Tick-Auflösung
}
