#include "ImageManager.h"
#include "Logger.h"
#include <WiFi.h>

// ============================================================
//  ImageManager — Implementierung
//  Format wird zur Compile-Zeit via IMAGE_FORMAT_PNG gewählt.
// ============================================================

ImageManager::ImageManager() {}

bool ImageManager::begin() {
    if (!LittleFS.begin(true)) {
        logError("IMG", "LittleFS Init fehlgeschlagen");
        return false;
    }
    logInfo("IMG", "LittleFS OK — " +
                   String(LittleFS.usedBytes()) + "/" +
                   String(LittleFS.totalBytes()) + " Bytes belegt");
    logInfo("IMG", "Bildformat: " +
                   String(IMAGE_FORMAT_PNG ? "PNG" : "BMP 1-Bit"));

    String etag = readFile(FS_ETAG_PATH);
    if (etag.length() > 0) logInfo("IMG", "ETag: " + etag);
    return true;
}

DownloadResult ImageManager::downloadImage(const char* url) {
    if (!WiFi.isConnected()) {
        logError("IMG", "Kein WLAN");
        return DownloadResult::ERROR_NO_WIFI;
    }

    logInfo("IMG", "HTTP GET: " + String(url));
    logHeap("IMG");

    HTTPClient http;
    http.begin(url);
    http.setTimeout((int)HTTP_TIMEOUT_MS);
    http.setUserAgent("ESP32-DoorSign/1.0 (" + String(DEVICE_NAME) + ")");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    // Conditional GET
    String savedEtag = readFile(FS_ETAG_PATH);
    if (savedEtag.length() > 0) {
        http.addHeader("If-None-Match", savedEtag);
    } else {
        String savedLastMod = readFile(FS_LASTMOD_PATH);
        if (savedLastMod.length() > 0)
            http.addHeader("If-Modified-Since", savedLastMod);
    }

    int httpCode = http.GET();
    logInfo("IMG", "HTTP Response: " + String(httpCode));

    if (httpCode == HTTP_CODE_NOT_MODIFIED) {
        logInfo("IMG", "Bild unveraendert (304)");
        http.end();
        return DownloadResult::SUCCESS_UNCHANGED;
    }

    if (httpCode != HTTP_CODE_OK) {
        logError("IMG", "HTTP-Fehler: " + String(httpCode));
        http.end();
        return DownloadResult::ERROR_HTTP;
    }

    int contentLen = http.getSize();
    logInfo("IMG", "Content-Length: " + String(contentLen) + " Bytes");

    if (contentLen > 0 && (size_t)contentLen > IMG_MAX_BYTES) {
        logError("IMG", "Datei zu gross (" + String(contentLen) + " Bytes)");
        http.end();
        return DownloadResult::ERROR_INVALID_IMAGE;
    }

    String newEtag    = http.header("ETag");
    String newLastMod = http.header("Last-Modified");

    // Download in temporäre Datei
    File tmpFile = LittleFS.open(FS_IMAGE_TMP, "w");
    if (!tmpFile) {
        logError("IMG", "Kann tmp-Datei nicht anlegen: " + String(FS_IMAGE_TMP));
        http.end();
        return DownloadResult::ERROR_STORAGE;
    }

    WiFiClient*   stream  = http.getStreamPtr();
    uint8_t       buf[512];
    size_t        written = 0;
    unsigned long dlStart = millis();

    // Bedingung enthaelt bewusst auch stream->available(): wenn der Server die
    // Verbindung schliesst, koennen im Empfangspuffer noch Bytes liegen — diese
    // muessen geleert werden, sonst wird das Bild abgeschnitten (chunked / EOF).
    while (http.connected() || stream->available() > 0) {
        size_t avail = stream->available();
        if (avail > 0) {
            size_t toRead = min(avail, sizeof(buf));
            int n = stream->readBytes(buf, toRead);
            if (n > 0) {
                // Rueckgabe von write() pruefen: bei vollem LittleFS wird weniger
                // geschrieben als angefordert → Datei waere unvollstaendig.
                size_t w = tmpFile.write(buf, n);
                if (w != (size_t)n) {
                    logError("IMG", "Schreibfehler (" + String(w) + "/" +
                                    String(n) + " Bytes) — LittleFS voll?");
                    tmpFile.close();
                    LittleFS.remove(FS_IMAGE_TMP);
                    http.end();
                    return DownloadResult::ERROR_STORAGE;
                }
                written += n;
            }
        } else {
            if (contentLen > 0 && written >= (size_t)contentLen) break;
            delay(1);
        }
        if (millis() - dlStart > DOWNLOAD_TIMEOUT_MS) {
            logError("IMG", "Download-Timeout");
            tmpFile.close();
            LittleFS.remove(FS_IMAGE_TMP);
            http.end();
            return DownloadResult::ERROR_HTTP;
        }
        if (written > IMG_MAX_BYTES) {
            logError("IMG", "Download zu gross");
            tmpFile.close();
            LittleFS.remove(FS_IMAGE_TMP);
            http.end();
            return DownloadResult::ERROR_INVALID_IMAGE;
        }
    }

    tmpFile.close();
    http.end();
    logInfo("IMG", "Download: " + String(written) + " Bytes");

    // Vollstaendigkeit pruefen: bei bekannter Content-Length muss die
    // geschriebene Bytezahl exakt passen — sonst wurde der Download
    // abgeschnitten (Verbindungsabbruch mitten im Stream).
    if (contentLen > 0 && written != (size_t)contentLen) {
        logError("IMG", "Unvollstaendig: " + String(written) + "/" +
                        String(contentLen) + " Bytes");
        LittleFS.remove(FS_IMAGE_TMP);
        return DownloadResult::ERROR_HTTP;
    }

    // Validierung
    if (!validateImage(FS_IMAGE_TMP)) {
        LittleFS.remove(FS_IMAGE_TMP);
        return DownloadResult::ERROR_INVALID_IMAGE;
    }

    // Atomarer Austausch: rename ZUERST versuchen. LittleFS ueberschreibt ein
    // bestehendes Ziel — das alte gute Bild bleibt so bis zum Erfolg intakt.
    // Nur falls rename ueber ein existierendes Ziel scheitert, gezielt
    // remove+retry als Fallback (Fenster ohne Bild bleibt dann minimal).
    if (!LittleFS.rename(FS_IMAGE_TMP, FS_IMAGE_PATH)) {
        LittleFS.remove(FS_IMAGE_PATH);
        if (!LittleFS.rename(FS_IMAGE_TMP, FS_IMAGE_PATH)) {
            logError("IMG", "Rename fehlgeschlagen");
            LittleFS.remove(FS_IMAGE_TMP);
            return DownloadResult::ERROR_STORAGE;
        }
    }

    // Metadaten speichern
    if (newEtag.length() > 0)    writeFile(FS_ETAG_PATH,    newEtag);
    else                          LittleFS.remove(FS_ETAG_PATH);
    if (newLastMod.length() > 0) writeFile(FS_LASTMOD_PATH, newLastMod);
    else                          LittleFS.remove(FS_LASTMOD_PATH);

    logInfo("IMG", "Bild gespeichert: " + String(FS_IMAGE_PATH));
    return DownloadResult::SUCCESS_CHANGED;
}

