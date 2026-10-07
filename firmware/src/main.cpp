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
#include "classic_transport.h"

#ifndef GITHUB_ACTIONS_TOKEN
#define GITHUB_ACTIONS_TOKEN ""
#endif
#ifndef DAILY_START_HOUR
#define DAILY_START_HOUR 5
#endif
static_assert(DAILY_START_HOUR >= 0 && DAILY_START_HOUR < 24, "Invalid daily start hour");

#ifndef BT_PRINT_DENSITY
#define BT_PRINT_DENSITY 0
#endif

static_assert(BT_PRINT_DENSITY >= 0 && BT_PRINT_DENSITY <= 4, "Density must stay within the conservative configured range");
static_assert(HTTP_POLL_MS >= 60000UL, "HTTP interval must be at least one minute");

JournalClassic printer;
Preferences prefs;
bool storageReady = false;
bool btReady = false;
bool refreshBeforePrint = false;
bool manualPrint = false;
String wifiIdleDay;
uint32_t scheduledPrint = 0;
int activeSlot = 0;
String cachedDate, cachedHash, cachedFile;
size_t cachedSize = 0;
uint32_t lastPoll = 0, lastBt = 0, lastWiFi = 0;
String serialLine;
uint8_t printDensity = BT_PRINT_DENSITY;
uint8_t connectionFailures = 0;
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

