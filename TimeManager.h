#pragma once
#include <Arduino.h>
#include <time.h>

// ============================================================
//  TimeManager — NTP-Synchronisation, Zeitzone, Aktivfenster
// ============================================================
class TimeManager {
public:
    TimeManager();

    // Zeitzone konfigurieren (muss vor synchronize() aufgerufen werden)
    void begin();

    // NTP-Synchronisation starten.
    // Blockiert maximal NTP_SYNC_TIMEOUT_MS.
    // Gibt true zurück wenn Zeit erfolgreich synchronisiert wurde.
    bool synchronize();

    // true wenn die Zeit mindestens einmal erfolgreich synchronisiert wurde
    bool isSynced() const;

    // Übernimmt eine bereits gültige Systemzeit (aus der RTC, die den Deep
    // Sleep überlebt), ohne neuen NTP-Sync. Setzt intern _synced, wenn die
    // Uhr plausibel ist. Gibt true zurück, wenn die Zeit gültig ist.
    bool markSyncedIfValid();

    // true wenn jetzt Montag–Freitag innerhalb des aktiven Zeitfensters
    // (ACTIVE_START_MIN..ACTIVE_END_MIN, Europe/Berlin)
    bool isInActiveWindow() const;

    // Aktuelle lokale Zeit als "HH:MM:SS"
    String getTimeString() const;

    // Aktuelles Datum + Zeit als "YYYY-MM-DD HH:MM:SS"
    String getTimestamp() const;

    // Millisekunden seit dem letzten erfolgreichen NTP-Sync
    unsigned long millisSinceLastSync() const;

private:
    bool          _synced;
    unsigned long _lastSyncMillis;
};
