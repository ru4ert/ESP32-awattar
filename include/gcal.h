#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Google-Kalender-Preisalarm: Konten werden per echtem OAuth 2.0 Authorization-
// Code-Flow gekoppelt. Google erlaubt Calendar-Scopes NICHT über den nativen
// Device-Authorization-Flow (nur email/openid/profile/drive.appdata/drive.file/
// youtube, siehe https://developers.google.com/identity/protocols/oauth2/limited-input-device)
// – daher übernimmt ein kleines Google-Apps-Script (tools/gcal-bridge.gs, unter
// deinem eigenen Google-Account deployt) den OAuth-Redirect + Code-Tausch
// server-seitig. Das ESP32 zeigt nur einen kurzen QR-Link zum Script und pollt
// es danach, exakt wie zuvor beim (nicht funktionierenden) Device-Flow.
// Refresh-Tokens liegen im NVS (Preferences, Namespace "gcal"); Access-Tokens
// werden nie persistiert, sondern vor jedem Event-Call frisch geholt.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <time.h>

#define MAX_GCAL_ACCOUNTS 3   // NVS-Partition ist nur 20 KB (min_spiffs.csv)

struct GCalAccount { String email; String refreshToken; };

extern GCalAccount gcalAccounts[MAX_GCAL_ACCOUNTS];
extern int         gcalAccountCount;

struct PairingState {
    String        sessionId, qrUrl;
    unsigned long expiresAt  = 0;
    unsigned long nextPollAt = 0;
};
extern PairingState gcalFlow;

void loadGCalAccounts();
bool startPairing(PairingState& s);

// Rückgabe: 0 = pending (weiter warten), 1 = Erfolg (Account gespeichert,
// letzter Eintrag in gcalAccounts[]), 2 = Fehler/abgelaufen
int pollPairing(PairingState& s);

bool createCalendarEvent(const GCalAccount& acc, time_t hourStart, float totalPriceCt);