bool cycleDue() {
  time_t now = time(nullptr);
  if (now < 1704067200) return false;
  struct tm local;
  localtime_r(&now, &local);
  return local.tm_hour >= DAILY_START_HOUR;
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
  if (!url.startsWith("https://raw.githubusercontent.com/") &&
      !url.startsWith("https://api.github.com/repos/")) {
    Serial.println("[HTTPS] Hote GitHub requis");
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

bool triggerGeneration() {
  if (!strlen(GITHUB_ACTIONS_TOKEN)) {
    Serial.println("[GENERATION] Jeton absent : attente du cron GitHub");
    return false;
  }
  String base = JOURNAL_BASE_URL;
  const String prefix = "https://raw.githubusercontent.com/";
  if (!base.startsWith(prefix)) return false;
  int ownerEnd = base.indexOf('/', prefix.length());
  int repoEnd = base.indexOf('/', ownerEnd + 1);
  if (ownerEnd < 0 || repoEnd < 0) return false;
  String repository = base.substring(prefix.length(), repoEnd);
  WiFiClientSecure tls;
  HTTPClient http;
  if (!beginHttp(http, tls, "https://api.github.com/repos/" + repository +
      "/actions/workflows/journal.yml/dispatches")) return false;
  http.addHeader("Authorization", String("Bearer ") + GITHUB_ACTIONS_TOKEN);
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "MiniJournal-ESP32");
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");
  int status = http.POST("{\"ref\":\"main\"}");
  http.end();
  // The API may return either an empty success or the created run details.
  bool ok = status == 204 || status == 200;
  Serial.printf("[GENERATION] Demande directe GitHub HTTP=%d %s\n", status, ok ? "acceptee" : "echec");
  return ok;
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
  if (!ok || received != size || actualHash != hash) {
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
    Serial.printf("[JOURNAL] Édition %s ignorée (pas aujourd'hui)\n", date.c_str()); return false;
  }
  Serial.printf("[JOURNAL] Manifeste du jour : %s\n", filename.c_str());
  // Refresh the cache even after printing; the NVS guard prevents duplicates
  // independently. Otherwise an explicit reprint uses an obsolete edition.
  if (date == cachedDate && hash == cachedHash && !refreshBeforePrint) return true;
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
  Serial.println("[SPP] Recherche/connexion M02 Pro...");
  return printer.connect(PRINTER_MAC, PRINTER_NAME);
}

void wakeWifi() {
  wifiIdleDay = "";
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// Restore Wi-Fi on failure, leave it off after a confirmed daily print.
struct SuspendWifiForPrint {
  wifi_mode_t previousMode;
  SuspendWifiForPrint() : previousMode(WiFi.getMode()) {
    bool stopped = WiFi.mode(WIFI_OFF);
    delay(250);
    Serial.printf("[BT] Wi-Fi suspendu=%s RAM=%u\n", stopped ? "oui" : "echec", unsigned(ESP.getFreeHeap()));
  }
  ~SuspendWifiForPrint() {
    if (wifiIdleDay.isEmpty()) {
      WiFi.mode(previousMode);
      if (previousMode & WIFI_STA) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }
};

// A single bounded NVS record, never one flash write per Bluetooth packet.
// Repeated identical connection failures keep the first report to avoid wear.
bool savePrintDiagnostic(const char *outcome, const char *kind = "journal") {
  if (!storageReady) return false;
  auto d = printer.diagnostic();
  char received[sizeof(d.received) * 2 + 1] = {};
  for (size_t i = 0; i < d.receivedCount; ++i) snprintf(received + i * 2, 3, "%02x", d.received[i]);
  StaticJsonDocument<1024> record;
  record["schema"] = 1;
  record["type"] = kind;
  record["densite"] = printDensity;
  record["debit_max_octets_s"] = JournalTransport::RASTER_BYTES_PER_SECOND;
  record["date"] = today();
  record["hash"] = cachedHash.substring(0, 16);
  record["resultat"] = outcome;
  record["phase"] = JournalTransport::phaseName(d.phase);
  record["cause"] = JournalTransport::failureName(d.failure);
  record["octets_envoyes"] = d.accepted;
  record["octets_confirmes"] = d.acknowledged;
  record["octets_prevus"] = d.expected;
  record["lignes_lues"] = d.rows;
  record["statut_ecriture"] = d.writeStatus;
  record["statut_fermeture"] = d.closeStatus;
  record["fermeture_distante"] = d.remoteClose;
  record["fin_image"] = d.imageCompleted;
  record["derniere_reponse_hex"] = received;
  String signature;
  serializeJson(record, signature);
  // Times and event counters don't make an otherwise identical failure worth
  // another flash write. They remain in every newly stored report.
  String previous = prefs.getString("printSig", "");
  if (signature == previous && !prefs.getString("lastPrint", "").isEmpty()) return true;
  record["duree_ms"] = d.elapsed;
  record["heure_unix"] = uint32_t(time(nullptr));
  record["evenements_ignores"] = d.ignoredEvents;
  record["fins_image_prematurees"] = d.earlyCompletions;
  String report;
  serializeJson(record, report);
  if (record.overflowed() || prefs.putString("lastPrint", report) != report.length()) {
    Serial.println("[DIAGNOSTIC] Impossible de conserver le compte rendu en NVS"); return false;
  }
  prefs.putString("printSig", signature);
  Serial.printf("[DIAGNOSTIC] %s\n", report.c_str());
  return true;
}

void showPrintDiagnostic() {
  String report = storageReady ? prefs.getString("lastPrint", "") : "";
  if (report.isEmpty()) Serial.println("[DIAGNOSTIC] Aucun compte rendu memorise");
  else Serial.printf("[DIAGNOSTIC] Derniere tentative : %s\n", report.c_str());
}

void printIfReady() {
  String day = today();
  if (storageReady && !day.isEmpty()) {
    String pending = prefs.getString("pending", "");
    if (!pending.isEmpty() && pending < day) {
      prefs.remove("pending");
      Serial.println("[IMPRESSION] Ancien envoi incertain clos ; nouvelle edition autorisee");
    }
  }
  if ((!manualPrint && !cycleDue()) || refreshBeforePrint || !storageReady || day.isEmpty() || cachedDate != day || prefs.getString("printed", "") >= day ||
      !prefs.getString("pending", "").isEmpty()) return;
  File job = LittleFS.open(cachedFile, "r");
  if (!job || !JournalClassic::validate(job)) {
    Serial.println("[FLASH] Encodage raster invalide ; aucune impression"); job.close(); return;
  }
  SuspendWifiForPrint wifiPause;
  if (!connectPrinter()) {
    savePrintDiagnostic("connexion-refusee");
    printer.disconnect();
    Serial.println("[SPP] Imprimante éteinte ou connexion refusée");
    job.close();
    if (++connectionFailures >= 3) {
      Serial.println("[SPP] Trois connexions refusees : redemarrage du Bluetooth");
      printer.end(); btReady = false;
      delay(500);
      btReady = printer.begin();
      connectionFailures = 0;
    }
    return;
  }
  connectionFailures = 0;
  if (!printer.prepare(printDensity)) {
    savePrintDiagnostic("preparation-echouee");
    Serial.println("[SPP] Preparation interrompue avant l'image ; nouvelle connexion au prochain cycle");
    job.close(); printer.disconnect(); return;
  }
  if (prefs.putString("pending", day) != day.length()) {
    Serial.println("[NVS] Impossible de mémoriser l'envoi ; impression annulée");
    job.close(); printer.disconnect(); return;
  }
  // Persist the start as well: a sudden power cut must leave evidence even
  // without a serial monitor. Exact counters are saved when send() returns.
  if (!savePrintDiagnostic("en-cours")) {
    Serial.println("[IMPRESSION] Envoi annule : diagnostic non memorise ; blocage incertain conserve");
    job.close(); printer.disconnect(); return;
  }
  bool ok = printer.send(job);
  job.close();
  savePrintDiagnostic(ok ? "transmission-confirmee" : "envoi-incertain");
  if (ok && prefs.putString("printed", day) == day.length()) {
    prefs.remove("pending");
    wifiIdleDay = day;
    manualPrint = false;
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WIFI] Coupe apres impression confirmee, jusqu'au prochain jour ou une commande explicite");
    Serial.printf("[IMPRESSION] Journal %s transmis, fin du raster continu reçue, anti-doublon enregistré\n", day.c_str());
  } else {
    if (ok) savePrintDiagnostic("anti-doublon-non-memorise");
    Serial.println("[IMPRESSION] Envoi incertain. Pas de nouvel essai automatique. Vérifier le papier puis RETRY.");
  }
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
  ok = ok && job && job.size() == sizeof(calibrationTicket) && JournalClassic::validate(job);
  if (ok) {
    SuspendWifiForPrint wifiPause;
    ok = connectPrinter() && printer.prepare(printDensity);
    if (ok) ok = savePrintDiagnostic("en-cours", "calibration") && printer.send(job);
    savePrintDiagnostic(ok ? "transmission-confirmee" : "echec", "calibration");
    printer.disconnect();
  }
  job.close(); LittleFS.remove(path);
  Serial.printf("[TEST] SPP resultat=%s ; cache et anti-doublon conserves\n", ok ? "OK" : "incertain");
}

void command(const String &line) {
  if (line == "STATUS") {
    Serial.printf("[PLAN] Prochain essai=%u UTC Unix\n", scheduledPrint);
    Serial.printf("[STATUS] Aujourd'hui=%s cache=%s Wi-Fi=%s imprimé=%s incertain=%s densite=%u transport=Classic-SPP largeur=576 hash=%s actualisation=%s\n",
      today().c_str(), cachedDate.c_str(), WiFi.status() == WL_CONNECTED ? "OK" : "hors ligne",
      prefs.getString("printed", "").c_str(), prefs.getString("pending", "").c_str(), printDensity,
      cachedHash.substring(0, 16).c_str(), refreshBeforePrint ? "requise" : "OK");
    showPrintDiagnostic();
  } else if (line.length() == 9 && line.startsWith("DENSITY ") && line[8] >= '0' && line[8] <= '4') {
    printDensity = line[8] - '0';
    if (storageReady) prefs.putUChar("density", printDensity);
    Serial.printf("[REGLAGE] Densité=%u (0 = réglage natif, mémorisé)\n", printDensity);

  } else if (line == "TEST") {
    printCalibration();
  } else if (line == "GENERATE") {
    wakeWifi();
    // A diagnostic request does not clear the cache or the duplicate guard.
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000UL) delay(10);
    if (btReady) { printer.end(); btReady = false; }
    if (WiFi.status() == WL_CONNECTED) triggerGeneration();
    else Serial.println("[GENERATION] Wi-Fi indisponible");
    btReady = printer.begin();
    lastPoll = millis() - HTTP_POLL_MS;
  } else if (line == "FETCH") {
    wakeWifi();
    lastPoll = millis() - HTTP_POLL_MS;
  } else if (line == "NET") {
    wakeWifi();
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
  } else if (line.startsWith("PRINTAT ")) {
    String value = line.substring(8);
    for (char c : value) if (!isDigit(c)) { Serial.println("[PLAN] Heure invalide"); return; }
    uint32_t target = strtoul(value.c_str(), nullptr, 10);
    time_t now = time(nullptr);
    if (!storageReady || value.length() != 10 || now < 1704067200 || target <= now || target - now > 86400 ||
        prefs.putUInt("printAt", target) != sizeof(uint32_t)) {
      Serial.println("[PLAN] Programmation refusee"); return;
    }
    scheduledPrint = target;
    Serial.printf("[PLAN] Impression unique programmee : %u UTC Unix\n", scheduledPrint);
  } else if (line == "REPRINT") {
    if (!storageReady || prefs.putBool("refresh", true) != 1) {
      Serial.println("[NVS] Réimpression annulée : actualisation non mémorisée"); return;
    }
    prefs.remove("printed"); prefs.remove("pending");
    refreshBeforePrint = true;
    manualPrint = true;
    wakeWifi();
    lastPoll = millis() - HTTP_POLL_MS;
    lastBt = millis() - BT_POLL_MS;
    Serial.println("[IMPRESSION] Réimpression demandée, actualisation Internet obligatoire avant l'envoi");
  } else if (line == "SCAN" && btReady) {
    printer.scan();
  } else {
    Serial.println("Commandes : STATUS, GENERATE, FETCH, NET, SCAN, RETRY, REPRINT, PRINTAT timestamp, TEST, DENSITY 0..4");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nMini-journal thermique — ESP32 Classic SPP");
  if (LittleFS.begin(false) && prefs.begin("journal", false)) {
    storageReady = true; loadCache();
    refreshBeforePrint = prefs.getBool("refresh", false);
    manualPrint = refreshBeforePrint;
    scheduledPrint = prefs.getUInt("printAt", 0);
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
  Serial.printf("[CYCLE] Demarrage quotidien %02d:00 Europe/Paris ; declenchement direct=%s\n",
    DAILY_START_HOUR, strlen(GITHUB_ACTIONS_TOKEN) ? "configure" : "absent");
  Serial.println("[DEMARRAGE] Attente Wi-Fi/heure ; STATUS et SCAN disponibles");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') { serialLine.trim(); command(serialLine); serialLine = ""; }
    else if (c != '\r' && serialLine.length() < 80) serialLine += c;
  }
  if (scheduledPrint && time(nullptr) >= scheduledPrint) {
    uint32_t target = scheduledPrint;
    // Disarm durably before changing guards. A stale test must never repeat tomorrow.
    if (prefs.remove("printAt")) {
      scheduledPrint = 0;
      if (time(nullptr) - target <= 3600) command("REPRINT");
      else Serial.println("[PLAN] Essai expire, impression quotidienne conservee");
    }
  }
  if (!wifiIdleDay.isEmpty() && !today().isEmpty() && today() != wifiIdleDay && cycleDue()) {
    wakeWifi();
    lastPoll = millis() - HTTP_POLL_MS;
    Serial.println("[WIFI] Nouveau jour : reprise du telechargement");
  }
  if (wifiIdleDay.isEmpty() && WiFi.status() != WL_CONNECTED && millis() - lastWiFi >= 60000UL) {
    lastWiFi = millis(); WiFi.reconnect(); Serial.println("[WIFI] Reconnexion...");
  }
  // Once the current edition is cached, leave the Bluetooth stack intact.
  // Repeated TLS polls used to tear it down while waiting for the printer.
  bool needsJournal = cachedDate != today() || refreshBeforePrint;
  if (storageReady && WiFi.status() == WL_CONNECTED && !today().isEmpty() &&
      (cycleDue() || refreshBeforePrint) && needsJournal && millis() - lastPoll >= HTTP_POLL_MS) {
    // Release the Bluetooth stack during TLS: both stacks otherwise compete for heap and radio time.
    if (btReady) { printer.end(); btReady = false; }
    bool success = pollJournal();
    if (!success && cachedDate != today() && strlen(GITHUB_ACTIONS_TOKEN)) {
      uint32_t now = uint32_t(time(nullptr));
      uint32_t previous = prefs.getUInt("dispatchAt", 0);
      if (!previous || now < previous || now - previous >= 600UL) {
        // Retry at most every ten minutes, including after power cycling.
        prefs.putUInt("dispatchAt", now);
        triggerGeneration();
      }
    }
    if (success) {
      prefs.remove("refresh");
      refreshBeforePrint = prefs.getBool("refresh", false);
    }
    btReady = printer.begin();
    lastPoll = millis();
    if (!success) lastPoll -= HTTP_POLL_MS - 60000UL; // Retry network failures after one minute.
  }
  if (millis() - lastBt >= BT_POLL_MS) {
    printIfReady();
    if (wifiIdleDay.isEmpty() && storageReady && !today().isEmpty() &&
        !refreshBeforePrint && scheduledPrint == 0 && prefs.getString("pending", "").isEmpty() &&
        prefs.getString("printed", "") >= today()) {
      wifiIdleDay = today();
      WiFi.setAutoReconnect(false);
      WiFi.mode(WIFI_OFF);
      Serial.printf("[WIFI] Jour deja imprime : attente du cycle de %02d:00\n", DAILY_START_HOUR);
    }
    lastBt = millis();
  }
  delay(10);
}
