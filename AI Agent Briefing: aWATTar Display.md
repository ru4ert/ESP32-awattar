Projekt-Spezifikation: aWATTar Strompreis-Monitor — v2 (Stand Juli 2026)

> **Für KI-Agenten:** Die technische Wahrheit (Hardware-Quirks, Architektur,
> Build, Fallstricke) steht in **CLAUDE.md** — die IMMER zuerst lesen.
> Dieses Dokument beschreibt Produkt-Ziel, Umsetzungsstand und Backlog.
> Die ursprüngliche v1-Spezifikation ist in der Git-Historie erhalten.

1. Zielsetzung (erreicht)

Firmware für ein ESP32-Touch-Display, die aktuelle und zukünftige Strompreise
(aWATTar Österreich) visuell darstellt. Kernidee gegenüber v1 erweitert: Es
wird nicht der Börsenpreis angezeigt, sondern der TATSÄCHLICH bezahlte
Gesamtpreis pro kWh (Börse + aWATTar-Aufschlag + Netzentgelte inkl.
SNAP-Rabatt + Abgaben + USt) — nur so ist „ist Strom gerade günstig?"
ehrlich beantwortbar.

2. Hardware (Ist-Zustand)

- ESP32 + 2,8" TFT 320×240, resistiver Touch XPT2046 (Stiftbedienung)
- Panel: ST7789-CLONE („HW-458") mit invertierter Farbdarstellung und
  R/B-Tausch → Software-Kompensation, Details in CLAUDE.md
- Verdrahtung nach CYD-Pinout (TFT: HSPI 12/13/14/15/2, BL 21;
  Touch: VSPI 25/32/33/36/39)

3. Umsetzungsstand (main-Branch)

Ansicht 1 – Dashboard: Gesamtpreis in Font 7, Börse-Referenzzeile mit
SNAP-Hinweis, Einstufung GESCHENKT/GÜNSTIG/MODERAT/TEUER, Strommix-Badge
(🔋 ≥75 % Erneuerbare / 🛢 darunter, energy-charts.info), Trend-Pfeil,
Refresh-Button, Tap → Chart.

Ansicht 2 – Tagesverlauf: 24 farbcodierte Balken (Gesamtpreis), dynamische
Y-Achse, gestrichelte Ø-Linie mit Wert, SNAP-Fenster (Apr–Sep 10–16 Uhr)
grün hinterlegt, Jetzt-Markierung, Buttons Heute / Morgen (ausgegraut bis
~14:00) / Zurück. Tap auf Balken → Tooltip; zweiter Tap → Ansicht 3.

Ansicht 3 – Preis-Details: vollständige Aufschlüsselung (Börse, aWATTar,
Netznutzung ggf. mit SNAP, Netzverlust, Abgaben, USt, Summe) + QR-Code zur
öffentlichen Doku-Seite (docs/index.html via GitHub Pages).

System: NTP mit Sommerzeit, stündlicher Fetch + manueller Refresh,
Display-Sleep 30 s mit View-Reset, deutsche Texte mit echten Umlauten
(eigener Font-Renderer), Tarifwerte zentral in include/tarif.h
(validiert gegen reale aWATTar-Rechnung 06/2026, Wiener Netze).

4. Bewusste Architektur-Entscheidungen

- Natives TFT_eSPI statt LVGL: Die LVGL-Migration liegt geparkt auf
  feature/touch-simulator-snapshot (inkl. Wokwi-Sim). Auf dem Clone-Panel
  war LVGL fehleranfällig; nativ ist auf dieser Hardware bewiesen.
- Farb-/Rotations-Register des Panels werden NIE angefasst (Clone-Quirk),
  Farben werden per PANEL()-Makro in Software vorkompensiert.
- Nur kWh-VARIABLE Kosten im Preis (Grenzpreis-Logik) — Fixkosten wie
  Grundgebühr/Pauschalen bewusst ausgenommen.

5. Backlog

- Wetter-/Preis-Prognose 1–2 Wochen (v1-Idee, weiter offen): externe
  Wind-/Solarprognose als Trend-Indikator; Code ist modular genug
  (zweiter Fetch analog fetchRenewableShare()).
- GitHub Pages aktivieren (Settings → Pages → main + /docs), damit der
  QR-Code in Ansicht 3 auflöst.
- Optional: Preise/Schwellen über Doku-Seite oder Captive Portal
  konfigurierbar statt Compile-Zeit.
