# ⚡ aWATTar Strompreis-Monitor

ESP32 + 2,8"-Touch-TFT zeigt den österreichischen Börsenstrompreis
(aWATTar HOURLY) — aber nicht den „nackten" Spotpreis, sondern den
**echten Gesamtpreis pro kWh**: Börse + aWATTar-Aufschlag + Netzentgelte
(inkl. SNAP-Sommerrabatt) + Abgaben + USt.

## Features

- **Dashboard**: Gesamtpreis groß, Börsenpreis als Referenz, Einstufung
  GESCHENKT / GÜNSTIG / MODERAT / TEUER, Trend-Pfeil für die nächste Stunde
- **Strommix-Badge**: 🔋 „Öko" ab 75 % Erneuerbaren-Anteil an der aktuellen
  AT-Erzeugung, sonst 🛢 „Fossil" (Live-Daten: energy-charts.info / Fraunhofer)
- **24h-Chart** für heute & morgen: farbcodierte Balken (Gesamtpreis),
  **SNAP-Fenster grün hinterlegt** (Apr–Sep, 10–16 Uhr: −20 % Netz-Arbeitspreis),
  Ø-Tageslinie, Tages-Tooltip per Stift-Tap
- **Preis-Aufschlüsselung**: zweiter Tap auf einen Balken zeigt alle
  Bestandteile bis zur Brutto-Summe + **QR-Code** zur [Doku-Seite](docs/index.html)
- Display-Sleep nach 30 s (Aufwecken per Touch), stündlicher Auto-Refresh

## Los geht's

```bash
# include/secrets.h anlegen (gitignored):
#   #define WIFI_SSID     "..."
#   #define WIFI_PASSWORD "..."
pio run -e esp32dev -t upload
```

Eigener Tarif? Alle Beträge (Netzentgelte, Abgaben, SNAP, Farbschwellen)
stehen zentral in [include/tarif.h](include/tarif.h) — Werte von der eigenen
Strom-/Netzrechnung eintragen, fertig.

## Technik-Doku

Für Entwickler:innen und **KI-Agenten**: [CLAUDE.md](CLAUDE.md) — unbedingt
zuerst lesen, das Display-Panel ist ein Clone mit ungewöhnlichen Eigenheiten
(Farb-Kompensation in Software, eigener UTF-8-Font-Renderer).
Font-/Emoji-Generierung: [tools/README.md](tools/README.md).

Hardware: ESP32 + 2,8" TFT 320×240 (ST7789-Clone „HW-458") + XPT2046-Touch,
Verdrahtung nach CYD-Pinout (ESP32-2432S028R), siehe
[include/tft_config.h](include/tft_config.h).
