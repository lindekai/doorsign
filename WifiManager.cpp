#include "WifiManager.h"
#include "config.h"
#include "Logger.h"

// ============================================================
//  WifiManager — Implementierung
// ============================================================

WifiManager::WifiManager(const char* ssid, const char* password)
    : _ssid(ssid), _password(password), _lastReconnectAttemptMs(0) {}

bool WifiManager::connect(unsigned long timeoutMs) {
    if (WiFi.isConnected()) {
        logInfo("WIFI", "Bereits verbunden, IP: " + getIPAddress());
        return true;
    }

    logInfo("WIFI", "Verbinde mit SSID: " + String(_ssid));

    WiFi.mode(WIFI_STA);
    // Auto-Reconnect aktivieren: falls die Verbindung unerwartet abbricht,
    // versucht der ESP32 selbstständig, sie wiederherzustellen.
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);  // Keine WLAN-Konfiguration in Flash speichern
    WiFi.begin(_ssid, _password);

    unsigned long startMs = millis();
    uint8_t dots = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) {
        delay(500);
        Serial.print(".");
        if (++dots % 40 == 0) Serial.println();
    }
    Serial.println();

    if (WiFi.isConnected()) {
        logInfo("WIFI", "Verbunden!");
        logInfo("WIFI", "  IP-Adresse : " + getIPAddress());
        logInfo("WIFI", "  Signalstärke: " + String(getRSSI()) + " dBm");
        return true;
    }

    // Fehlerursache aus Status-Code ableiten
    wl_status_t status = WiFi.status();
    String reason;
    switch (status) {
        case WL_NO_SSID_AVAIL:    reason = "SSID nicht gefunden";         break;
        case WL_CONNECT_FAILED:   reason = "Verbindung abgelehnt (Passwort?)"; break;
        case WL_CONNECTION_LOST:  reason = "Verbindung verloren";          break;
        default:                  reason = "Status " + String(status);     break;
    }
    logError("WIFI", "Verbindung fehlgeschlagen: " + reason);
    return false;
}

bool WifiManager::connectFast(unsigned long timeoutMs, uint8_t channel, const uint8_t* bssid) {
    if (WiFi.isConnected()) return true;
    if (!bssid || channel == 0) return false;

    logInfo("WIFI", "Schnellverbindung (Kanal " + String(channel) + ", BSSID bekannt)...");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    // BSSID + Kanal vorgeben → kein AP-Scan nötig, deutlich schneller.
    WiFi.begin(_ssid, _password, (int32_t)channel, bssid);

    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) {
        delay(100);
    }

    if (WiFi.isConnected()) {
        logInfo("WIFI", "Schnellverbindung OK — IP: " + getIPAddress() +
                        ", " + String(getRSSI()) + " dBm");
        return true;
    }

    logWarn("WIFI", "Schnellverbindung fehlgeschlagen — Fallback auf Scan-Verbindung");
    return false;
}

bool WifiManager::isConnected() const {
    return WiFi.isConnected();
}

bool WifiManager::reconnectIfNeeded() {
    if (WiFi.isConnected()) return true;

    unsigned long nowMs = millis();
    if (nowMs - _lastReconnectAttemptMs < WIFI_RECONNECT_INTERVAL_MS) {
        // Noch nicht Zeit für einen neuen Versuch — sofort zurückkehren
        return false;
    }

    logWarn("WIFI", "Verbindung verloren. Reconnect-Versuch...");
    _lastReconnectAttemptMs = nowMs;

    // Kurzen Reconnect versuchen (10 Sekunden Timeout)
    return connect(10000UL);
}

void WifiManager::disconnect() {
    WiFi.disconnect(true);
    logInfo("WIFI", "Verbindung getrennt");
}

String WifiManager::getIPAddress() const {
    return WiFi.localIP().toString();
}

int WifiManager::getRSSI() const {
    return WiFi.RSSI();
}
