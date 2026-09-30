#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <time.h>

#if __has_include("journal_config.h")
#include "journal_config.h"
#else
#include "config.example.h"
#endif
#include "tls_roots.h"
#include "calibration_ticket.h"
#include "ble_transport.h"

#ifndef BT_PRINT_DENSITY
#define BT_PRINT_DENSITY 0
#endif

static_assert(BT_PRINT_DENSITY >= 0 && BT_PRINT_DENSITY <= 4, "Density must stay within the conservative configured range");
static_assert(BT_CHUNK_BYTES > 0 && BT_CHUNK_BYTES <= 512, "Invalid chunk size");
static_assert(HTTP_POLL_MS >= 60000UL, "HTTP interval must be at least one minute");

JournalBle printer;
Preferences prefs;
bool storageReady = false;
bool btReady = false;
int activeSlot = 0;
String cachedDate, cachedHash, cachedFile;
size_t cachedSize = 0;
uint32_t lastPoll = 0, lastBt = 0, lastWiFi = 0;
String serialLine;
uint8_t printDensity = BT_PRINT_DENSITY;
const size_t MAX_JOB_BYTES = 200000;
String today() {
  time_t now = time(nullptr);
  if (now < 1704067200) return "";
  struct tm local;
  localtime_r(&now, &local);
  char date[11];
  strftime(date, sizeof(date), "%Y-%m-%d", &local);
  return String(date);
}

bool validDate(const String &date) {
  if (date.length() != 10 || date[4] != '-' || date[7] != '-') return false;
  for (int i = 0; i < 10; i++) if (i != 4 && i != 7 && !isDigit(date[i])) return false;
  return true;
}

bool validHash(const String &hash) {
  if (hash.length() != 64) return false;
  for (int i = 0; i < 64; i++) if (!((hash[i] >= '0' && hash[i] <= '9') || (hash[i] >= 'a' && hash[i] <= 'f'))) return false;
  return true;
}

String slotPath(int slot, const char *extension) {
  return String("/job") + slot + extension;
}

