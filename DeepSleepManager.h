#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================
//  DeepSleepManager
//
//  Verwaltet den Deep-Sleep-Zyklus des ESP32.
//  Im Deep Sleep wird der gesamte RAM gelöscht — Zustand der
//  zwischen Zyklen erhalten bleiben muss (ETag, letztes Bild)
//  liegt in LittleFS, nicht im RTC-RAM.
//
//  RTC-RAM (überlebt Deep Sleep):
//    - Bootcount für Diagnose
//    - Flag ob Erststart (für Startbildschirm-Logik)
// ============================================================

// Struktur im RTC-RAM — überlebt Deep Sleep
struct RtcData {
    uint32_t bootCount;       // Zählt jeden Wake-Up
    uint32_t lastNtpSync;     // Unix-Zeit des letzten NTP-Syncs (0 = nie)
    uint8_t  wifiBssid[6];    // BSSID des zuletzt verbundenen APs (Schnellverbindung)
    uint8_t  wifiChannel;     // WLAN-Kanal des zuletzt verbundenen APs
    bool     wifiValid;       // true = WLAN-Cache (BSSID/Kanal) gültig
    uint32_t crc;             // Integritätsprüfung über alle Felder
};

class DeepSleepManager {
public:
    DeepSleepManager();

    // RTC-Daten laden / initialisieren
    void begin();

    // true wenn Gerät aus Deep Sleep aufgewacht ist (kein Kaltstart)
    bool isWakeFromSleep() const;

    // Anzahl der bisherigen Boots seit letztem Power-Cycle
    uint32_t getBootCount() const;

    // In Deep Sleep gehen für UPDATE_INTERVAL_SEC Sekunden.
    // Kehrt NICHT zurück — ESP32 startet danach neu.
    void sleepUntilNextUpdate();

    // Berechnet wie viele Sekunden bis zum nächsten aktiven
    // Zeitfenster (Mo–Fr 08:00) — für intelligenten Sleep.
    // Gibt UPDATE_INTERVAL_SEC zurück wenn im aktiven Fenster.
    uint64_t secondsUntilNextActiveWindow() const;

    // Wakeup-Grund als lesbarer String
    String getWakeupReason() const;

    // --- WLAN-Schnellverbindung (Cache im RTC-RAM) ---
    // true wenn ein gültiger BSSID/Kanal-Cache vorliegt
    bool hasWifiCache() const;
    uint8_t getWifiChannel() const;
    const uint8_t* getWifiBssid() const;
    // BSSID + Kanal des verbundenen APs merken (nach erfolgreichem Connect)
    void storeWifiCache(uint8_t channel, const uint8_t* bssid);
    // Cache verwerfen (z.B. wenn Schnellverbindung fehlschlug)
    void clearWifiCache();

    // --- NTP-Sync-Zeitstempel (Cache im RTC-RAM) ---
    // Unix-Zeit des letzten erfolgreichen NTP-Syncs (0 = nie)
    uint32_t getLastNtpSync() const;
    void     setLastNtpSync(uint32_t epoch);

private:
    RtcData   _rtcData;
    bool      _wakeFromSleep;

    uint32_t calcCrc(const RtcData& d) const;
    void     saveRtcData();
};
