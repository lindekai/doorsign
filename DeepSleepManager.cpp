#include "DeepSleepManager.h"
#include "Logger.h"
#include <esp_sleep.h>
#include <time.h>
#include <SPI.h>

// RTC-RAM Sektion — bleibt bei Deep Sleep erhalten
RTC_DATA_ATTR static RtcData rtcStore = {0};

DeepSleepManager::DeepSleepManager() : _wakeFromSleep(false) {}

void DeepSleepManager::begin() {
    // Wakeup-Grund ermitteln
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    _wakeFromSleep = (cause == ESP_SLEEP_WAKEUP_TIMER);

    // RTC-Daten validieren
    if (rtcStore.crc != calcCrc(rtcStore) || !_wakeFromSleep) {
        // Kaltstart oder korrupte Daten → gesamten RTC-Store initialisieren,
        // damit kein veralteter WLAN-/NTP-Cache weiterverwendet wird.
        rtcStore.bootCount   = 0;
        rtcStore.lastNtpSync = 0;
        rtcStore.wifiValid   = false;
        rtcStore.wifiChannel = 0;
        for (int i = 0; i < 6; i++) rtcStore.wifiBssid[i] = 0;
    }

    rtcStore.bootCount++;
    rtcStore.crc = calcCrc(rtcStore);

    logInfo("SLEEP", "Boot #" + String(rtcStore.bootCount) +
                     " | Wakeup: " + getWakeupReason());
}

bool DeepSleepManager::isWakeFromSleep() const {
    return _wakeFromSleep;
}

uint32_t DeepSleepManager::getBootCount() const {
    return rtcStore.bootCount;
}

void DeepSleepManager::sleepUntilNextUpdate() {
    uint64_t sleepSec = secondsUntilNextActiveWindow();

    logInfo("SLEEP", "Deep Sleep fuer " + String((uint32_t)sleepSec) +
                     " Sekunden (" + String((uint32_t)(sleepSec / 60)) + " Minuten)");

    // Display ist bereits im Hibernate (hibernate() vor sleep() aufrufen!)
    // SPI deaktivieren um Strom zu sparen
    SPI.end();

    // Wakeup-Timer konfigurieren (Einheit: Mikrosekunden)
    esp_sleep_enable_timer_wakeup(sleepSec * 1000000ULL);

    Serial.println("[INFO ] [SLEEP] Gute Nacht.");
    Serial.flush();
    delay(100);

    esp_deep_sleep_start();
    // Kehrt nicht zurück
}

uint64_t DeepSleepManager::secondsUntilNextActiveWindow() const {
    // Ohne Zeitsync: normales Intervall schlafen
    time_t now = time(nullptr);
    if (now < 1704067200UL) {
        // Ohne gueltige Zeit koennen wir das Zeitfenster nicht bestimmen.
        // Laengeres Blind-Intervall statt UPDATE_INTERVAL_SEC, damit ein
        // anhaltender NTP-Ausfall (z.B. am Wochenende) den Akku nicht durch
        // haeufiges Aufwachen leert.
        logWarn("SLEEP", "Keine Zeitinfo — Blind-Backoff " +
                         String(NTP_FAIL_SLEEP_SEC) + "s");
        return (uint64_t)NTP_FAIL_SLEEP_SEC;
    }

    struct tm ti;
    localtime_r(&now, &ti);

    int wday = ti.tm_wday;   // 0=So, 1=Mo, ..., 6=Sa
    int hour = ti.tm_hour;
    int min  = ti.tm_min;
    int sec  = ti.tm_sec;

    // Im aktiven Fenster: normales Update-Intervall
    int  minutesOfDay = hour * 60 + min;
    bool activeDay  = (wday >= ACTIVE_WEEKDAY_FROM && wday <= ACTIVE_WEEKDAY_TO);
    bool activeTime = (minutesOfDay >= ACTIVE_START_MIN && minutesOfDay < ACTIVE_END_MIN);

    if (activeDay && activeTime) {
        logInfo("SLEEP", "Im aktiven Zeitfenster → schlafe " +
                         String(UPDATE_INTERVAL_SEC) + "s");
        return (uint64_t)UPDATE_INTERVAL_SEC;
    }

    // Außerhalb Zeitfenster: bis zum nächsten Werktagmorgen 08:00 schlafen.
    // Berechnung: Sekunden bis zum nächsten 08:00 Mo–Fr.
    int secondsToday = hour * 3600 + min * 60 + sec;
    int targetSeconds = ACTIVE_START_MIN * 60;  // Fensterbeginn (07:55) in Sekunden

    // Tage bis zum nächsten aktiven Tag berechnen
    int daysToNext = 0;
    for (int d = 0; d < 8; d++) {
        int checkDay = (wday + d) % 7;
        int checkIsActive = (checkDay >= ACTIVE_WEEKDAY_FROM &&
                             checkDay <= ACTIVE_WEEKDAY_TO);

        if (checkIsActive) {
            if (d == 0 && secondsToday < targetSeconds) {
                // Heute noch vor 08:00
                daysToNext = 0;
                break;
            } else if (d > 0) {
                daysToNext = d;
                break;
            }
        }
    }

    uint64_t sleepSec;
    if (daysToNext == 0) {
        // Heute, aber vor 08:00
        sleepSec = (uint64_t)(targetSeconds - secondsToday);
    } else {
        // Nächster Werktag
        sleepSec = (uint64_t)(daysToNext * 86400 - secondsToday + targetSeconds);
    }

    // Mindestens UPDATE_INTERVAL_SEC schlafen (Sicherheit)
    if (sleepSec < (uint64_t)UPDATE_INTERVAL_SEC)
        sleepSec = (uint64_t)UPDATE_INTERVAL_SEC;

    logInfo("SLEEP", "Ausserhalb Zeitfenster → schlafe " +
                     String((uint32_t)(sleepSec / 3600)) + "h " +
                     String((uint32_t)((sleepSec % 3600) / 60)) + "min");
    return sleepSec;
}

