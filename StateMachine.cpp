#include "StateMachine.h"
#include "WifiManager.h"
#include "TimeManager.h"
#include "ImageManager.h"
#include "DisplayManager.h"
#include "config.h"
#include "Logger.h"

// Vorwärts-Deklaration der file-lokalen Hilfsfunktion
static const char* stateToString(State s);

// ============================================================
//  StateMachine — Implementierung
//
//  Zustandsdiagramm:
//
//  BOOT
//   │ immer
//   ▼
//  WIFI_CONNECTING
//   │ verbunden          │ Timeout
//   ▼                    ▼
//  NTP_SYNCING      IDLE (ohne Zeitsync)
//   │ synchronisiert     │ Fehler/Timeout
//   ▼                    ▼
//  IDLE ◄───────── ERROR_RECOVERY
//   │ Update fällig & aktives Zeitfenster
//   ▼
//  DOWNLOADING
//   │ SUCCESS_CHANGED     │ SUCCESS_UNCHANGED
//   ▼                     │ ERROR_*
//  UPDATING_DISPLAY        ▼
//   │                    IDLE (kein Display-Update nötig)
//   ▼
//  IDLE
// ============================================================

StateMachine::StateMachine(WifiManager*    wifi,
                           TimeManager*    time,
                           ImageManager*   image,
                           DisplayManager* display)
    : _wifi(wifi), _time(time), _image(image), _display(display),
      _state(State::BOOT),
      _displayShowingContent(false),
      _pendingDisplayUpdate(false),
      _pendingStoredImageDisplay(false),
      _lastUpdateMs(0),
      _ntpLastAttemptMs(0),
      _stateEnteredMs(0),
      _errorRecoveryDelayMs(0) {}

void StateMachine::begin() {
    logInfo("SM", "=== DoorSign " + String(DEVICE_NAME) +
                  " — " + String(ROOM_NAME) + " ===");

    // Startbildschirm anzeigen — gibt sofortige visuelle Rueckmeldung.
    // Das E-Ink-Panel benoetigt nach einem Full-Refresh mehrere Sekunden
    // Erholung. Deshalb wird hier NUR der Startbildschirm angezeigt und
    // KEIN weiteres Bild geladen. Der normale Update-Zyklus (IDLE →
    // DOWNLOADING → UPDATING_DISPLAY) uebernimmt nach WLAN + NTP.
    logInfo("SM", "Zeige Startbildschirm...");
    if (_image->hasStoredImage()) {
        _display->showStartup("Letztes Bild vorhanden — verbinde WLAN...");
    } else {
        _display->showStartup("Verbinde mit WLAN: " + String(WIFI_SSID));
    }
    _displayShowingContent = false;  // Warten auf normalen Update-Zyklus

    // Wenn ein gespeichertes Bild vorhanden ist, soll es im ersten
    // IDLE-Durchlauf angezeigt werden — unabhaengig vom Zeitfenster.
    // Dafuer _pendingStoredImageDisplay setzen.
    if (_image->hasStoredImage()) {
        logInfo("SM", "Gespeichertes Bild vorhanden — wird nach NTP-Sync angezeigt");
        _pendingStoredImageDisplay = true;
    }

    setState(State::BOOT);
}

void StateMachine::update() {
    switch (_state) {
        case State::BOOT:              handleBoot();             break;
        case State::WIFI_CONNECTING:   handleWifiConnecting();   break;
        case State::NTP_SYNCING:       handleNtpSyncing();       break;
        case State::IDLE:              handleIdle();             break;
        case State::DOWNLOADING:       handleDownloading();      break;
        case State::UPDATING_DISPLAY:  handleUpdatingDisplay();  break;
        case State::ERROR_RECOVERY:    handleErrorRecovery();    break;
    }
}

// ============================================================
//  Zustandshandler
// ============================================================

void StateMachine::handleBoot() {
    // BOOT → WIFI_CONNECTING (sofort, kein Warten)
    logInfo("SM", "Boot abgeschlossen — starte WLAN-Verbindung");
    setState(State::WIFI_CONNECTING);
}

