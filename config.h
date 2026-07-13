#pragma once

// ============================================================
//  DOORSIGN — GERÄTEKONFIGURATION
//  Für jedes Gerät exakt EINEN Block aktivieren.
// ============================================================

// --- Gerät 1: Konferenzraum Alpha ---
#define DEVICE_NAME    "DoorSign-Alpha"
#define ROOM_NAME      "Konferenzraum Alpha"
#define IMAGE_URL      "https://server.com/konferenzraum-alpha.png"
#define WIFI_SSID      "MeinNetzwerk"
#define WIFI_PASSWORD  "MeinPasswort"

/*
// --- Gerät 2: Konferenzraum Beta ---
#define DEVICE_NAME    "DoorSign-Beta"
#define ROOM_NAME      "Konferenzraum Beta"
#define IMAGE_URL      "https://server.com/konferenzraum-beta.png"
#define WIFI_SSID      "MeinNetzwerk"
#define WIFI_PASSWORD  "MeinPasswort"
*/

// ============================================================
//  BETRIEBSMODUS
//  DEEP_SLEEP_ENABLED 1 = Deep Sleep zwischen Updates (Akku-Betrieb)
//  DEEP_SLEEP_ENABLED 0 = Dauerbetrieb (Netzteil, kein Sleep)
// ============================================================
#define DEEP_SLEEP_ENABLED  1

// ============================================================
//  UPDATE-INTERVALL
//  Im Netzteil-Betrieb (DEEP_SLEEP_ENABLED 0): Polling-Intervall
//  Im Akku-Betrieb    (DEEP_SLEEP_ENABLED 1): Sleep-Dauer
//
//  Empfehlungen:
//    Netzteil: 5  Minuten  → 5UL * 60UL
//    Akku:     15 Minuten  → 15UL * 60UL
//    Akku:     30 Minuten  → 30UL * 60UL
// ============================================================
#define UPDATE_INTERVAL_SEC  (20UL * 60UL)   // 20 Minuten

// Blind-Schlafintervall im Deep-Sleep, wenn KEINE gueltige Zeit vorliegt
// (NTP fehlgeschlagen). Verhindert, dass das Akku-Geraet bei anhaltendem
// NTP-Ausfall alle UPDATE_INTERVAL_SEC aufwacht und funkt → Batterie-Schutz.
#define NTP_FAIL_SLEEP_SEC   (30UL * 60UL)   // 30 Minuten

// ============================================================
//  ZEITKONFIGURATION
// ============================================================
#define NTP_SERVER_PRIMARY    "pool.ntp.org"
#define NTP_SERVER_SECONDARY  "europe.pool.ntp.org"
#define TIMEZONE_POSIX        "CET-1CEST,M3.5.0,M10.5.0/3"

// Aktive Tage: 1=Mo, 2=Di, 3=Mi, 4=Do, 5=Fr, 6=Sa, 0=So
#define ACTIVE_WEEKDAY_FROM   1    // Montag
#define ACTIVE_WEEKDAY_TO     5    // Freitag
// Aktive Uhrzeit — minutengenau, als Minuten seit Mitternacht.
// Start etwas vor 08:00, damit das Schild um 08:00 aktuell ist;
// Ende 17:00 = letzter Abgleich (spart die 17-18-Uhr-Stunde, ~5h/Woche).
#define ACTIVE_START_MIN     (7 * 60 + 55)   // 07:55 Uhr (inklusiv)
#define ACTIVE_END_MIN       (17 * 60 + 0)   // 17:00 Uhr (exklusiv)

// ============================================================
//  TIMEOUTS
// ============================================================
#define WIFI_CONNECT_TIMEOUT_MS    (20UL * 1000UL)
// Kürzeres Timeout für die WLAN-Schnellverbindung (BSSID/Kanal aus RTC-Cache).
// Schlägt sie in dieser Zeit fehl (AP-Reboot/Kanalwechsel), wird auf den
// normalen Scan-Connect zurückgefallen.
#define WIFI_FASTCONNECT_TIMEOUT_MS (8UL * 1000UL)
// NTP nur höchstens einmal pro diesem Intervall neu synchronisieren;
// dazwischen hält die ESP32-RTC die Zeit über den Deep Sleep. Spart Funkzeit.
#define NTP_RESYNC_INTERVAL_SEC    (24UL * 60UL * 60UL)   // 24 Stunden
#define WIFI_RECONNECT_INTERVAL_MS (30UL * 1000UL)
#define NTP_SYNC_TIMEOUT_MS        (15UL * 1000UL)
#define NTP_RETRY_INTERVAL_MS      (60UL * 1000UL)
#define HTTP_TIMEOUT_MS            (15UL * 1000UL)
#define DOWNLOAD_TIMEOUT_MS        (30UL * 1000UL)
// Für Rückwärtskompatibilität (StateMachine nutzt MS-Wert)
#define UPDATE_INTERVAL_MS         (UPDATE_INTERVAL_SEC * 1000UL)

// ============================================================
//  DISPLAY-PINBELEGUNG (Waveshare ESP32 Driver Board)
//  ┌──────────┬────────────┬──────────────────────────────┐
//  │ E-Paper  │ ESP32 GPIO │ Beschreibung                 │
//  ├──────────┼────────────┼──────────────────────────────┤
//  │ VCC      │ 3V3        │ Versorgungsspannung          │
//  │ GND      │ GND        │ Masse                        │
//  │ DIN/MOSI │ GPIO 14    │ SPI Data In                  │
//  │ SCLK     │ GPIO 13    │ SPI Clock                    │
//  │ CS       │ GPIO 15    │ Chip Select (aktiv LOW)      │
//  │ DC       │ GPIO 27    │ Data/Command                 │
//  │ RST      │ GPIO 26    │ Reset (aktiv LOW)            │
//  │ BUSY     │ GPIO 25    │ Busy (aktiv LOW)             │
//  └──────────┴────────────┴──────────────────────────────┘
// [DISPLAY-ABHÄNGIG] Pins anpassen falls andere Verdrahtung:
#define PIN_EPD_CS    15
#define PIN_EPD_DC    27
#define PIN_EPD_RST   26
#define PIN_EPD_BUSY  25
#define PIN_SPI_MOSI  14
#define PIN_SPI_SCK   13
#define PIN_SPI_MISO  -1

