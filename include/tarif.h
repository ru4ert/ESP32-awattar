#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Tarifmodell: rechnet den EPEX-Börsenpreis (aWATTar-API) auf den tatsächlich
// bezahlten GRENZpreis pro kWh hoch (brutto).
//
// Werte aus der realen aWATTar-Rechnung 2026648479 (Juni 2026, Wiener Netze,
// alle Angaben NETTO in ct/kWh). Bewusst NICHT enthalten sind Fixkosten
// (Grundgebühr, Leistungspauschale, Messentgelt, Förderpauschalen) – sie
// fallen unabhängig vom Einschaltzeitpunkt an.
// ─────────────────────────────────────────────────────────────────────────────

// aWATTar HOURLY: Börsenpreis + 3 % + 1,5 ct/kWh
#define T_AWATTAR_PCT   0.03f
#define T_AWATTAR_FIX   1.50f

// Netz (Wiener Netze): Netznutzungsentgelt Arbeitspreis + Netzverlustentgelt
#define T_NETZ_AP       6.98f   // lt. Rechnung; im SNAP-Fenster 5,58 (= −20 %)
#define T_NETZVERLUST   0.70f

// Abgaben pro kWh
#define T_EABGABE       0.10f   // Elektrizitätsabgabe (reduzierter Satz)
#define T_OEKO_ARBEIT   0.58f   // Ökostromförderbeitrag (Arbeit)
#define T_OEKO_VERLUST  0.04f   // Ökostromförderbeitrag (Verlust)
#define T_GEBRAUCHSABG  0.07f   // Wiener Gebrauchsabgabe: 7 % auf Energie+Netz

// Umsatzsteuer-Faktor
#define T_UST           1.20f

// ── SNAP: Sommer-Nieder-Arbeitspreis ────────────────────────────────────────
// 1. April – 30. September, täglich 10:00–16:00 Uhr: −20 % auf den
// Netznutzungs-Arbeitspreis. Auf der Rechnung bestätigt: 5,58 = 6,98 × 0,8.
#define SNAP_RABATT       0.20f
#define SNAP_MONAT_VON    4
#define SNAP_MONAT_BIS    9
#define SNAP_STUNDE_VON   10   // inklusive
#define SNAP_STUNDE_BIS   16   // exklusive

// ── Farbschwellen für den GESAMTpreis (brutto, ct/kWh) ──────────────────────
// Zur Einordnung: Börse 0 ct → ~12,7 | Börse 10 ct → ~25 | Börse 15 ct → ~31
#define T_TOTAL_CHEAP   20.0f   // darunter: GÜNSTIG (grün)
#define T_TOTAL_HIGH    30.0f   // darüber:  TEUER  (rot)

// ── Umrechnung Börse → Gesamtpreis (brutto, pro kWh) ────────────────────────
static inline bool snapAktiv(time_t ts) {
    struct tm t; localtime_r(&ts, &t);
    int monat = t.tm_mon + 1;
    return monat >= SNAP_MONAT_VON && monat <= SNAP_MONAT_BIS
        && t.tm_hour >= SNAP_STUNDE_VON && t.tm_hour < SNAP_STUNDE_BIS;
}

// Alle Preisbestandteile einzeln (für die Detail-Ansicht im Chart)
struct PriceParts {
    float spot;          // EPEX-Börsenpreis
    float aufschlag;     // aWATTar: 3 % + 1,5 ct
    float netz;          // Netznutzung (ggf. mit SNAP-Rabatt)
    float netzverlust;
    float eabgabe;       // Elektrizitätsabgabe
    float oeko;          // Ökostromförderbeitrag (Arbeit + Verlust)
    float gebrauchsabg;  // 7 % auf Energie + Netz
    float netto, ust, brutto;
    bool  snap;
};

static inline PriceParts priceParts(float spotCt, time_t ts) {
    PriceParts p;
    p.spot         = spotCt;
    p.aufschlag    = spotCt * T_AWATTAR_PCT + T_AWATTAR_FIX;
    p.snap         = snapAktiv(ts);
    p.netz         = T_NETZ_AP * (p.snap ? (1.0f - SNAP_RABATT) : 1.0f);
    p.netzverlust  = T_NETZVERLUST;
    p.eabgabe      = T_EABGABE;
    p.oeko         = T_OEKO_ARBEIT + T_OEKO_VERLUST;
    p.gebrauchsabg = T_GEBRAUCHSABG
                   * (p.spot + p.aufschlag + p.netz + p.netzverlust);
    p.netto  = p.spot + p.aufschlag + p.netz + p.netzverlust
             + p.eabgabe + p.oeko + p.gebrauchsabg;
    p.ust    = p.netto * (T_UST - 1.0f);
    p.brutto = p.netto * T_UST;
    return p;
}

static inline float totalPrice(float spotCt, time_t ts) {
    return priceParts(spotCt, ts).brutto;
}
