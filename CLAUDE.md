# aWATTar Strompreis-Monitor (ESP32)

2,8"-Touch-Display, das den österreichischen Börsenstrompreis (aWATTar HOURLY)
als **echten Gesamtpreis inkl. Netzentgelten, SNAP-Rabatt, Abgaben und USt**
anzeigt. Aktive Entwicklung auf `main`, natives TFT_eSPI (kein LVGL — siehe
Branch-Historie unten).

## ⚠️ Hardware-Quirks — ZUERST LESEN

Das Panel (Modul „HW-458", wird als `ST7789_DRIVER` gefahren) ist ein Clone
mit zwei Eigenheiten. **Verstöße dagegen haben in diesem Projekt schon Tage
gekostet:**

1. **Farben:** Das Panel zeigt alles invertiert + Rot/Blau vertauscht
   (`Anzeige = invert(swapRB(gesendet))`). Fix ist **rein in Software**:
   jede Farbe wird als `PANEL(0xRGB565)` notiert ([include/panel_color.h](include/panel_color.h)).
   - Neue Farben IMMER mit `PANEL()` wrappen.
   - Berechnete Farben (Aufhellen, Blending) erst im Ziel-Farbraum rechnen,
     dann `PANEL()` anwenden (`PANEL` ist selbstinvers).
   - **NIEMALS** `TFT_INVERSION_*`, `TFT_RGB_ORDER` oder MADCTL-Register
     anfassen — das Bit 3 des MADCTL kippt auf diesem Clone auch die Achsen!
2. **Geometrie/Touch (bewährt, nicht ändern):** `tft.setRotation(3)` +
   `ts.setRotation(3)`, XPT2046-Kalibrierung
   `x = map(rawX, 3790, 210, 0, W)`, `y = map(rawY, 3900, 245, 0, H)`.
3. `tft.setSwapBytes(true)` ist gesetzt — nötig für alle `pushImage()`-Aufrufe
   (Emojis, Font-Glyphen).

## Build & Flash

```bash
pio run -e esp32dev              # bauen
pio run -e esp32dev -t upload    # flashen (USB)
pio device monitor               # Serial 115200
```

`include/secrets.h` ist gitignored — bei frischem Clone anlegen:
```cpp
#define WIFI_SSID     "..."
#define WIFI_PASSWORD "..."
```

## Architektur (alles Wesentliche in src/main.cpp)

| Datei | Inhalt |
|---|---|
| [src/main.cpp](src/main.cpp) | Setup/Loop, WiFi/NTP, beide API-Fetches, alle Screens, Touch, Font-Renderer `drawDE()` |
| [include/tarif.h](include/tarif.h) | Tarifmodell: `priceParts()`/`totalPrice()` = Börse → Gesamtpreis; SNAP-Fenster; Farbschwellen |
| [include/panel_color.h](include/panel_color.h) | `PANEL()`-Farbkompensation (siehe Quirks) |
| [include/font_de.h](include/font_de.h) + [src/font_de.cpp](src/font_de.cpp) | Generierte Montserrat-Fonts 14/10 px (ASCII + ÄÖÜäöüß Ø ° €) |
| [include/img_emoji.h](include/img_emoji.h) | 🔋/🛢 als 24×24-RGB565-Bitmaps (Noto Emoji) |
| [include/tft_config.h](include/tft_config.h) | TFT_eSPI-Pins (via `-include` in platformio.ini) |
| [tools/](tools/README.md) | Generatoren für Fonts & Emojis (Node) |
| [docs/index.html](docs/index.html) | Öffentliche Doku-Seite (GitHub Pages), Ziel des QR-Codes |

### Screens & Bedienung

- **DASHBOARD**: Gesamtpreis groß (Font 7), Börse-Referenzzeile, Einstufung
  (GESCHENKT <0 Börse / GÜNSTIG / MODERAT / TEUER nach Gesamt-Schwellen),
  Strommix-Badge (🔋 ab `RENEW_THRESHOLD` 75 % Erneuerbaren-Anteil, sonst 🛢),
  Trend-Pfeil. Tap auf Fläche → Chart; Refresh-Button oben rechts.
- **CHART**: 24 Balken = Gesamtpreise, Y-Achse dynamisch (5-ct-Raster),
  SNAP-Fenster grün hinterlegt, Ø-Durchschnitt, Heute/Morgen/Zurück-Buttons.
  1. Tap auf Balken = Auswahl (aufgehellt + Tooltip), 2. Tap = DETAIL.
- **DETAIL**: Preis-Aufschlüsselung (alle Posten aus `priceParts()`, Summe
  farbcodiert) + QR-Code zur Doku-Seite. Beliebiger Tap schließt.
- **Sleep**: nach 30 s Backlight aus, View-Reset aufs Dashboard wird sofort
  (unsichtbar) vorgezeichnet; erster Touch weckt nur auf.

### Eigener Font-Renderer

`drawDE(x, yBase, text, FONT_DE14|FONT_DE10, fg, bg)` — **y ist die Baseline!**
UTF-8-fähig, antialiased durch Blending gegen die angegebene Hintergrundfarbe.
Keine Font-Library verwenden (U8g2_for_TFT_eSPI ist mit Unicode nachweislich
kaputt, LVGL siehe unten). Neue Glyphen/Größen: [tools/README.md](tools/README.md).

## Daten & Tarif

- **Preise**: `https://api.awattar.at/v1/marketdata?start=<ms>&end=<ms>`,
  stündlich + manuell. EUR/MWh ÷ 10 = ct/kWh (netto Börse).
- **Strommix**: `https://api.energy-charts.info/public_power?country=at&start=YYYY-MM-DD&end=YYYY-MM-DD`
  — **nur ISO-Datum, keine Unix-Sekunden!** Bevorzugt Serie
  „Renewable share of generation", sonst Namens-Klassifikation.
- **Tarifmodell** ([include/tarif.h](include/tarif.h)): Werte aus der realen
  aWATTar-Rechnung 06/2026 (Wiener Netze). Nur kWh-variable Posten
  (Grenzpreis-Logik), Fixkosten bewusst ausgenommen. SNAP = Apr–Sep,
  10–16 Uhr, −20 % auf Netznutzungs-AP (auf Rechnung verifiziert:
  5,58 = 6,98 × 0,8). Bei Tarifwechsel/Umzug: nur tarif.h anpassen.
- **QR-Doku**: `DOKU_URL` in main.cpp (max. 42 Zeichen, QR v3!) →
  GitHub Pages aus `/docs` (Settings → Pages → main + /docs, Repo muss
  dafür public sein).

## Branches

- `main` — **aktiv**: natives TFT_eSPI, Stand dieser Doku.
- `feature/touch-simulator-snapshot` — geparkte LVGL-8.3-Version inkl.
  Wokwi-Simulator-Env und Runtime-Display-Kalibrierscreen. Auf der realen
  Hardware gab es Artefakte/Orientierungsprobleme (Clone-Panel + LVGL-
  Koordinaten), daher Rückkehr zu nativ. Nur wiederbeleben, wenn ein
  Standard-Panel (echtes CYD) vorliegt.

## Gelernte Fallstricke (nicht wiederholen)

1. Farb-/Rotations-Register des Panels „fixen" → Orientierung kippt.
   Nur Software-Kompensation via `PANEL()`.
2. `U8g2_for_TFT_eSPI` rendert Unicode-Fonts als Pixelblöcke (Issues #3/#6
   im Repo) — deshalb der eigene Renderer.
3. energy-charts-API lehnt Unix-Timestamps ab → ISO-Datum.
4. lv_font_conv liefert `adv_w` in 1/16 px — beim Packen durch 16 teilen
   (der Bug erzeugte „zufällig verstreute Buchstaben").
5. aWATTar-/energy-charts-Fetches blockieren den Loop (kein async) — vor dem
   Fetch `lv_refr_now`-Äquivalent: Spinner zeichnen via `drawHeader()`.
6. DNG-Fotos vom Display sind farblich unzuverlässig konvertiert — für
   Farbdiagnosen HEIC/JPG anfordern.