void StateMachine::handleWifiConnecting() {
    // Verbindungsversuch (connect() blockiert bis Timeout).
    // Im normalen Betrieb dauert das 2–10 Sekunden.
    if (_wifi->connect(WIFI_CONNECT_TIMEOUT_MS)) {
        logInfo("SM", "WLAN verbunden → NTP-Sync");
        setState(State::NTP_SYNCING);
    } else {
        logWarn("SM", "WLAN-Verbindung fehlgeschlagen → IDLE (ohne Zeitsync)");
        // Im IDLE-Zustand wird reconnectIfNeeded() periodisch aufgerufen.
        setState(State::IDLE);
    }
}

void StateMachine::handleNtpSyncing() {
    _ntpLastAttemptMs = millis();

    if (_time->synchronize()) {
        logInfo("SM", "NTP synchronisiert: " + _time->getTimestamp());
        setState(State::IDLE);
    } else {
        logWarn("SM", "NTP-Sync fehlgeschlagen → IDLE (Retry in " +
                      String(NTP_RETRY_INTERVAL_MS / 1000) + "s)");
        setState(State::IDLE);
    }
}

void StateMachine::handleIdle() {
    // 1. WLAN-Verbindung prüfen und ggf. wiederherstellen
    if (!_wifi->isConnected()) {
        bool reconnected = _wifi->reconnectIfNeeded();
        if (!reconnected) return;  // Warte auf nächsten update()-Aufruf
    }

    // 2. NTP-Retry falls Zeit nicht synchronisiert
    if (!_time->isSynced()) {
        unsigned long nowMs = millis();
        if (nowMs - _ntpLastAttemptMs >= NTP_RETRY_INTERVAL_MS) {
            logInfo("SM", "NTP-Retry...");
            setState(State::NTP_SYNCING);
            return;
        }
        // Ohne Zeitsync kein Update möglich — abwarten
        return;
    }

    // 3. Gespeichertes Bild einmalig nach NTP-Sync anzeigen
    //    (Display hat sich nach dem Startbildschirm erholt: WLAN+NTP = mind. 5–30s)
    if (_pendingStoredImageDisplay && _time->isSynced()) {
        logInfo("SM", "Zeige gespeichertes Bild (nach Settle-Zeit)...");
        _pendingStoredImageDisplay = false;
        if (_display->showImageFromFile(FS_IMAGE_PATH)) {
            _displayShowingContent = true;
            _display->hibernate();
            logInfo("SM", "Gespeichertes Bild wiederhergestellt");
        } else {
            _display->showFallback();
        }
        return;
    }

    // 4. Update starten wenn fällig und aktives Zeitfenster
    if (isUpdateDue()) {
        if (_time->isInActiveWindow()) {
            logInfo("SM", "Update fällig und im aktiven Zeitfenster → Download");
            setState(State::DOWNLOADING);
        } else {
            // Außerhalb des Zeitfensters: Zeitpunkt merken, damit sofort
            // nach Fensterbeginn ein Update erfolgt.
            logInfo("SM", "Update fällig, aber außerhalb Zeitfenster (" +
                          String(ACTIVE_START_MIN / 60) + ":" +
                          (ACTIVE_START_MIN % 60 < 10 ? "0" : "") + String(ACTIVE_START_MIN % 60) +
                          "–" +
                          String(ACTIVE_END_MIN / 60) + ":" +
                          (ACTIVE_END_MIN % 60 < 10 ? "0" : "") + String(ACTIVE_END_MIN % 60) +
                          " Mo–Fr) — warte");
            // _lastUpdateMs NICHT aktualisieren: sofort updaten wenn Fenster öffnet
        }
    }
}

