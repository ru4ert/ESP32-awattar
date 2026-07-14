# ⚡ aWATTar Strompreis-Monitor

> ESP32 touch display showing the **true total electricity price** for
> aWATTar Austria customers — spot price + grid fees (incl. SNAP summer
> discount) + levies + VAT. Documentation is in German.

ESP32 + 2,8"-Touch-TFT zeigt den österreichischen Börsenstrompreis
(aWATTar HOURLY) — aber nicht den „nackten" Spotpreis, sondern den
**echten Gesamtpreis pro kWh**: Börse + aWATTar-Aufschlag + Netzentgelte
(inkl. SNAP-Sommerrabatt) + Abgaben + USt. Erst damit ist „ist Strom gerade
günstig?" ehrlich beantwortbar.

📷 *Fotos folgen in Kürze.*
<!-- TODO: Fotos Dashboard / Chart / Detail-Ansicht hier einfügen -->

## Features

- **Dashboard**: Gesamtpreis groß, Börsenpreis als Referenz, Einstufung
  GESCHENKT / GÜNSTIG / MODERAT / TEUER, Trend-Pfeil für die nächste Stunde
- **Strommix-Badge**: 🔋 „Öko" ab 75 % Erneuerbaren-Anteil an der aktuellen
  AT-Erzeugung, sonst 🛢 „Fossil" (Live-Daten: energy-charts.info / Fraunhofer ISE)
- **24h-Chart** für heute & morgen: farbcodierte Balken (Gesamtpreis),
  **SNAP-Fenster grün hinterlegt** (Apr–Sep, 10–16 Uhr: −20 % Netz-Arbeitspreis),
  Ø-Tageslinie, Tooltip per Stift-Tap
- **Preis-Aufschlüsselung**: zweiter Tap auf einen Balken zeigt alle
  Bestandteile bis zur Brutto-Summe + **QR-Code** zur
  [Doku-Seite](https://ru4ert.github.io/ESP32-awattar/)
- Deutsche Texte mit echten Umlauten (eigener antialiased Font-Renderer),
  echte Farb-Emojis als Bitmaps
- Display-Sleep nach 30 s, stündlicher Auto-Refresh, NTP mit Sommerzeit

## Hardware

| Teil | Details |
|---|---|
| ESP32 DevKit | klassischer ESP32 (kein S2/S3 nötig) |
| 2,8" TFT 320×240 | ST7789/ILI9341-Klasse mit XPT2046-Resistiv-Touch, z. B. „HW-458"-Modul — oder gleich ein CYD (ESP32-2432S028R) |
| Verdrahtung | CYD-Pinout: TFT auf HSPI (12/13/14/15/2, BL 21), Touch auf VSPI (25/32/33/36/39) — siehe [include/tft_config.h](include/tft_config.h) |

⚠️ Billige Clone-Panels haben teils invertierte Farben / vertauschte
Farbkanäle — dieses Projekt kompensiert das in Software
([include/panel_color.h](include/panel_color.h), Details in [CLAUDE.md](CLAUDE.md)).

## Installation

```bash
git clone https://github.com/ru4ert/ESP32-awattar.git
cd ESP32-awattar

# WLAN-Zugangsdaten anlegen (gitignored):
cat > include/secrets.h <<'EOF'
#define WIFI_SSID     "..."
#define WIFI_PASSWORD "..."
EOF

pio run -e esp32dev -t upload    # PlatformIO CLI oder VSCode-Extension
```

## Eigenen Tarif eintragen

Alle Beträge stehen zentral in [include/tarif.h](include/tarif.h):
Netznutzungs-/Netzverlustentgelt, Abgaben, aWATTar-Aufschlag, SNAP-Fenster
und die Farbschwellen. Einfach die ct/kWh-Werte der eigenen Strom- und
Netzrechnung eintragen — die hinterlegten Werte stammen von einer realen
aWATTar-Rechnung (06/2026, Wiener Netze) und sind gegen diese validiert.
Deutschland: `ENERGY_COUNTRY` auf `"de"` und die aWATTar-DE-URL setzen.

## Projektstruktur & Mitentwickeln

Technische Doku für Entwickler:innen und **KI-Agenten**:
[CLAUDE.md](CLAUDE.md) — unbedingt zuerst lesen (Panel-Quirks!).
Font-/Emoji-Generierung: [tools/README.md](tools/README.md).
Öffentliche Tarif-Doku (QR-Ziel): [docs/index.html](docs/index.html)
via GitHub Pages (Settings → Pages → `main` + `/docs`).

## Kontakt

Du willst so ein Display auch haben oder hast Fragen zum Nachbau?
→ Einfach ein [Issue aufmachen](https://github.com/ru4ert/ESP32-awattar/issues)
oder mich über mein GitHub-Profil [@ru4ert](https://github.com/ru4ert)
kontaktieren.

## Lizenz & Credits

Code: [MIT](LICENSE) © 2026 Rupert Bogensperger

Eingebettete Assets & Abhängigkeiten:
[Montserrat](https://github.com/JulietaUla/Montserrat) (SIL OFL 1.1, als
Bitmap-Font eingebettet) · [Noto Emoji](https://github.com/googlefonts/noto-emoji)
(Apache-2.0) · [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) ·
[XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) ·
[ArduinoJson](https://arduinojson.org) ·
[QRCode](https://github.com/ricmoo/QRCode) ·
Daten: [aWATTar API](https://www.awattar.at/services/api) &
[energy-charts.info](https://energy-charts.info) (Fraunhofer ISE)