bool ImageManager::hasStoredImage() const {
    return LittleFS.exists(FS_IMAGE_PATH);
}

String ImageManager::resultToString(DownloadResult r) {
    switch (r) {
        case DownloadResult::SUCCESS_CHANGED:    return "Neues Bild";
        case DownloadResult::SUCCESS_UNCHANGED:  return "Unveraendert (304)";
        case DownloadResult::ERROR_NO_WIFI:      return "Fehler: kein WLAN";
        case DownloadResult::ERROR_HTTP:         return "Fehler: HTTP";
        case DownloadResult::ERROR_INVALID_IMAGE:return "Fehler: ungueltige Datei";
        case DownloadResult::ERROR_STORAGE:      return "Fehler: Speicher";
        default:                                 return "Unbekannt";
    }
}

// ============================================================
//  Validierung — wählt automatisch PNG oder BMP
// ============================================================
bool ImageManager::validateImage(const char* path) const {
#if IMAGE_FORMAT_PNG
    return validatePng(path);
#else
    return validateBmp(path);
#endif
}

bool ImageManager::validatePng(const char* path) const {
    File f = LittleFS.open(path, "r");
    if (!f) { logError("IMG", "validatePng: nicht oeffenbar"); return false; }

    size_t size = f.size();
    // Minimum: 8 Byte Signatur + IHDR (Laenge/Typ/Daten bis Offset 24).
    if (size < 24) {
        logError("IMG", "validatePng: zu klein (" + String(size) + " Bytes)");
        f.close(); return false;
    }

    // Kopf lesen: Signatur (0..7) + IHDR mit Breite/Hoehe (16..23, big-endian).
    uint8_t hdr[24];
    if (f.read(hdr, 24) != 24) {
        logError("IMG", "validatePng: Lesefehler Kopf");
        f.close(); return false;
    }

    const uint8_t pngSig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    for (int i = 0; i < 8; i++) {
        if (hdr[i] != pngSig[i]) {
            logError("IMG", "validatePng: keine PNG-Signatur");
            f.close(); return false;
        }
    }

    // Breite/Hoehe aus IHDR (PNG speichert big-endian) gegen erwartete Maße pruefen.
    uint32_t w = ((uint32_t)hdr[16] << 24) | ((uint32_t)hdr[17] << 16) |
                 ((uint32_t)hdr[18] <<  8) |  (uint32_t)hdr[19];
    uint32_t h = ((uint32_t)hdr[20] << 24) | ((uint32_t)hdr[21] << 16) |
                 ((uint32_t)hdr[22] <<  8) |  (uint32_t)hdr[23];
    if (w != (uint32_t)IMG_WIDTH || h != (uint32_t)IMG_HEIGHT) {
        logError("IMG", "validatePng: Dimensionen " + String(w) + "x" + String(h) +
                        " (erwartet " + String(IMG_WIDTH) + "x" + String(IMG_HEIGHT) + ")");
        f.close(); return false;
    }

    // IEND-Chunk am Dateiende pruefen — erkennt abgeschnittene Downloads
    // (chunked / Verbindungsabbruch), die die Signatur allein nicht aufdeckt.
    // Es wird im Endbereich GESUCHT (nicht exakt das letzte Byte verlangt),
    // damit wenige harmlose Zusatzbytes hinter IEND (z.B. ein angehaengter
    // Zeilenumbruch durch eine Zustellkette) ein gueltiges Bild nicht ablehnen.
    // Ein abgeschnittener Download enthaelt den Marker gar nicht.
    const uint8_t iend[8] = {0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82};
    size_t  tailLen = (size < 64) ? size : 64;   // size >= 24 garantiert
    uint8_t tail[64];
    f.seek(size - tailLen);
    if ((size_t)f.read(tail, tailLen) != tailLen) {
        logError("IMG", "validatePng: Lesefehler Ende");
        f.close(); return false;
    }
    f.close();
    bool iendFound = false;
    for (size_t i = 0; i + 8 <= tailLen; i++) {
        bool match = true;
        for (int j = 0; j < 8; j++) {
            if (tail[i + j] != iend[j]) { match = false; break; }
        }
        if (match) { iendFound = true; break; }
    }
    if (!iendFound) {
        logError("IMG", "validatePng: IEND fehlt — Bild unvollstaendig");
        return false;
    }

    logInfo("IMG", "validatePng: OK — " + String(w) + "x" + String(h) +
                   ", " + String(size) + " Bytes");
    return true;
}

