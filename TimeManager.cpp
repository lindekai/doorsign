#include "TimeManager.h"
#include "config.h"
#include "Logger.h"

// ============================================================
//  TimeManager — Implementierung
// ============================================================

TimeManager::TimeManager()
    : _synced(false), _lastSyncMillis(0) {}

void TimeManager::begin() {
    // POSIX Timezone-String für Europe/Berlin setzen.
    // Die TZ-Variable wird von localtime_r() ausgewertet und enthält
    // alle Regeln für Sommer-/Winterzeit automatisch.
    setenv("TZ", TIMEZONE_POSIX, 1);
    tzset();
    logInfo("TIME", "Zeitzone gesetzt: " + String(TIMEZONE_POSIX));
}

bool TimeManager::synchronize() {
    logInfo("TIME", "Starte NTP-Synchronisation...");
    logInfo("TIME", "NTP-Server: " + String(NTP_SERVER_PRIMARY) +
                    ", " + String(NTP_SERVER_SECONDARY));

    // configTime setzt SNTP-Server und löst eine Synchronisation aus.
    // offset-Parameter sind 0, da wir TIMEZONE_POSIX via setenv("TZ") verwenden.
    configTime(0, 0, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);

    // Zeitzone nach configTime erneut setzen (configTime setzt TZ zurück)
    setenv("TZ", TIMEZONE_POSIX, 1);
    tzset();

    // Auf Synchronisation warten (maximal NTP_SYNC_TIMEOUT_MS)
    unsigned long startMs = millis();
    while (millis() - startMs < NTP_SYNC_TIMEOUT_MS) {
        time_t now = time(nullptr);
        // Plausibilitätsprüfung: timestamp > 01.01.2024 00:00:00 UTC
        if (now > 1704067200UL) {
            _synced = true;
            _lastSyncMillis = millis();
            logInfo("TIME", "NTP synchronisiert: " + getTimestamp());
            return true;
        }
        delay(250);  // kurzes Polling — akzeptabel im Init-Pfad
    }

    logError("TIME", "NTP-Synchronisation fehlgeschlagen (Timeout nach " +
                     String(NTP_SYNC_TIMEOUT_MS / 1000) + " Sekunden)");
    _synced = false;
    return false;
}

bool TimeManager::isSynced() const {
    return _synced;
}

bool TimeManager::markSyncedIfValid() {
    // Nach Deep Sleep ist das TimeManager-Objekt frisch (_synced=false), aber
    // die ESP32-RTC hält die Zeit. Ist sie plausibel, gilt sie als synchron —
    // so funktioniert isInActiveWindow() auch ohne erneuten NTP-Sync.
    time_t now = time(nullptr);
    if (now > 1704067200UL) {   // > 01.01.2024
        _synced = true;
        _lastSyncMillis = millis();
        return true;
    }
    return false;
}

bool TimeManager::isInActiveWindow() const {
    if (!_synced) {
        logWarn("TIME", "isInActiveWindow: Zeit nicht synchronisiert");
        return false;
    }

    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);

    // Wochentag prüfen (0=Sonntag, 1=Montag, ..., 6=Samstag)
    if (ti.tm_wday < ACTIVE_WEEKDAY_FROM || ti.tm_wday > ACTIVE_WEEKDAY_TO) {
        return false;
    }

    // Uhrzeit minutengenau prüfen (Minuten seit Mitternacht)
    int minutesOfDay = ti.tm_hour * 60 + ti.tm_min;
    if (minutesOfDay < ACTIVE_START_MIN || minutesOfDay >= ACTIVE_END_MIN) {
        return false;
    }

    return true;
}

String TimeManager::getTimeString() const {
    if (!_synced) return "??:??:??";
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             ti.tm_hour, ti.tm_min, ti.tm_sec);
    return String(buf);
}

String TimeManager::getTimestamp() const {
    if (!_synced) return "????-??-?? ??:??:??";
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[22];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);
    return String(buf);
}

unsigned long TimeManager::millisSinceLastSync() const {
    if (!_synced) return ULONG_MAX;
    return millis() - _lastSyncMillis;
}
