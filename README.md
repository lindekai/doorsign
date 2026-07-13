# DoorSign // Digitales Türschild für Konferenzräume

ESP32 + Waveshare 7.5" E-Ink Display zeigt Raumbelegung aus einem Kalender-Server an.
Der Server rendert pro Raum ein Bild (PNG oder BMP), der ESP32 lädt und zeigt es an.

---

## Inhaltsverzeichnis

1. [Funktionsübersicht](#funktionsübersicht)
2. [Hardware](#hardware)
3. [Pinbelegung](#pinbelegung)
4. [Architektur](#architektur)
5. [Bibliotheken installieren](#bibliotheken-installieren)
6. [Gerät konfigurieren](#gerät-konfigurieren)
7. [Kompilieren und Flashen](#kompilieren-und-flashen)
8. [Betriebsmodi](#betriebsmodi)
9. [Bildformat](#bildformat)
10. [Display-Versionen V1 / V2 / V3](#display-versionen-v1--v2--v3)
11. [Stromversorgung](#stromversorgung)
12. [Energieverbrauch und Akku-Laufzeit](#energieverbrauch-und-akku-laufzeit)
13. [OTA-Updates](#ota-updates)
14. [Fehlersuche](#fehlersuche)
15. [Projektstruktur](#projektstruktur)
16. [Bekannte Einschränkungen](#bekannte-einschränkungen)

---

## Funktionsübersicht

- Lädt regelmäßig ein Bild (PNG oder BMP) vom Server und zeigt es auf dem E-Ink-Display
- Aktiv nur Mo–Fr, 07:55–17:00 Uhr (minutengenau konfigurierbar, Europe/Berlin, Sommerzeit automatisch)
- Außerhalb der Betriebszeit: letztes Bild bleibt sichtbar, ESP32 schläft
- **Drei Display-Versionen** unterstützt: V1, V2 und V3 (mit Rot-Kanal)
- **Zwei Bildformate** umschaltbar: PNG (1-Bit/Graustufen) und BMP (1-Bit oder 4-Bit Palette)
- **Zwei Betriebsmodi**: Deep Sleep (Akku) und Dauerbetrieb (Netzteil)
- HTTP-ETag-Caching → kein Display-Refresh wenn Bild unverändert
- Letztes Bild in LittleFS persistiert → überlebt Stromausfall
- OTA-Firmware-Updates über WLAN

---

## Hardware

| Komponente | Beschreibung |
|---|---|
| ESP32 | Waveshare ESP32 Driver Board |
| Display | Waveshare 7.5" E-Paper V1, V2 oder V3 |
| Stromversorgung | USB-C 5V/1A Netzteil oder LiPo-Akku 2000–3300 mAh |

---

## Pinbelegung

Waveshare ESP32 Driver Board ↔ E-Paper HAT:

| E-Paper Pin | ESP32 GPIO | Funktion |
|---|---|---|
| VCC | 3V3 | Versorgungsspannung |
| GND | GND | Masse |
| DIN / MOSI | GPIO 14 | SPI Daten |
| SCLK | GPIO 13 | SPI Takt |
| CS | GPIO 15 | Chip Select (aktiv LOW) |
| DC | GPIO 27 | Data/Command |
| RST | GPIO 26 | Reset (aktiv LOW) |
| BUSY | GPIO 25 | Busy-Signal |

Alle Pins in `config.h` (`PIN_EPD_*` und `PIN_SPI_*`) konfigurierbar.

---

## Architektur

### Deep Sleep Modus (Akku)

```
Boot / Wake-Up
  │
  ├─ Kaltstart? → Startbildschirm anzeigen
  ├─ WLAN verbinden  →  Fehler? → letztes Bild → Sleep
  ├─ OTA-Fenster 60s (nur bei Kaltstart)
  ├─ NTP synchronisieren
  ├─ Zeitfenster prüfen (Mo–Fr 07:55–17:00)
  │   └─ Inaktiv? → Sleep bis 07:55
  ├─ Bild laden (ETag-Check)
  │   ├─ Neu  → Display aktualisieren
  │   ├─ 304  → kein Update
  │   └─ Fehler → letztes Bild behalten
  └─ Display hibernate → Deep Sleep N Minuten
```

### Dauerbetrieb (Netzteil)

State-Machine mit 7 Zuständen:
`BOOT → WIFI_CONNECTING → NTP_SYNCING → IDLE → DOWNLOADING → UPDATING_DISPLAY → ERROR_RECOVERY`

---

## Bibliotheken installieren

Arduino IDE → Sketch → Bibliotheken einbinden → Bibliotheken verwalten

| Bibliothek | Autor | Suchbegriff |
|---|---|---|
| GxEPD2 | ZinggJM | `GxEPD2` |
| Adafruit GFX Library | Adafruit | `Adafruit GFX` |
| PNGdec | Larry Bank | `PNGdec` |

WiFi, HTTPClient, LittleFS und ArduinoOTA sind im ESP32-Board-Paket enthalten.

---

## Gerät konfigurieren

Alle Einstellungen in `config.h`.

### Schritt 1 — Geräteblock auswählen

```cpp
#define DEVICE_NAME    "DoorSign-Brandenburg"
#define ROOM_NAME      "Brandenburg"
#define IMAGE_URL      "https://shelf.example.com/348896.bmp"
#define WIFI_SSID      "MeinNetzwerk"
#define WIFI_PASSWORD  "MeinPasswort"
```

### Schritt 2 — Display-Version

```cpp
#define DISPLAY_TYPE  DISPLAY_V1   // V1: 640×384, S/W
// #define DISPLAY_TYPE  DISPLAY_V2   // V2: 800×480, S/W
// #define DISPLAY_TYPE  DISPLAY_V3   // V3: 800×480, S/W/Rot
```
Auflösung und Treiber werden automatisch aus dieser Einstellung abgeleitet.

### Schritt 3 — Betriebsmodus

```cpp
#define DEEP_SLEEP_ENABLED  1   // 1 = Akku, 0 = Netzteil
```

### Schritt 4 — Update-Intervall

```cpp
#define UPDATE_INTERVAL_SEC  (20UL * 60UL)  // 20 Minuten (Standard)
// Empfehlungen:
//   Netzteil:  5UL * 60UL
//   Akku:     20UL * 60UL
//   Akku:     30UL * 60UL
```

Aktivfenster minutengenau in `config.h`:
```cpp
#define ACTIVE_START_MIN  (7 * 60 + 55)   // 07:55 Uhr
#define ACTIVE_END_MIN    (17 * 60 + 0)   // 17:00 Uhr (letzter Abgleich)
```

### Schritt 5 — Bildformat

```cpp
#define IMAGE_FORMAT_PNG  1   // PNG aktiv (Standard)
#define IMAGE_FORMAT_BMP  0   // oder umgekehrt für BMP
```

### Schritt 6 — Bildqualität (nur PNG)

```cpp
#define IMG_GRAY_THRESHOLD  180  // 128=Standard, 180=empfohlen
```

---

## Kompilieren und Flashen

### Board-Einstellungen

| Einstellung | Wert |
|---|---|
| Board | ESP32 Dev Module |
| Upload Speed | 921600 |
| Flash Size | 4MB (32Mb) |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA)** |
| PSRAM | Disabled |

> **Wichtig:** Standard-Partition „Default 4MB with spiffs" hat nur 1,25 MB für die App
> — reicht NICHT mit OTA. Falls Partition nicht umstellbar: `#define OTA_ENABLED 0`.

### Flash-Vorgang

1. `config.h` öffnen → Gerät, Display, Modus konfigurieren
2. Sketch → Hochladen (`Cmd+U` / `Ctrl+U`)
3. Seriellen Monitor öffnen: 115200 Baud
4. Startsequenz beobachten

### Mehrere Geräte

`config.h` vor jedem Flash anpassen — Firmware ist identisch.

---

## Betriebsmodi

### Deep Sleep — `DEEP_SLEEP_ENABLED 1` (Akku)

**Ablauf pro Wake-Up (~12–15 Sekunden):**
1. Boot (~1s)
2. WLAN verbinden (~3–5s)
3. NTP sync (~1s)
4. Bild herunterladen (~2–5s)
5. E-Ink Refresh (~3s V1/V2 / ~7s V3)
6. Deep Sleep

**OTA im Deep-Sleep-Modus:**
Nur beim Kaltstart (Reset oder Stromunterbrechung) — 60s-Fenster.

**Intelligenter Sleep:**
Außerhalb Mo–Fr 07:55–17:00 schläft das Gerät direkt bis zum nächsten Werktag 07:55.

### Dauerbetrieb — `DEEP_SLEEP_ENABLED 0` (Netzteil)

WLAN permanent aktiv, State Machine prüft alle `UPDATE_INTERVAL_SEC` Sekunden.
OTA jederzeit möglich.

---

## Bildformat

### PNG — `IMAGE_FORMAT_PNG 1`

| Eigenschaft | Wert |
|---|---|
| Farbraum | Graustufen oder 1-Bit |
| Typische Dateigröße | 4–30 KB |
| Max. Größe | 200 KB |
| Dekodierung | ~100ms (PNGdec) |
| Graustufen-Wandlung | via `IMG_GRAY_THRESHOLD` |

```python
from PIL import Image, ImageDraw
img = Image.new("1", (800, 480), 1)  # 1-Bit für schärfstes Ergebnis
draw = ImageDraw.Draw(img)
draw.text((10, 10), "Brandenburg", fill=0)
img.save("image.png")
```

### BMP — `IMAGE_FORMAT_BMP 1`

| Eigenschaft | Wert |
|---|---|
| Bit-Tiefe | **1-Bit oder 4-Bit** (16 Farben) |
| Format | Windows BMP, unkomprimiert |
| Typische Dateigröße | 30 KB (1-Bit) bis 192 KB (4-Bit) |
| Max. Größe | 256 KB |
| Dekodierung | ~5–20ms (direktes Bit-Mapping) |

**1-Bit BMP** (klassisch S/W):
```python
img = Image.new("1", (800, 480), 1)
img.save("image.bmp")
```

**4-Bit BMP** (für V3 mit Rot — oder S/W mit Anti-Aliasing):
```python
# 4-Bit Palette mit Schwarz, Weiß, Rot
palette = [0,0,0, 255,255,255, 255,0,0] + [0,0,0]*13
img = Image.new("P", (800, 480), 1)
img.putpalette(palette)
# ... draw ...
img.save("image.bmp")
```

Der ESP32 erkennt anhand der Palette automatisch:
- **Helligkeit < 128** → schwarzer Pixel
- **R≥150 G<120 B<120** → roter Pixel (nur V3, sonst → schwarz)
- **sonst** → weißer Pixel

### ETag-Unterstützung (empfohlen)

Server sollte `ETag`-Header liefern. Bei unverändertem Bild: `304 Not Modified`
→ kein Download, kein Display-Refresh.

```nginx
location /api/room/ {
    etag on;
    add_header Cache-Control "no-cache";
}
```

---

## Display-Versionen V1 / V2 / V3

| Version | Modell | Auflösung | Farben | Refresh |
|---|---|---|---|---|
| **V1** | GDEW075T8 | 640×384 | S/W | ~3s |
| **V2** | GDEW075T7 | 800×480 | S/W | ~3,5s |
| **V3** | GDEW075Z90 | 800×480 | S/W/**Rot** | ~7s |

In `config.h` umstellen — eine Zeile:
```cpp
#define DISPLAY_TYPE  DISPLAY_V2
```

### V3 — Rot-Kanal nutzen

Der Renderer nutzt automatisch den Rot-Kanal des V3-Displays, wenn der Server
eine 4-Bit-BMP mit roten Palette-Einträgen liefert (R≥150, G<120, B<120).
Auf V1/V2 wird Rot zu Schwarz konvertiert — gleiches Bild funktioniert auf allen
Displays.

---

## Stromversorgung

### Netzteil-Betrieb
USB-C 5V/1A direkt am Board. Kein zusätzliches Modul nötig.

### Akku-Betrieb (LiPo)
Ein einzelner 3,7V LiPo reicht **nicht** für VIN (5V Mindestspannung am Board-Regler).
Empfohlene Schaltung:

```
USB-C → Lade-Modul (TP4056) → LiPo → Boost-Converter (MT3608) → 5V → ESP32 VIN
                                                                  └─ + 1500µF Elko
```

Wichtig:
- **Boost-Converter vor Anschluss auf exakt 5V einstellen** (Poti)
- **1500µF Elko** zwischen 5V und GND direkt am ESP32 zur Pufferung der WLAN-Stromspitzen
- **Tiefentladeschutz** für den LiPo (oft im TP4056-Modul integriert)

### Brownout-Diagnose

Symptom `E BOD: Brownout detector was triggered` im seriellen Monitor →
Stromversorgung kann WLAN-Spitzen (~500mA) nicht liefern. Lösung: dickerer Elko
oder stabileres Netzteil.

---

## Energieverbrauch und Akku-Laufzeit

### Verbrauch pro Wake-Up-Zyklus

| Phase | Dauer | Strom |
|---|---|---|
| Boot + WLAN + NTP | ~5s | 180 mA |
| Download | ~0,5–2s | 180 mA |
| BMP/PNG Decode | ~0,01–0,1s | 80 mA |
| E-Ink Refresh | 3–7s | 20 mA |
| Deep Sleep | konfigurierbar | 0,01 mA |

### Hochrechnung 3300 mAh LiPo

| Intervall | V1/V2 | V3 |
|---|---|---|
| 5 Min | ~133 Tage | ~124 Tage |
| 15 Min | ~390 Tage | ~360 Tage |
| 30 Min | ~760 Tage | ~710 Tage |
| Dauerbetrieb | ~24h | ~24h |

> Theoretische Werte. In Praxis 10–15% weniger (Selbstentladung, Temperatur, reale Akkukapazität).

### PNG vs BMP

Praktisch identisch — Unterschied < 2% der Laufzeit.
Format-Wahl nach Server-Komfort, nicht nach Akku-Verbrauch.

---

## OTA-Updates

### Netzteil-Betrieb
Jederzeit aktiv. Arduino IDE → Werkzeuge → Port → Netzwerk-Port wählen.

### Akku-Betrieb (Deep Sleep)
1. Reset-Taste oder Strom kurz trennen
2. Startbildschirm erscheint → **60-Sekunden-OTA-Fenster**
3. Arduino IDE → Port → Netzwerk-Port → Hochladen
4. OTA-Passwort eingeben (`OTA_PASSWORD` in `config.h`)

OTA deaktivieren: `#define OTA_ENABLED 0`

---

## Fehlersuche

Serieller Monitor: **115200 Baud**

### Häufige Meldungen

| Meldung | Bedeutung | Lösung |
|---|---|---|
| `HTTP Response: 304` | Bild unverändert — normal | — |
| `HTTP-Fehler: -1` | Server nicht erreichbar | URL + WLAN prüfen |
| `Falsche Dimensionen` | Server-Auflösung passt nicht | `DISPLAY_TYPE` prüfen |
| `validateBmp: Bit-Tiefe X (erwartet 1 oder 4)` | BMP nicht unterstützt | Server auf 1- oder 4-Bit umstellen |
| `Datei zu gross` | > `IMG_MAX_BYTES` | URL prüfen / Server-Output verkleinern |
| `Sketch zu groß` | Falsche Partition | Minimal SPIFFS wählen |
| `Brownout detector triggered` | Stromversorgung schwach | Elko + stärkeres Netzteil |

### Reset-Ursachen

| Reset | Bedeutung |
|---|---|
| `rst:0x5 (DEEPSLEEP_RESET)` | Aufwachen aus Deep Sleep — **normal** |
| `rst:0x1 (POWERON_RESET)` | Kaltstart — normal |
| `rst:0x8 (TG1WDT_SYS_RESET)` | Watchdog — Problem |
| `rst:0xc (SW_CPU_RESET)` | Software-Reset / OTA |

### Display zeigt nichts

- `_Update_Full` Wert prüfen — V1 ~2,6s, V2 ~3,5s, V3 ~7s
- Werte deutlich kleiner → falscher `DISPLAY_TYPE`
- Server-Auflösung muss zur Display-Version passen

---

## Projektstruktur

```
DoorSign/
├── DoorSign.ino              # Hauptdatei (setup/loop)
├── config.h                  # ⚙️ ALLE Einstellungen
├── Logger.h                  # Logging
├── WifiManager.h/.cpp        # WLAN mit Auto-Reconnect
├── TimeManager.h/.cpp        # NTP + Europe/Berlin
├── ImageManager.h/.cpp       # HTTP, ETag, PNG+BMP Validierung
├── DisplayManager.h/.cpp     # GxEPD2, V1/V2/V3, PNG+BMP Renderer
├── StateMachine.h/.cpp       # Zustandsautomat (Dauerbetrieb)
├── DeepSleepManager.h/.cpp   # Deep Sleep + intelligenter Wakeup
├── README.md                 # Diese Datei
├── CHANGELOG.md              # Versionshistorie
└── GITHUB_ANLEITUNG.md       # Schritt-für-Schritt Git-Setup

```

---

## Bekannte Einschränkungen

- **HTTPS:** Kein Zertifikats-Check (`setInsecure()`). Für öffentliche Server CA hinterlegen.
- **PNG:** Schwarzweiß-Ausgabe. Farb-PNGs werden via Schwellwert gewandelt — Rot-Kanal aus PNG wird derzeit nicht für V3 erkannt (nur 4-Bit BMP).
- **BMP:** 1-Bit oder 4-Bit Palette, unkomprimiert. 8/24-Bit BMP werden abgelehnt.
- **OTA im Deep-Sleep:** Nur 60s nach Kaltstart.
- **WLAN:** Kein 802.1X/RADIUS (Enterprise-WLAN).
- **Zwei Geräte:** Identische Firmware, `config.h` pro Flash anpassen.
- **Partition:** Erfordert „Minimal SPIFFS" oder OTA deaktivieren.

---

## Lizenz

MIT License frei verwendbar für private und kommerzielle Projekte.
