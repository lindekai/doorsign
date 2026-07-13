# Changelog — DoorSign

## Version 1.2 — Robustheit

### Behobene Fehler
- **Akku-Schutz bei NTP-Ausfall** — ohne gültige Zeit schläft das Gerät jetzt
  `NTP_FAIL_SLEEP_SEC` (30 min) statt `UPDATE_INTERVAL_SEC`. Verhindert häufiges
  Aufwachen und Batterie-Entleerung bei anhaltendem NTP-Ausfall
  (`DeepSleepManager.cpp`, `config.h`)
- **Abgeschnittene Downloads werden erkannt** — Download-Schleife leert den
  Empfangspuffer nach Verbindungsende, `write()`-Rückgabe wird geprüft, und die
  geschriebene Bytezahl wird gegen `Content-Length` verifiziert. Verhindert, dass
  ein unvollständiges Bild das letzte gute überschreibt (`ImageManager.cpp`)
- **Stärkere PNG-Validierung** — `validatePng()` prüft nun IHDR-Dimensionen und
  den IEND-Chunk am Dateiende (definitiver Trunkierungs-Detektor), nicht nur die
  Signatur (`ImageManager.cpp`)
- **Wirklich atomarer Bild-Austausch** — `rename` wird zuerst versucht, sodass ein
  fehlgeschlagener Tausch das letzte gute Bild nicht mehr vernichten kann
  (`ImageManager.cpp`)

### Härtung
- **Compile-Guards** in `config.h`: ungültiger `DISPLAY_TYPE` oder eine falsche
  Bildformat-Wahl (keins/beide aktiv) bricht jetzt sichtbar zur Compile-Zeit ab

## Version 1.1 (Final)

### Neue Features
- **4-Bit BMP-Support** — Server kann nun auch 4-Bit-Palette-BMPs liefern (16 Farben)
- **V3-Display Rot-Kanal** — bei 4-Bit BMP mit roten Palette-Einträgen wird Rot
  tatsächlich rot dargestellt; auf V1/V2 wird es zu Schwarz konvertiert
- **`IMG_MAX_BYTES` für BMP** auf 256 KB erhöht (vorher 64 KB)

### Verbesserte Validierung
- `validateBmp()` akzeptiert jetzt 1-Bit UND 4-Bit
- Compression-Check (nur unkomprimierte BMPs)

## Version 1.0

### Funktionen
- Zwei Betriebsmodi: Deep Sleep (Akku) und Dauerbetrieb (Netzteil)
- Drei Display-Versionen: V1 (640×384 S/W), V2 (800×480 S/W), V3 (800×480 S/W/Rot)
- Zwei Bildformate: PNG und BMP, umschaltbar via `IMAGE_FORMAT_PNG`/`IMAGE_FORMAT_BMP`
- HTTP ETag/304-Caching für minimalen Display-Refresh
- Intelligenter Sleep außerhalb der Betriebszeiten (Mo–Fr 08:00–18:00)
- LittleFS-Persistenz für letztes Bild + Metadaten
- OTA-Firmware-Updates (60s-Fenster im Sleep-Modus)
- Robustes Wiederherstellen nach Stromausfall

### Behobene Hardware-Probleme während der Entwicklung
- GxEPD2-Header-Pfad: `<epd/...>` (V1/V2) bzw. `<epd3c/...>` (V3) — nicht direkt
- SPI-Pins müssen vor UND nach `_display.init()` gesetzt werden (Waveshare ESP32 Driver Board)
- ArduinoOTA.begin() erfordert aktives WLAN — lazy initialisieren
- Task-Watchdog-Timeout während E-Ink-Refresh erhöhen (60s statt 5s)
- PNG-Decode-Loop: `yield()` alle 32 Zeilen verhindert TG1WDT-Reset
- PNG-Objekt MUSS auf Heap (`new PNG()`, ~15 KB) — Stack hat nur 8 KB
- E-Ink Settle-Zeit zwischen zwei Full-Refreshes beachten
- `drawBitmap()` statt `writeImage()` für korrekte Bit-Polarität
- V1-Display benötigt `GxEPD2_750`, NICHT `GxEPD2_750_T7`
- Partition Scheme „Minimal SPIFFS" für Sketch-Größe + OTA
- Brownout bei WLAN-Start: dicker Pufferelko (1500µF) am ESP32