void StateMachine::handleDownloading() {
    // Download blockiert den Loop für die Dauer des HTTP-Requests.
    // Typisch: 1–15 Sekunden. Akzeptabel für diesen Use Case.
    logInfo("SM", "Starte Download: " + String(IMAGE_URL));
    _lastUpdateMs = millis();  // Zeitstempel setzen (auch bei Fehler)

    DownloadResult result = _image->downloadImage(IMAGE_URL);
    logInfo("SM", "Download-Ergebnis: " + ImageManager::resultToString(result));

    switch (result) {
        case DownloadResult::SUCCESS_CHANGED:
            // Neues Bild → Display aktualisieren
            _pendingDisplayUpdate = true;
            setState(State::UPDATING_DISPLAY);
            break;

        case DownloadResult::SUCCESS_UNCHANGED:
            // Kein neues Bild (304) → Display nicht anfassen
            logInfo("SM", "Kein Display-Update nötig (Bild unverändert)");
            setState(State::IDLE);
            break;

        case DownloadResult::ERROR_NO_WIFI:
        case DownloadResult::ERROR_HTTP:
            logWarn("SM", "Download-Fehler — letztes Bild bleibt erhalten");
            // Nicht sofort wieder versuchen: _lastUpdateMs wurde gesetzt
            _errorRecoveryDelayMs = 30000UL;  // 30s Pause bei HTTP-Fehler
            setState(State::ERROR_RECOVERY);
            break;

        case DownloadResult::ERROR_INVALID_IMAGE:
            logError("SM", "Ungültige BMP — Server-Konfiguration prüfen");
            _errorRecoveryDelayMs = 60000UL;  // 60s Pause bei ungültigem Bild
            setState(State::ERROR_RECOVERY);
            break;

        case DownloadResult::ERROR_STORAGE:
            logError("SM", "Speicherfehler — LittleFS-Problem");
            _errorRecoveryDelayMs = 60000UL;
            setState(State::ERROR_RECOVERY);
            break;
    }
}

void StateMachine::handleUpdatingDisplay() {
    if (!_pendingDisplayUpdate) {
        setState(State::IDLE);
        return;
    }

    logInfo("SM", "Aktualisiere Display...");

    // Display-Update blockiert für ~5–15 Sekunden (E-Ink-Refresh)
    // Das ist hardware-inhärent und unvermeidbar.
    if (_display->showImageFromFile(FS_IMAGE_PATH)) {
        _displayShowingContent = true;
        _pendingDisplayUpdate  = false;
        logInfo("SM", "Display erfolgreich aktualisiert");

        // Display in Ruhemodus versetzen um Strom zu sparen und
        // die Lebensdauer des E-Ink-Panels zu schonen.
        _display->hibernate();
    } else {
        logError("SM", "Display-Update fehlgeschlagen");
        _pendingDisplayUpdate = false;
        _errorRecoveryDelayMs = 10000UL;
        setState(State::ERROR_RECOVERY);
        return;
    }

    setState(State::IDLE);
}

void StateMachine::handleErrorRecovery() {
    unsigned long elapsed = millis() - _stateEnteredMs;
    if (elapsed < _errorRecoveryDelayMs) {
        return;  // Warte ab — sofort zurückkehren
    }
    logInfo("SM", "Error Recovery abgeschlossen → IDLE");
    setState(State::IDLE);
}

// ============================================================
//  Hilfsfunktionen
// ============================================================

bool StateMachine::isUpdateDue() const {
    if (_lastUpdateMs == 0) return true;  // Noch kein Update seit Start
    return (millis() - _lastUpdateMs) >= UPDATE_INTERVAL_MS;
}

void StateMachine::setState(State next) {
    if (_state != next) {
        logInfo("SM", String("Zustand: ") + getStateString() +
                      " → " + stateToString(next));
    }
    _state = next;
    _stateEnteredMs = millis();
}

const char* StateMachine::getStateString() const {
    return stateToString(_state);
}

// File-lokale Hilfsfunktion — gibt Zustandsnamen als C-String zurück.
static const char* stateToString(State s) {
    switch (s) {
        case State::BOOT:             return "BOOT";
        case State::WIFI_CONNECTING:  return "WIFI_CONNECTING";
        case State::NTP_SYNCING:      return "NTP_SYNCING";
        case State::IDLE:             return "IDLE";
        case State::DOWNLOADING:      return "DOWNLOADING";
        case State::UPDATING_DISPLAY: return "UPDATING_DISPLAY";
        case State::ERROR_RECOVERY:   return "ERROR_RECOVERY";
        default:                      return "UNKNOWN";
    }
}