String DeepSleepManager::getWakeupReason() const {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER:     return "Timer (Deep Sleep)";
        case ESP_SLEEP_WAKEUP_EXT0:      return "EXT0";
        case ESP_SLEEP_WAKEUP_EXT1:      return "EXT1";
        case ESP_SLEEP_WAKEUP_UNDEFINED: return "Kaltstart / Reset";
        default:                         return "Unbekannt";
    }
}

uint32_t DeepSleepManager::calcCrc(const RtcData& d) const {
    // Leichtgewichtige Prüfsumme über ALLE Felder (ausser crc selbst).
    // Erkennt Korruption im RTC-RAM, damit ein defekter WLAN-/NTP-Cache
    // nicht zu Fehlverbindungen oder falscher Zeit führt.
    uint32_t c = 0xDEADBEEF;
    c ^= d.bootCount * 2654435761u;
    c ^= d.lastNtpSync * 40503u;
    c ^= (uint32_t)d.wifiChannel << 3;
    for (int i = 0; i < 6; i++) c ^= (uint32_t)d.wifiBssid[i] << ((i % 4) * 8);
    c ^= d.wifiValid ? 0xA5A5A5A5u : 0u;
    return c;
}

// ============================================================
//  WLAN-Schnellverbindung — Cache im RTC-RAM
// ============================================================
bool DeepSleepManager::hasWifiCache() const {
    return rtcStore.wifiValid;
}

uint8_t DeepSleepManager::getWifiChannel() const {
    return rtcStore.wifiChannel;
}

const uint8_t* DeepSleepManager::getWifiBssid() const {
    return rtcStore.wifiBssid;
}

void DeepSleepManager::storeWifiCache(uint8_t channel, const uint8_t* bssid) {
    if (!bssid || channel == 0) return;
    rtcStore.wifiChannel = channel;
    for (int i = 0; i < 6; i++) rtcStore.wifiBssid[i] = bssid[i];
    rtcStore.wifiValid = true;
    rtcStore.crc = calcCrc(rtcStore);
}

void DeepSleepManager::clearWifiCache() {
    rtcStore.wifiValid = false;
    rtcStore.crc = calcCrc(rtcStore);
}

// ============================================================
//  NTP-Sync-Zeitstempel — Cache im RTC-RAM
// ============================================================
uint32_t DeepSleepManager::getLastNtpSync() const {
    return rtcStore.lastNtpSync;
}

void DeepSleepManager::setLastNtpSync(uint32_t epoch) {
    rtcStore.lastNtpSync = epoch;
    rtcStore.crc = calcCrc(rtcStore);
}
