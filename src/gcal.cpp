// ─────────────────────────────────────────────────────────────────────────────
// Google-Kalender-Preisalarm: Pairing über die Apps-Script-Brücke (siehe
// gcal.h), Token-Refresh, Event-Erstellung. Reine Netzwerk-/Storage-Logik,
// kein UI (siehe gcal.h).
// ─────────────────────────────────────────────────────────────────────────────
#include "gcal.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_system.h>
#include "secrets.h"

GCalAccount   gcalAccounts[MAX_GCAL_ACCOUNTS];
int           gcalAccountCount = 0;
PairingState  gcalFlow;

static const char* TOKEN_URL    = "https://oauth2.googleapis.com/token";
static const char* USERINFO_URL = "https://www.googleapis.com/oauth2/v3/userinfo";
static const char* EVENTS_URL   = "https://www.googleapis.com/calendar/v3/calendars/primary/events";

// ── Persistenz (NVS) ─────────────────────────────────────────────────────────
void loadGCalAccounts() {
    Preferences prefs;
    prefs.begin("gcal", true);
    gcalAccountCount = prefs.getInt("n", 0);
    if (gcalAccountCount > MAX_GCAL_ACCOUNTS) gcalAccountCount = MAX_GCAL_ACCOUNTS;
    for (int i = 0; i < gcalAccountCount; i++) {
        char ek[8], tk[8];
        snprintf(ek, sizeof(ek), "email%d", i);
        snprintf(tk, sizeof(tk), "tok%d", i);
        gcalAccounts[i].email        = prefs.getString(ek, "");
        gcalAccounts[i].refreshToken = prefs.getString(tk, "");
    }
    prefs.end();
    Serial.printf("gcal: %d Konto(s) geladen\n", gcalAccountCount);
}

static void saveGCalAccounts() {
    Preferences prefs;
    prefs.begin("gcal", false);
    prefs.putInt("n", gcalAccountCount);
    for (int i = 0; i < gcalAccountCount; i++) {
        char ek[8], tk[8];
        snprintf(ek, sizeof(ek), "email%d", i);
        snprintf(tk, sizeof(tk), "tok%d", i);
        prefs.putString(ek, gcalAccounts[i].email);
        prefs.putString(tk, gcalAccounts[i].refreshToken);
    }
    prefs.end();
}

// ── HTTP-Helper (gleiches Blocking-Muster wie fetchPrices() in main.cpp) ─────
static bool httpPostForm(const char* url, const String& body, JsonDocument& out) {
    HTTPClient http;
    http.begin(url);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int code = http.POST(body);
    String payload = http.getString();
    http.end();
    if (code != 200 && code != 400) return false;
    return deserializeJson(out, payload) == DeserializationError::Ok;
}

static bool httpPostJson(const char* url, const String& accessToken,
                          const String& body, JsonDocument& out) {
    HTTPClient http;
    http.begin(url);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + accessToken);
    int code = http.POST(body);
    String payload = http.getString();
    http.end();
    if (code < 200 || code >= 300) return false;
    if (payload.length() == 0) return true;
    return deserializeJson(out, payload) == DeserializationError::Ok;
}

static String getAccessToken(const String& refreshToken) {
    String body = String("client_id=") + GOOGLE_CLIENT_ID
                + "&client_secret=" + GOOGLE_CLIENT_SECRET
                + "&refresh_token=" + refreshToken
                + "&grant_type=refresh_token";
    JsonDocument doc;
    if (!httpPostForm(TOKEN_URL, body, doc)) return "";
    return doc["access_token"] | "";
}

// ── Pairing über die Apps-Script-Brücke ───────────────────────────────────────
// Google erlaubt Calendar-Scopes nicht im nativen Device-Flow (nur email/
// openid/profile/drive.appdata/drive.file/youtube) – deshalb übernimmt
// tools/gcal-bridge.gs den echten Authorization-Code-Flow server-seitig.
// Das ESP32 zeigt nur einen QR mit ?start=<sessionId> auf die Brücke und
// pollt sie danach mit ?poll=<sessionId>.
static String randomSessionId() {
    char buf[33];
    for (int i = 0; i < 32; i += 8)
        snprintf(buf + i, 9, "%08x", (unsigned int)esp_random());
    return String(buf);
}