String finishHash(mbedtls_sha256_context &ctx) {
  unsigned char digest[32];
  mbedtls_sha256_finish_ret(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  hex[64] = 0;
  return String(hex);
}

bool verifyFile(const String &path, size_t expectedSize, const String &expectedHash) {
  File file = LittleFS.open(path, "r");
  if (!file || file.size() != expectedSize) return false;
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  uint8_t buffer[1024];
  while (file.available()) {
    size_t n = file.read(buffer, sizeof(buffer));
    if (!n) { file.close(); mbedtls_sha256_free(&ctx); return false; }
    mbedtls_sha256_update_ret(&ctx, buffer, n);
    yield();
  }
  file.close();
  return finishHash(ctx) == expectedHash;
}

bool loadCache() {
  activeSlot = prefs.getUChar("slot", 0);
  if (activeSlot > 1) return false;
  File meta = LittleFS.open(slotPath(activeSlot, ".json"), "r");
  if (!meta) return false;
  StaticJsonDocument<1024> doc;
  auto error = deserializeJson(doc, meta);
  meta.close();
  if (error) return false;
  String date = doc["date"] | "";
  String hash = doc["sha256"] | "";
  size_t size = doc["size"] | 0;
  if (!validDate(date) || !validHash(hash) || !size || size > MAX_JOB_BYTES ||
      !verifyFile(slotPath(activeSlot, ".bin"), size, hash)) return false;
  cachedDate = date; cachedHash = hash; cachedSize = size;
  cachedFile = slotPath(activeSlot, ".bin");
  Serial.printf("[FLASH] Journal vérifié : %s, %u octets\n", date.c_str(), (unsigned)size);
  return true;
}

bool beginHttp(HTTPClient &http, WiFiClientSecure &tls, const String &url) {
  if (!url.startsWith("https://raw.githubusercontent.com/")) {
    Serial.println("[HTTPS] L'URL doit utiliser raw.githubusercontent.com");
    return false;
  }
  tls.setCACert(TLS_ROOTS); // Certificate AND hostname validation. Never setInsecure().
  tls.setTimeout(15); // WiFiClientSecure takes seconds, HTTPClient takes milliseconds.
  tls.setHandshakeTimeout(45);
  http.setTimeout(15000);
  http.setConnectTimeout(15000);
  http.useHTTP10(true); // Avoid chunked-transfer framing in the streaming download.
  return http.begin(tls, url);
}

bool downloadJob(const String &filename, size_t size, const String &hash, int slot) {
  WiFiClientSecure tls;
  HTTPClient http;
  if (!beginHttp(http, tls, String(JOURNAL_BASE_URL) + filename)) return false;
  int status = http.GET();
  if (status != HTTP_CODE_OK || (http.getSize() >= 0 && size_t(http.getSize()) != size)) {
    Serial.printf("[HTTPS] Téléchargement refusé (%d)\n", status);
    http.end(); return false;
  }
  File file = LittleFS.open(slotPath(slot, ".bin"), "w");
  if (!file) { http.end(); return false; }
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  uint8_t buffer[1024];
  size_t received = 0;
  uint32_t lastData = millis(), started = millis();
  WiFiClient *stream = http.getStreamPtr();
  bool ok = true;
  while (received < size) {
    int available = stream->available();
    if (available > 0) {
      size_t count = min(size - received, min(sizeof(buffer), size_t(available)));
      int n = stream->read(buffer, count);
      if (n <= 0 || file.write(buffer, n) != size_t(n)) { ok = false; break; }
      mbedtls_sha256_update_ret(&ctx, buffer, n);
      received += n;
      lastData = millis();
    } else if (!stream->connected() || millis() - lastData > 15000UL) {
      ok = false; break;
    }
    if (millis() - started > 120000UL) { ok = false; break; }
    delay(1);
  }
  file.flush(); file.close(); http.end();
  String actualHash = finishHash(ctx);
  if (!ok || received != size || actualHash != hash || !verifyFile(slotPath(slot, ".bin"), size, hash)) {
    LittleFS.remove(slotPath(slot, ".bin"));
    Serial.println("[FLASH] Téléchargement incomplet ou SHA-256 incorrect ; ancien cache conservé");
    return false;
  }
  return true;
}

bool pollJournal() {
  WiFiClientSecure tls;
  HTTPClient http;
  // Cache-busting for the small manifest; binary names themselves are immutable.
  String url = String(JOURNAL_BASE_URL) + "manifest.json?t=" + String((unsigned long)time(nullptr));
  if (!beginHttp(http, tls, url)) return false;
  int status = http.GET();
  int length = http.getSize();
  if (status != HTTP_CODE_OK || length <= 0 || length > 2048) {
    Serial.printf("[HTTPS] Manifeste indisponible (%d)\n", status);
    if (status < 0) {
      char error[160] = {};
      int code = tls.lastError(error, sizeof(error));
      Serial.printf("[HTTPS] TLS=%d %s, RAM libre=%u\n", code, error, ESP.getFreeHeap());
    }
    http.end(); return false;
  }
  String body = http.getString();
  http.end();
  if (body.length() != size_t(length)) return false;
  StaticJsonDocument<1536> doc;
  if (deserializeJson(doc, body)) return false;
  String date = doc["date"] | "";
  String hash = doc["sha256"] | "";
  String filename = doc["file"] | "";
  size_t size = doc["size"] | 0;
  // Derive the allowed filename, rejecting traversal/foreign hosts and malformed metadata.
  String expectedName = "journal-" + date + "-" + hash.substring(0, 16) + ".bin";
  if (doc["schema"] != 1 || doc["demo"].as<bool>() || doc["width"] != 626 ||
      doc["height"] <= 0 || doc["height"] > 2362 || !validDate(date) ||
      !validHash(hash) || filename != expectedName || size < 100 || size > MAX_JOB_BYTES) {
    Serial.println("[HTTPS] Manifeste invalide ou édition de démonstration"); return false;
  }
  if (date != today()) {
    Serial.printf("[JOURNAL] Édition %s ignorée (pas aujourd'hui)\n", date.c_str()); return true;
  }
  if (prefs.getString("printed", "") >= date) return true;
  if (date == cachedDate && hash == cachedHash) return true;
  int newSlot = 1 - activeSlot;
  if (!downloadJob(filename, size, hash, newSlot)) return false;
  File meta = LittleFS.open(slotPath(newSlot, ".json"), "w");
  if (!meta) return false;
  size_t written = serializeJson(doc, meta);
  meta.flush(); meta.close();
  if (written != measureJson(doc)) return false;
  // Atomically switch only after both the binary and its metadata are durable.
  if (prefs.putUChar("slot", newSlot) != 1) return false;
  return loadCache();
}

bool connectPrinter() {
  if (!btReady) return false;
  Serial.println("[BLE] Recherche/connexion M02 Pro...");
  return printer.connect(PRINTER_MAC, PRINTER_NAME);
}

// Keep the radio in the same state as the visually accepted calibration.
// Restore Wi-Fi on every return, including connection and storage failures.
struct SuspendWifiForPrint {
  wifi_mode_t previousMode;
  SuspendWifiForPrint() : previousMode(WiFi.getMode()) {
    bool stopped = WiFi.mode(WIFI_OFF);
    delay(250);
    Serial.printf("[BT] Wi-Fi suspendu=%s RAM=%u\n", stopped ? "oui" : "echec", unsigned(ESP.getFreeHeap()));
  }
  ~SuspendWifiForPrint() {
    WiFi.mode(previousMode);
    if (previousMode & WIFI_STA) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
};

void printIfReady() {
  String day = today();
  if (!storageReady || day.isEmpty() || cachedDate != day || prefs.getString("printed", "") >= day ||
      !prefs.getString("pending", "").isEmpty()) return;
  if (!verifyFile(cachedFile, cachedSize, cachedHash)) {
    Serial.println("[FLASH] Cache corrompu, téléchargement requis"); cachedDate = ""; return;
  }
  File job = LittleFS.open(cachedFile, "r");
  if (!job || !JournalBle::validate(job)) {
    Serial.println("[FLASH] Encodage raster invalide ; aucune impression"); job.close(); return;
  }
  SuspendWifiForPrint wifiPause;
  if (!connectPrinter()) { Serial.println("[BLE] Imprimante éteinte ou connexion refusée"); job.close(); return; }
  if (prefs.putString("pending", day) != day.length()) {
    Serial.println("[NVS] Impossible de mémoriser l'envoi ; impression annulée");
    job.close(); printer.disconnect(); return;
  }
  bool ok = printer.send(job, printDensity);
  job.close();
  if (ok && prefs.putString("printed", day) == day.length()) {
    prefs.remove("pending");
    Serial.printf("[IMPRESSION] Journal %s transmis, fins de bandes reçues, anti-doublon enregistré\n", day.c_str());
  } else Serial.println("[IMPRESSION] Envoi incertain. Pas de nouvel essai automatique. Vérifier le papier puis RETRY.");
  printer.disconnect();
}

void printCalibration() {
  if (!storageReady) return;
  const char *path = "/test.bin";
  File temp = LittleFS.open(path, "w");
  if (!temp) return;
  uint8_t buffer[512];
  bool ok = true;
  for (size_t offset = 0; offset < sizeof(calibrationTicket);) {
    size_t n = min(sizeof(buffer), sizeof(calibrationTicket) - offset);
    memcpy_P(buffer, calibrationTicket + offset, n);
    if (temp.write(buffer, n) != n) { ok = false; break; }
    offset += n;
  }
  temp.close();
  File job = LittleFS.open(path, "r");
  ok = ok && job && job.size() == sizeof(calibrationTicket) && JournalBle::validate(job);
  if (ok) {
    SuspendWifiForPrint wifiPause;
    ok = connectPrinter() && printer.send(job, printDensity);
    printer.disconnect();
  }
  job.close(); LittleFS.remove(path);
  Serial.printf("[TEST] BLE resultat=%s ; cache et anti-doublon conserves\n", ok ? "OK" : "incertain");
}

void command(const String &line) {
  if (line == "STATUS") {
    Serial.printf("[STATUS] Aujourd'hui=%s cache=%s Wi-Fi=%s imprimé=%s incertain=%s densite=%u transport=BLE largeur=576\n",
      today().c_str(), cachedDate.c_str(), WiFi.status() == WL_CONNECTED ? "OK" : "hors ligne",
      prefs.getString("printed", "").c_str(), prefs.getString("pending", "").c_str(), printDensity);
  } else if (line.length() == 9 && line.startsWith("DENSITY ") && line[8] >= '0' && line[8] <= '4') {
    printDensity = line[8] - '0';
    if (storageReady) prefs.putUChar("density", printDensity);
    Serial.printf("[REGLAGE] Densité=%u (0 = réglage natif, mémorisé)\n", printDensity);
  } else if (line.startsWith("PACE ") || line.startsWith("CHUNK ")) {
    Serial.println("[REGLAGE] BLE : blocs <=182 octets, cadence pilotee par les credits de l'imprimante");
  } else if (line == "TEST") {
    printCalibration();
  } else if (line == "FETCH") {
    lastPoll = millis() - HTTP_POLL_MS;
  } else if (line == "NET") {
    Serial.printf("[NET] Wi-Fi=%s signal=%d dBm RAM=%u bloc=%u\n",
      WiFi.status() == WL_CONNECTED ? "OK" : "hors ligne", WiFi.RSSI(),
      ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    for (const char *host : {"raw.githubusercontent.com", "example.com"}) {
      IPAddress address;
      bool resolved = WiFi.hostByName(host, address);
      Serial.printf("[NET] %s DNS=%s\n", host, resolved ? address.toString().c_str() : "échec");
      if (resolved) {
        WiFiClient probe;
        bool connected = probe.connect(address, 443, 5000);
        Serial.printf("[NET] %s TCP443=%s\n", host, connected ? "OK" : "échec");
        probe.stop();
      }
    }
  } else if (line == "RETRY") {
    if (storageReady) prefs.remove("pending");
    lastBt = millis() - BT_POLL_MS;
    Serial.println("[IMPRESSION] Blocage incertain levé. Une impression partielle peut se répéter.");
  } else if (line == "REPRINT") {
    if (storageReady) { prefs.remove("printed"); prefs.remove("pending"); }
    lastBt = millis() - BT_POLL_MS;
    Serial.println("[IMPRESSION] Réimpression explicitement demandée");
  } else if (line == "SCAN" && btReady) {
    printer.scan();
  } else {
    Serial.println("Commandes : STATUS, FETCH, NET, SCAN, RETRY, REPRINT, TEST, DENSITY 0..4");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nMini-journal Mathias — ESP32 BLE");
  if (LittleFS.begin(false) && prefs.begin("journal", false)) {
    storageReady = true; loadCache();
    printDensity = prefs.getUChar("density", BT_PRINT_DENSITY);
    if (printDensity > 4) printDensity = BT_PRINT_DENSITY;
  } else {
    // Never format silently: that could delete the only cached journal or duplicate guard.
    Serial.println("[FLASH] LittleFS/NVS indisponible. Premier démarrage : téléverser le filesystem vide.");
  }
  btReady = printer.begin();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTzTime(PARIS_TZ, "pool.ntp.org", "time.google.com");
  lastPoll = millis() - HTTP_POLL_MS;
  lastBt = millis() - BT_POLL_MS;
  Serial.println("[DEMARRAGE] Attente Wi-Fi/heure ; STATUS et SCAN disponibles");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') { serialLine.trim(); command(serialLine); serialLine = ""; }
    else if (c != '\r' && serialLine.length() < 80) serialLine += c;
  }
  if (WiFi.status() != WL_CONNECTED && millis() - lastWiFi >= 60000UL) {
    lastWiFi = millis(); WiFi.reconnect(); Serial.println("[WIFI] Reconnexion...");
  }
  if (storageReady && WiFi.status() == WL_CONNECTED && !today().isEmpty() && millis() - lastPoll >= HTTP_POLL_MS) {
    // Release the BLE stack during TLS: both stacks otherwise compete for heap and radio time.
    if (btReady) { printer.end(); btReady = false; }
    bool success = pollJournal();
    btReady = printer.begin();
    lastPoll = millis();
    if (!success) lastPoll -= HTTP_POLL_MS - 60000UL; // Retry network failures after one minute.
  }
  if (millis() - lastBt >= BT_POLL_MS) {
    printIfReady(); lastBt = millis();
  }
  delay(10);
}