// ============================================================
//  DISPLAY-VERSION  [DISPLAY-ABHÄNGIG] — nur EINEN Wert eintragen
// ============================================================
//  V1: Waveshare 7.5" V1 (GDEW075T8)   640x384  Schwarz/Weiss  weisse Rueckseite
//  V2: Waveshare 7.5" V2 (GDEW075T7)   800x480  Schwarz/Weiss  schwarze Rueckseite
//  V3: Waveshare 7.5" V3 (GDEW075Z90)  800x480  Schwarz/Weiss/ROT
//
//  Treiber:
//    V1 → GxEPD2_BW + GxEPD2_750       (Unterordner: epd/)
//    V2 → GxEPD2_BW + GxEPD2_750_T7    (Unterordner: epd/)
//    V3 → GxEPD2_3C + GxEPD2_750c_Z90  (Unterordner: epd3c/)
//
//  Hinweis V3: Server-Bilder bleiben schwarz/weiss.
//  Rot erscheint nur wenn der Renderer explizit GxEPD_RED verwendet.
//  Fuer reine S/W-Bilder vom Server: V3 verhalt sich wie V1/V2.
#define DISPLAY_V1  1
#define DISPLAY_V2  2
#define DISPLAY_V3  3
#define DISPLAY_TYPE  DISPLAY_V1   // ← Display-Version hier eintragen

// Fehlkonfiguration zur Compile-Zeit abfangen:
#if DISPLAY_TYPE != DISPLAY_V1 && DISPLAY_TYPE != DISPLAY_V2 && DISPLAY_TYPE != DISPLAY_V3
  #error "DISPLAY_TYPE muss DISPLAY_V1, DISPLAY_V2 oder DISPLAY_V3 sein"
#endif

// Aufloesung wird automatisch aus DISPLAY_TYPE abgeleitet:
#if DISPLAY_TYPE == DISPLAY_V1
  #define IMG_WIDTH   640
  #define IMG_HEIGHT  384
#else
  // V2 und V3 beide 800x480
  #define IMG_WIDTH   800
  #define IMG_HEIGHT  480
#endif

// ============================================================
//  BILDFORMAT
// ============================================================
// Bildformat wählen — nur EINEN Wert auf 1 setzen:
#define IMAGE_FORMAT_PNG  1   // PNG: kleinere Dateien, Dekodierung auf ESP32
#define IMAGE_FORMAT_BMP  0   // BMP: 1-Bit monochrom, keine Dekodierung nötig
// Genau EIN Format muss aktiv sein — sonst Compile-Fehler:
#if (IMAGE_FORMAT_PNG + IMAGE_FORMAT_BMP) != 1
  #error "Genau EIN Bildformat aktivieren: IMAGE_FORMAT_PNG oder IMAGE_FORMAT_BMP = 1, das andere = 0"
#endif
//
// Vergleich bei 640×384:
//   PNG: ~4–30 KB,  Download schnell, Dekodierung ~100ms
//   BMP: ~48 KB,    Download langsamer, Dekodierung ~5ms
//   Akku-Laufzeit: PNG ca. 2% besser (kaum messbar)
//
// BMP-Anforderungen an den Server:
//   1-Bit monochrom, Windows BMP (BITMAPINFOHEADER)
//   Dateigröße: 14+40+8+(80*384) = 30762 Bytes für 640×384

// [DISPLAY-ABHÄNGIG] V1=640x384, V2=800x480

#if IMAGE_FORMAT_PNG
  #define IMG_MAX_BYTES     (200UL * 1024UL)
  #define FS_IMAGE_PATH     "/image.png"
  #define FS_IMAGE_TMP      "/image_tmp.png"
  // Schwellwert Graustufen → schwarzweiß (0–255)
  // 128=Standard | 180=empfohlen für Text+QR | 200=aggressiv
  #define IMG_GRAY_THRESHOLD  180
#else
  #define IMG_MAX_BYTES     (256UL * 1024UL)  // erhoeht: V2/V3 mit 4-Bit BMPs benoetigen mehr
  #define FS_IMAGE_PATH     "/image.bmp"
  #define FS_IMAGE_TMP      "/image_tmp.bmp"
  // BMP: kein Schwellwert nötig — 1-Bit direkt
  #define IMG_GRAY_THRESHOLD  128   // nur als Fallback definiert
#endif

// Invertierung: 0 = normal, 1 = invertiert
#define DISPLAY_INVERT_IMAGE  0

// ============================================================
//  DATEIPFADE (LittleFS) — Metadaten (format-unabhängig)
// ============================================================
#define FS_ETAG_PATH     "/etag.txt"
#define FS_LASTMOD_PATH  "/lastmod.txt"

// ============================================================
//  OTA (nur im Netzteil-Betrieb sinnvoll)
//  Im Deep-Sleep-Betrieb: OTA nur im 60s-Startfenster nach Boot
// ============================================================
#define OTA_ENABLED   1
#define OTA_PASSWORD  "doorsign_ota_pw"
#define OTA_PORT      3232

// ============================================================
//  SERIELL
// ============================================================
#define SERIAL_BAUD_RATE  115200
