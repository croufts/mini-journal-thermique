#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <BluetoothSerial.h>
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

#if !defined(CONFIG_BT_SPP_ENABLED)
#error "An original ESP32 with Bluetooth Classic SPP is required (not S2/S3/C3/C6 or ESP8266)."
#endif
static_assert(BT_CHUNK_BYTES > 0 && BT_CHUNK_BYTES <= 512, "Invalid chunk size");
static_assert(HTTP_POLL_MS >= 60000UL, "HTTP interval must be at least one minute");

BluetoothSerial printer;
Preferences prefs;
bool storageReady = false;
bool btReady = false;
int activeSlot = 0;
String cachedDate, cachedHash, cachedFile;
size_t cachedSize = 0;
uint32_t lastPoll = 0, lastBt = 0, lastWiFi = 0;
String serialLine;
const size_t MAX_JOB_BYTES = 200000;
volatile bool transmitting = false, transportFailed = false;
volatile uint32_t acknowledgedBytes = 0;

void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (!transmitting) return;
  if (event == ESP_SPP_WRITE_EVT) {
    if (param->write.status == ESP_SPP_SUCCESS) acknowledgedBytes += param->write.len;
    else transportFailed = true;
  } else if (event == ESP_SPP_CLOSE_EVT) {
    transportFailed = true;
  }
}

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
  const char *mac = PRINTER_MAC;
  Serial.println("[BT] Recherche/connexion M02 Pro...");
  if (!strlen(mac)) return printer.connect(String(PRINTER_NAME));
  unsigned int bytes[6];
  if (sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", &bytes[0], &bytes[1], &bytes[2],
             &bytes[3], &bytes[4], &bytes[5]) != 6) {
    Serial.println("[BT] Adresse MAC invalide"); return false;
  }
  uint8_t address[6];
  for (int i = 0; i < 6; i++) { if (bytes[i] > 255) return false; address[i] = bytes[i]; }
  esp_spp_sec_t security = BT_REQUIRE_PIN ? ESP_SPP_SEC_AUTHENTICATE : ESP_SPP_SEC_NONE;
  return printer.connect(address, BT_CHANNEL, security, ESP_SPP_ROLE_MASTER);
}

void printIfReady() {
  String day = today();
  if (!storageReady || day.isEmpty() || cachedDate != day || prefs.getString("printed", "") >= day ||
      !prefs.getString("pending", "").isEmpty()) return;
  if (!verifyFile(cachedFile, cachedSize, cachedHash)) {
    Serial.println("[FLASH] Cache corrompu, téléchargement requis"); cachedDate = ""; return;
  }
  if (!connectPrinter()) { Serial.println("[BT] Imprimante éteinte ou connexion refusée"); return; }
  File job = LittleFS.open(cachedFile, "r");
  if (!job) { printer.disconnect(); return; }
  // Record uncertainty BEFORE any bytes can reach the printer. A reset cannot trigger a duplicate.
  if (prefs.putString("pending", day) != day.length()) {
    Serial.println("[NVS] Impossible de mémoriser l'envoi ; impression annulée");
    job.close(); printer.disconnect(); return;
  }
  uint8_t chunk[BT_CHUNK_BYTES];
  transportFailed = false; acknowledgedBytes = 0; transmitting = true;
  bool ok = true;
  size_t sent = 0;
  while (sent < cachedSize) {
    size_t n = job.read(chunk, min(sizeof(chunk), cachedSize - sent));
    if (!n || transportFailed || !printer.connected() || printer.write(chunk, n) != n) { ok = false; break; }
    sent += n;
    delay(BT_CHUNK_DELAY_MS);
    // Drain responses; their meaning is undocumented and must not be treated as print ACKs.
    while (printer.available()) printer.read();
  }
  job.close();
  if (ok && sent == cachedSize) {
    printer.flush(); // Wait until SPP has drained its transmit queue.
    delay(BT_FINISH_DELAY_MS);
    ok = printer.connected() && !transportFailed && acknowledgedBytes == cachedSize;
  }
  transmitting = false;
  Serial.printf("[IMPRESSION] Mis en file=%u/%u confirmés SPP=%u connexion=%s erreur=%s\n",
    unsigned(sent), unsigned(cachedSize), unsigned(acknowledgedBytes),
    printer.connected() ? "OK" : "fermée", transportFailed ? "oui" : "non");
  if (ok && prefs.putString("printed", day) == day.length()) {
    prefs.remove("pending");
    Serial.printf("[IMPRESSION] Journal %s transmis, anti-doublon enregistré\n", day.c_str());
  } else {
    Serial.println("[IMPRESSION] Envoi incertain. Pas de nouvel essai automatique. Vérifier le papier puis RETRY.");
  }
  printer.disconnect();
  if (!ok) {
    // Drop any queued bytes before an explicit retry can open a fresh print session.
    printer.end();
    btReady = printer.begin("Journal-Mathias", true);
    if (BT_REQUIRE_PIN) printer.setPin(BT_PIN);
  }
}

void command(const String &line) {
  if (line == "STATUS") {
    Serial.printf("[STATUS] Aujourd'hui=%s cache=%s Wi-Fi=%s imprimé=%s incertain=%s\n",
      today().c_str(), cachedDate.c_str(), WiFi.status() == WL_CONNECTED ? "OK" : "hors ligne",
      prefs.getString("printed", "").c_str(), prefs.getString("pending", "").c_str());
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
    Serial.println("[BT] Scan Classic pendant 10 secondes...");
    BTScanResults *results = printer.discover(10000);
    if (results) {
      for (int i = 0; i < results->getCount(); i++) Serial.println(results->getDevice(i)->toString().c_str());
    }
    Serial.println("[BT] Scan terminé (absence = éteinte, occupée ou modèle BLE uniquement)");
  } else {
    Serial.println("Commandes : STATUS, FETCH, NET, SCAN, RETRY, REPRINT (fin de ligne obligatoire)");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nMini-journal Mathias — ESP32 Classic");
  if (LittleFS.begin(false) && prefs.begin("journal", false)) {
    storageReady = true; loadCache();
  } else {
    // Never format silently: that could delete the only cached journal or duplicate guard.
    Serial.println("[FLASH] LittleFS/NVS indisponible. Premier démarrage : téléverser le filesystem vide.");
  }
  btReady = printer.begin("Journal-Mathias", true);
  printer.register_callback(sppCallback);
  if (BT_REQUIRE_PIN) printer.setPin(BT_PIN);
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
    // Release the Classic stack during TLS: both stacks otherwise compete for heap and radio time.
    if (btReady) { printer.end(); btReady = false; }
    bool success = pollJournal();
    btReady = printer.begin("Journal-Mathias", true);
    printer.register_callback(sppCallback);
    if (BT_REQUIRE_PIN) printer.setPin(BT_PIN);
    lastPoll = millis();
    if (!success) lastPoll -= HTTP_POLL_MS - 60000UL; // Retry network failures after one minute.
  }
  if (millis() - lastBt >= BT_POLL_MS) {
    printIfReady(); lastBt = millis();
  }
  delay(10);
}
