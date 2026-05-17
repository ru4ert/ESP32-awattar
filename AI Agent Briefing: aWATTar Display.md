Projekt-Spezifikation: aWATTar Strompreis-Monitor

1. Zielsetzung

Entwicklung einer Firmware für ein ESP32-basiertes Touch-Display, um aktuelle und zukünftige Strompreise (aWATTar Österreich/Deutschland) visuell ansprechend darzustellen. Das System soll dem Endnutzer sofort signalisieren, ob Strom gerade teuer oder günstig ist, und eine Planung für den Tag/Morgen ermöglichen.

2. Hardware

Board: ESP32-2432S028R (bekannt als "Cheap Yellow Display" / CYD)

Features: ESP32 (WLAN/BT), 2.8" TFT Display (320x240), Resistiver Touchscreen (Stiftbedienung vorgesehen).

3. Entwicklungsumgebung & Tooling

IDE: Antigravity (VSCode-basierter AI-Editor).

Framework: PlatformIO.

Empfohlene Plugins (VSCode-kompatibel):

PlatformIO IDE (Zwingend erforderlich für Build/Flash/Serial Monitor)

C/C++ von Microsoft (Für C++ IntelliSense)

Error Lens (Hebt Syntax-Fehler direkt in der Zeile farblich hervor)

Even Better TOML (Hilft bei der sauberen Formatierung der platformio.ini)

4. API & Daten-Strategie

Endpunkt: https://api.awattar.at/v1/marketdata (bzw. .de je nach Standort).

Intervall: Ein HTTP GET Call exakt 1x pro Stunde (z.B. zur Minute 00). Zusätzlich kann der Nutzer jederzeit über einen Touch-Button einen manuellen Fetch auslösen.

Datenhaltung: JSON-Antwort parsen und die Stundenpreise für heute und morgen in einem C++ Struct/Array im RAM speichern.

5. UI/UX Architektur (Empfehlung: TFT_eSPI)

Framework-Entscheidung: Für eine KI-gestützte Entwicklung wird strikt TFT_eSPI (inkl. direkter Touch-Abfrage über XPT2046) empfohlen, um die Entwicklung simpel und ressourcenschonend zu halten.

Globales Element: Statusleiste (Header)

Position: Oberster Bildschirmrand (auf allen Ansichten sichtbar).

Inhalt: Aktuelle Uhrzeit (NTP), WLAN-Empfangsstärke (Icon/Balken) und ein kleines Lade-Icon, das nur aufblinkt, wenn gerade im Hintergrund ein API-Fetch läuft.

Ansicht 1: Dashboard (Standard)

Aktion: Ein kleiner "Refresh"-Button (Icon) für manuellen API-Fetch in der oberen Ecke.

Zentrum: Aktueller Strompreis (Cent/kWh) in sehr großem Font.

Farbcodierter Hintergrund:

Grün: < 10 ct (oder negativ)

Gelb: 10 - 20 ct

Rot: > 20 ct

Footer: Trend-Indikator für die nächste Stunde (Pfeil hoch/runter).

Touch-Aktion: Ein Tap auf den Hauptbereich wechselt zu Ansicht 2.

Ansicht 2: Tagesverlauf (Chart)

Visualisierung: Ein Balkendiagramm der 24 Stunden des gewählten Tages. Jeder Balken übernimmt die Farblogik (Grün/Gelb/Rot) basierend auf seinem Preis.

Durchschnitts-Linie: Eine horizontale, gestrichelte Linie quer über den Chart markiert den Tagesdurchschnittspreis als visuelle Hilfestellung.

Interaktion (Touch-Tooltip): Ein Tap mit dem Stift auf einen spezifischen Balken zeigt oben rechts (oder schwebend) temporär die genaue Uhrzeit und den Preis an (z.B. "18:00 - 19:00: 24,5 ct").

Navigation (Touch-Buttons):

Button "Heute"

Button "Morgen" (Ausgegraut, falls die API noch keine Daten für morgen geliefert hat - meist vor 14:00 Uhr).

Button "Zurück" (zum Dashboard).

6. Referenz-Projekte & Bibliotheken (Für den Agenten)

Der Agent soll sich beim Hardware-Setup (Pin-Belegung) an folgendem GitHub-Repository orientieren:

Hardware Baseline: witnessmenow/ESP32-Cheap-Yellow-Display

TFT-Pins: MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, RST -1, BL 21

Touch-Pins (XPT2046 auf VSPI): CS 33, IRQ 36, MOSI 32, MISO 39, CLK 25.

Benötigte PlatformIO lib_deps:

bodmer/TFT_eSPI (Display)

paulstoffregen/XPT2046_Touchscreen (Touch)

bblanchon/ArduinoJson (API Parsing)

arduino-libraries/NTPClient (Zeitsynchronisierung)

7. Start-Anweisung für den AI Agenten

Setze die platformio.ini mit den exakten Build-Flags für das CYD-Board auf.

Implementiere die WLAN- und NTP-Verbindung.

Baue die Fetch-Logik für die aWATTar-Daten (stündlich + manueller Trigger über den Screen).

Setze das UI-Layout mit der Statusleiste und den Touch-Zonen für den Wechsel zwischen Dashboard, Chart und dem Refresh-Button um. Achte auf die korrekte Implementierung der Tooltips im Chart.

8. Ausblick & Zukünftige Erweiterungen (Backlog)

1-2 Wochen Wetter/Preis-Prognose: Da die aWATTar API (Day-Ahead Markt) systembedingt nur Preise bis maximal morgen Mitternacht liefert, soll in Zukunft evaluiert werden, ob eine externe KI/Wetter-API angebunden werden kann. Diese soll auf Basis von Wind- und Solarprognosen einen groben Preistrend für die nächsten 1 bis 2 Wochen vorhersagen. Die Code-Struktur sollte so modular sein, dass ein solcher zweiter API-Call später leicht integriert werden kann.