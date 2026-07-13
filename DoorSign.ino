// ============================================================
//  DoorSign.ino — Digitales Türschild für Konferenzräume V.1.1
//  https://github.com/lindekai/DoorSign/
//
//  Betriebsmodi (config.h: DEEP_SLEEP_ENABLED):
//
//  0 = Netzteil-Betrieb:
//      Dauerhafter Loop, State Machine, WLAN immer aktiv.
//      OTA jederzeit möglich. Update-Intervall per millis().
//
//  1 = Akku-Betrieb (Deep Sleep):
//      setup() → WLAN → NTP → Download → Display → Sleep.
//      loop() läuft nur kurz für OTA-Fenster (60s), dann Sleep.
//      Kein State-Machine-Overhead. Stromverbrauch ~0,01mA im Sleep.
//
//  Bibliotheken (Arduino Library Manager):
//    - GxEPD2 by ZinggJM
//    - Adafruit GFX Library
//    - PNGdec by Larry Bank
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

#include "config.h"
#include "Logger.h"
#include "WifiManager.h"
#include "TimeManager.h"
#include "ImageManager.h"
#include "DisplayManager.h"

#if !DEEP_SLEEP_ENABLED
  #include "StateMachine.h"
#else
  #include "DeepSleepManager.h"
#endif

// ============================================================
//  Objekte
// ============================================================
WifiManager    wifiManager(WIFI_SSID, WIFI_PASSWORD);
TimeManager    timeManager;
ImageManager   imageManager;
DisplayManager displayManager;

#if !DEEP_SLEEP_ENABLED
  StateMachine stateMachine(&wifiManager, &timeManager,
                            &imageManager, &displayManager);
#else
  DeepSleepManager sleepManager;
#endif

static bool _otaStarted = false;
void registerOTAHandlers();

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(200);

    Serial.println();
    Serial.println("=========================================");
    Serial.println("  DoorSign -- Digitales Konferenzschild  ");
    Serial.println("=========================================");
    logInfo("MAIN", "Geraet:   " + String(DEVICE_NAME));
    logInfo("MAIN", "Raum:     " + String(ROOM_NAME));
    logInfo("MAIN", "Firmware: " + String(__DATE__) + " " + String(__TIME__));
    logInfo("MAIN", "Modus:    " + String(DEEP_SLEEP_ENABLED ? "Deep Sleep" : "Dauerbetrieb"));
    logHeap("MAIN");

    // ---- Hardware Init ----
    if (!imageManager.begin()) {
        logError("MAIN", "LittleFS Init fehlgeschlagen!");
    }
    timeManager.begin();
    displayManager.begin();

#if OTA_ENABLED
    registerOTAHandlers();
#endif

#if DEEP_SLEEP_ENABLED
    // ============================================================
    //  DEEP SLEEP MODUS — Lineare Ausführung, kein Loop
    // ============================================================
    sleepManager.begin();
    logInfo("MAIN", "Boot #" + String(sleepManager.getBootCount()));

    // Startbildschirm nur beim echten Kaltstart zeigen
    if (!sleepManager.isWakeFromSleep()) {
        logInfo("MAIN", "Kaltstart — zeige Startbildschirm");
        displayManager.showStartup("Verbinde mit WLAN: " + String(WIFI_SSID));
    }

    // WLAN verbinden
    if (!wifiManager.connect(WIFI_CONNECT_TIMEOUT_MS)) {
        logError("MAIN", "WLAN fehlgeschlagen — zeige letztes Bild und schlafe");
        if (imageManager.hasStoredImage()) {
            displayManager.showImageFromFile(FS_IMAGE_PATH);
        }
        displayManager.hibernate();
        sleepManager.sleepUntilNextUpdate();
        return; // Wird nie erreicht
    }

#if OTA_ENABLED
    // OTA-Fenster: 60 Sekunden nach dem Boot für OTA-Updates
    // (Nur bei Kaltstart, nicht bei jedem Sleep-Wake-Zyklus)
    if (!sleepManager.isWakeFromSleep()) {
        logInfo("MAIN", "OTA-Fenster: 60 Sekunden...");
        ArduinoOTA.begin();
        _otaStarted = true;
        unsigned long otaStart = millis();
        while (millis() - otaStart < 60000UL) {
            ArduinoOTA.handle();
            delay(10);
        }
        logInfo("MAIN", "OTA-Fenster geschlossen");
    }
#endif

    // NTP synchronisieren
    timeManager.synchronize();

    // Zeitfenster prüfen
    if (timeManager.isSynced() && !timeManager.isInActiveWindow()) {
        logInfo("MAIN", "Ausserhalb Zeitfenster — schlafe bis naechstes Fenster");
        // Letztes Bild bleibt auf Display (E-Ink hält ohne Strom)
        displayManager.hibernate();
        sleepManager.sleepUntilNextUpdate();
        return; // Wird nie erreicht
    }

    // Bild herunterladen
    DownloadResult result = imageManager.downloadImage(IMAGE_URL);
    logInfo("MAIN", "Download: " + ImageManager::resultToString(result));

    // Display aktualisieren wenn neues Bild
    if (result == DownloadResult::SUCCESS_CHANGED) {
        displayManager.showImageFromFile(FS_IMAGE_PATH);
    } else if (result == DownloadResult::SUCCESS_UNCHANGED) {
        logInfo("MAIN", "Bild unveraendert (304) — kein Display-Update");
    } else {
        // Fehler: letztes Bild behalten, ggf. Fallback
        if (!imageManager.hasStoredImage()) {
            displayManager.showFallback();
        }
    }

    // Display in Ruhemodus und schlafen
    displayManager.hibernate();
    sleepManager.sleepUntilNextUpdate();
    // Kehrt nicht zurück

#else
    // ============================================================
    //  DAUERBETRIEB — State Machine
    // ============================================================
    stateMachine.begin();
    logInfo("MAIN", "setup() abgeschlossen");
#endif
}

// ============================================================
//  loop() — nur im Dauerbetrieb aktiv
// ============================================================
void loop() {
#if DEEP_SLEEP_ENABLED
    // Sollte nie erreicht werden — sleepUntilNextUpdate() kehrt nicht zurück
    delay(1000);
#else
    stateMachine.update();

  #if OTA_ENABLED
    if (!_otaStarted && WiFi.isConnected()) {
        ArduinoOTA.begin();
        _otaStarted = true;
        logInfo("OTA", "ArduinoOTA gestartet (IP: " + WiFi.localIP().toString() + ")");
    }
    if (_otaStarted) ArduinoOTA.handle();
  #endif

    delay(10);
#endif
}

// ============================================================
//  OTA-Callbacks
// ============================================================
void registerOTAHandlers() {
    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.setHostname(DEVICE_NAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        logInfo("OTA", "Update gestartet");
    });
    ArduinoOTA.onEnd([]() {
        logInfo("OTA", "Update abgeschlossen");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t last = 255;
        uint8_t pct = (uint8_t)(progress * 100 / total);
        if (pct != last && pct % 10 == 0) {
            logInfo("OTA", String(pct) + "%");
            last = pct;
        }
    });
    ArduinoOTA.onError([](ota_error_t error) {
        logError("OTA", "Fehler: " + String(error));
    });
    logInfo("OTA", "Callbacks registriert");
}