bool ImageManager::validateBmp(const char* path) const {
    File f = LittleFS.open(path, "r");
    if (!f) { logError("IMG", "validateBmp: nicht oeffenbar"); return false; }

    size_t size = f.size();
    if (size < 62) {
        logError("IMG", "validateBmp: zu klein (" + String(size) + " Bytes)");
        f.close(); return false;
    }

    uint8_t sig[2];
    f.read(sig, 2);
    if (sig[0] != 'B' || sig[1] != 'M') {
        logError("IMG", "validateBmp: keine BMP-Signatur");
        f.close(); return false;
    }

    // Dimensionen + Bit-Tiefe + Compression aus DIB-Header
    f.seek(18);
    int32_t  w = 0, h = 0;
    uint16_t bits = 0;
    uint32_t compression = 0;
    f.read((uint8_t*)&w, 4);
    f.read((uint8_t*)&h, 4);
    f.seek(28);
    f.read((uint8_t*)&bits, 2);
    f.seek(30);
    f.read((uint8_t*)&compression, 4);
    if (h < 0) h = -h;
    f.close();

    if (w != (int32_t)IMG_WIDTH || h != (int32_t)IMG_HEIGHT) {
        logError("IMG", "validateBmp: Dimensionen " + String(w) + "x" + String(h) +
                        " (erwartet " + String(IMG_WIDTH) + "x" + String(IMG_HEIGHT) + ")");
        return false;
    }

    // Akzeptiert: 1-Bit (S/W) und 4-Bit (Palette mit bis zu 16 Farben).
    // 4-Bit wird verwendet fuer V3-Displays (S/W/Rot via Palette).
    // Compression muss 0 sein (BI_RGB, unkomprimiert).
    if (bits != 1 && bits != 4) {
        logError("IMG", "validateBmp: Bit-Tiefe " + String(bits) +
                        " (erwartet 1 oder 4)");
        return false;
    }
    if (compression != 0) {
        logError("IMG", "validateBmp: Kompression " + String(compression) +
                        " (nur unkomprimiert unterstuetzt)");
        return false;
    }

    logInfo("IMG", "validateBmp: OK — " + String(w) + "x" + String(h) +
                   ", " + String(bits) + "-Bit, " +
                   String(size) + " Bytes");
    return true;
}

// ============================================================
//  LittleFS Hilfsfunktionen
// ============================================================
String ImageManager::readFile(const char* path) const {
    if (!LittleFS.exists(path)) return "";
    File f = LittleFS.open(path, "r");
    if (!f) return "";
    String s = f.readString();
    f.close();
    s.trim();
    return s;
}

void ImageManager::writeFile(const char* path, const String& content) {
    File f = LittleFS.open(path, "w");
    if (!f) { logError("IMG", "writeFile fehlgeschlagen: " + String(path)); return; }
    f.print(content);
    f.close();
}