bool startPairing(PairingState& s) {
    if (WiFi.status() != WL_CONNECTED) return false;
    s.sessionId  = randomSessionId();
    s.qrUrl      = String(GCAL_BRIDGE_URL) + "?start=" + s.sessionId;
    s.expiresAt  = millis() + 1800000UL;  // 30 Min, deckt sich mit der Cache-TTL in der Brücke
    s.nextPollAt = millis() + 3000UL;
    Serial.printf("gcal: Pairing gestartet, Session %s\n", s.sessionId.c_str());
    return true;
}

int pollPairing(PairingState& s) {
    if (millis() > s.expiresAt) {
        Serial.println("gcal: Pairing Fehler: Session abgelaufen (expiresAt erreicht)");
        return 2;
    }

    HTTPClient http;
    http.begin(String(GCAL_BRIDGE_URL) + "?poll=" + s.sessionId);
    // Apps-Script-/exec-URLs leiten intern immer per 302 auf eine
    // googleusercontent.com-Ausführungs-URL um – ohne das folgt HTTPClient
    // dem nicht, und man bekommt nie den echten Inhalt zu sehen.
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    int code = http.GET();
    String payload = http.getString();
    http.end();
    Serial.printf("gcal: Poll HTTP %d, Payload: %s\n", code, payload.c_str());

    if (code != 200) { s.nextPollAt = millis() + 3000UL; return 0; }
    if (payload == "pending") { s.nextPollAt = millis() + 3000UL; return 0; }

    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) {
        Serial.println("gcal: Poll Fehler: Antwort ist kein gültiges JSON");
        s.nextPollAt = millis() + 3000UL;
        return 0;
    }
    String email        = doc["email"]        | "";
    String refreshToken = doc["refreshToken"]  | "";
    if (refreshToken.length() == 0) {
        Serial.println("gcal: Pairing Fehler: kein Refresh-Token von der Brücke erhalten");
        return 2;
    }

    int slot = (gcalAccountCount < MAX_GCAL_ACCOUNTS) ? gcalAccountCount++
                                                       : MAX_GCAL_ACCOUNTS - 1;
    gcalAccounts[slot].email        = email;
    gcalAccounts[slot].refreshToken = refreshToken;
    saveGCalAccounts();
    Serial.printf("gcal: Konto gekoppelt: %s\n", email.c_str());
    return 1;
}

// ── Kalendertermin anlegen ─────────────────────────────────────────────────
bool createCalendarEvent(const GCalAccount& acc, time_t hourStart, float totalPriceCt) {
    String accessToken = getAccessToken(acc.refreshToken);
    if (accessToken.length() == 0) return false;

    time_t hourEnd = hourStart + 3600;
    char startBuf[32], endBuf[32];
    strftime(startBuf, sizeof(startBuf), "%Y-%m-%dT%H:%M:%S", localtime(&hourStart));
    strftime(endBuf,   sizeof(endBuf),   "%Y-%m-%dT%H:%M:%S", localtime(&hourEnd));

    char summary[48];
    snprintf(summary, sizeof(summary), "Guenstiger Strompreis: %.1f ct/kWh", totalPriceCt);

    JsonDocument body;
    body["summary"]            = summary;
    body["start"]["dateTime"]  = startBuf;
    body["start"]["timeZone"]  = "Europe/Vienna";
    body["end"]["dateTime"]    = endBuf;
    body["end"]["timeZone"]    = "Europe/Vienna";

    String payload;
    serializeJson(body, payload);

    JsonDocument resp;
    bool ok = httpPostJson(EVENTS_URL, accessToken, payload, resp);
    Serial.printf("gcal: Termin anlegen für %s: %s\n", acc.email.c_str(), ok ? "OK" : "FEHLER");
    return ok;
}
