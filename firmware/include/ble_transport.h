#pragma once
#include <NimBLEDevice.h>
#include <LittleFS.h>
#include <nvs_flash.h>

// Phomemo FF00 UART: FF02 writes, FF03 credits (01 01) and completion (1A 0F 0C).
// The canvas remains 626 px; older 79-byte rows are resampled to 576 actual dots.
class JournalBle {
  NimBLEClient *client = nullptr;
  NimBLERemoteCharacteristic *writeChar = nullptr;
  SemaphoreHandle_t credits = xSemaphoreCreateCounting(16, 0);
  SemaphoreHandle_t completed = xSemaphoreCreateBinary();
  size_t packetSize = 182, buffered = 0;
  uint8_t packet[182];
  volatile uint32_t creditCount = 0;
  size_t accepted = 0;

  bool flush() {
    if (!buffered) return true;
    if (!connected() || xSemaphoreTake(credits, pdMS_TO_TICKS(10000)) != pdTRUE || !connected()) {
      Serial.println("[BLE] Credit absent ou liaison fermee ; envoi arrete"); return false;
    }
    if (!writeChar->writeValue(packet, buffered, false)) {
      Serial.println("[BLE] Ecriture refusee"); return false;
    }
    accepted += buffered; buffered = 0;
    return true;
  }

  bool append(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
      packet[buffered++] = data[i];
      if (buffered == packetSize && !flush()) return false;
    }
    return true;
  }

public:
  bool begin() {
    // NimBLE's recovery path can erase NVS. Refuse its initialization if NVS is
    // unhealthy, so a library recovery cannot erase our daily duplicate guard.
    if (nvs_flash_init() != ESP_OK || !credits || !completed) {
      Serial.println("[BLE] NVS indisponible ; aucun effacement automatique"); return false;
    }
    NimBLEDevice::init("Journal-Mathias");
    NimBLEDevice::setMTU(185);
    NimBLEDevice::setSecurityAuth(true, false, false); // encrypted bond, no MITM UI
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    return NimBLEDevice::getInitialized();
  }

  bool connected() { return client && client->isConnected(); }

  void disconnect() {
    if (connected()) {
      client->disconnect();
      uint32_t started = millis();
      while (connected() && millis() - started < 2000UL) delay(10);
    }
    writeChar = nullptr;
  }

  void end() {
    disconnect();
    if (client) { NimBLEDevice::deleteClient(client); client = nullptr; }
    if (NimBLEDevice::getInitialized()) NimBLEDevice::deinit(false);
  }

  bool connect(const char *mac, const char *name) {
    disconnect();
    if (!client) client = NimBLEDevice::createClient();
    if (!client) return false;
    client->setConnectTimeout(10);
    // Direct connections after stack restart failed on this unit (HCI 0x3E).
    // Use a fresh advertisement, including its address type, for every attempt.
    bool ok = false;
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    Serial.println("[BLE] Debut scan 3 s");
    scan->start(3, nullptr, false);
    uint32_t scanStarted = millis();
    while (scan->isScanning() && millis() - scanStarted < 4000UL) delay(20);
    if (scan->isScanning()) scan->stop();
    NimBLEScanResults results = scan->getResults();
    Serial.printf("[BLE] Scan termine : %d appareils\n", results.getCount());
    for (int i = 0; i < results.getCount(); i++) {
      NimBLEAdvertisedDevice device = results.getDevice(i);
      String address(device.getAddress().toString().c_str());
      bool match = strlen(mac) ? address.equalsIgnoreCase(mac) : device.getName() == name;
      if (match) { Serial.println("[BLE] Appareil trouve, connexion..."); ok = client->connect(&device); Serial.println("[BLE] Retour connexion"); break; }
    }
    scan->clearResults();
    if (!ok) { Serial.printf("[BLE] Connexion refusee (%d)\n", client->getLastError()); return false; }
    Serial.println("[BLE] Verification chiffrement");
    if (!client->getConnInfo().isEncrypted()) {
      int rc = NimBLEDevice::startSecurity(client->getConnId());
      if (rc && rc != BLE_HS_EALREADY) {
        Serial.printf("[BLE] Chiffrement refuse (%d)\n", rc); disconnect(); return false;
      }
      uint32_t started = millis();
      while (connected() && !client->getConnInfo().isEncrypted() && millis() - started < 15000UL) delay(20);
    }
    if (!connected() || !client->getConnInfo().isEncrypted()) {
      Serial.println("[BLE] Chiffrement non confirme sous 15 s"); disconnect(); return false;
    }
    NimBLERemoteService *service = client->getService("ff00");
    if (!service) { disconnect(); return false; }
    writeChar = service->getCharacteristic("ff02");
    NimBLERemoteCharacteristic *notify = service->getCharacteristic("ff03");
    if (!writeChar || !writeChar->canWriteNoResponse() || !notify || !notify->canNotify()) {
      disconnect(); return false;
    }
    if (!notify->subscribe(true, [this](NimBLERemoteCharacteristic *, uint8_t *data, size_t n, bool) {
      if (n == 2 && data[0] == 1 && data[1] == 1) {
        creditCount++;
        xSemaphoreGive(credits);
      }
      if (n == 3 && data[0] == 0x1a && data[1] == 0x0f && data[2] == 0x0c)
        xSemaphoreGive(completed);
      // Device-info notifications may contain a serial number: never log them.
    }, true)) { disconnect(); return false; }
    packetSize = min(size_t(182), size_t(client->getMTU() - 3));
    Serial.printf("[BLE] Chiffre, FF02/FF03, MTU=%u blocs=%u\n", client->getMTU(), unsigned(packetSize));
    return true;
  }

  void scan() {
    NimBLEScan *scanner = NimBLEDevice::getScan();
    scanner->setActiveScan(true);
    scanner->start(5, nullptr, false);
    uint32_t started = millis();
    while (scanner->isScanning() && millis() - started < 6000UL) delay(20);
    if (scanner->isScanning()) scanner->stop();
    NimBLEScanResults results = scanner->getResults();
    for (int i = 0; i < results.getCount(); i++) {
      auto device = results.getDevice(i);
      if (device.getName().find("M02") != std::string::npos)
        Serial.printf("[BLE] %s %s\n", device.getName().c_str(), device.getAddress().toString().c_str());
    }
    scanner->clearResults();
  }

  static bool validate(File &job) {
    job.seek(0);
    uint8_t header[5];
    if (job.read(header, 4) != 4) return false;
    const uint8_t prefix[] = {0x10, 0xff, 0xfe, 1};
    if (!memcmp(header, prefix, 4)) {
      if (job.read(header, 5) != 5) return false;
    } else if (job.read(header + 4, 1) != 1) return false;
    const uint8_t start[] = {0x1b, 0x40, 0x1b, 0x61};
    if (memcmp(header, start, 4) || header[4] > 1) return false;
    uint32_t rows = 0;
    while (job.available() > 3) {
      uint8_t raster[8];
      if (job.read(raster, 8) != 8 || memcmp(raster, "\x1d\x76\x30\x00", 4)) return false;
      uint16_t stride = raster[4] | (raster[5] << 8), count = raster[6] | (raster[7] << 8);
      if ((stride != 72 && stride != 79) || !count || count > 255 || rows + count > 2362 ||
          job.position() + size_t(stride) * count + 3 > job.size()) return false;
      rows += count;
      if (!job.seek(job.position() + size_t(stride) * count)) return false;
    }
    uint8_t feed[3];
    return rows && job.read(feed, 3) == 3 && feed[0] == 0x1b && feed[1] == 0x64 && feed[2] <= 10 && !job.available();
  }

  bool send(File &job, uint8_t density) {
    job.seek(0);
    uint8_t input[79], output[72];
    if (job.read(input, 4) != 4) return false;
    size_t skip = input[0] == 0x10 ? 5 : 1;
    if (!job.seek(job.position() + skip)) return false;
    while (xSemaphoreTake(credits, 0) == pdTRUE) {}
    while (xSemaphoreTake(completed, 0) == pdTRUE) {}
    for (int i = 0; i < 4; i++) xSemaphoreGive(credits);
    creditCount = 0; accepted = 0; buffered = 0;
    uint32_t started = millis();
    const uint8_t prologue[] = {0x1b, 0x40, 0x1b, 0x61, 0};
    if (!append(prologue, sizeof(prologue))) return false;
    const uint8_t energy[] = {0x1f, 0x11, 0x02, density};
    if (density && !append(energy, sizeof(energy))) return false;
    // Merge stored bands into one GS v 0 image: band boundaries must not stop
    // the motor or re-trigger the print head across a line of text.
    size_t rasterStart = job.position();
    uint16_t imageRows = 0;
    while (job.available() > 3) {
      uint8_t band[8];
      if (job.read(band, 8) != 8) return false;
      uint16_t stride = band[4] | (band[5] << 8), rows = band[6] | (band[7] << 8);
      imageRows += rows;
      if (!job.seek(job.position() + size_t(stride) * rows)) return false;
    }
    if (!job.seek(rasterStart)) return false;
    const uint8_t wholeRaster[] = {0x1d, 0x76, 0x30, 0, 72, 0,
      uint8_t(imageRows), uint8_t(imageRows >> 8)};
    if (!append(wholeRaster, sizeof(wholeRaster))) return false;
    uint32_t totalRows = 0;
    while (job.available() > 3) {
      uint8_t raster[8];
      if (job.read(raster, 8) != 8) return false;
      uint16_t stride = raster[4] | (raster[5] << 8), rows = raster[6] | (raster[7] << 8);
      for (int y = 0; y < rows; y++) {
        if (job.read(input, stride) != stride) return false;
        if (stride == 79) {
          memset(output, 0, sizeof(output));
          for (int x = 0; x < 576; x++) {
            int source = x * 626 / 576;
            if (input[source / 8] & (0x80 >> (source % 8))) output[x / 8] |= 0x80 >> (x % 8);
          }
        } else memcpy(output, input, sizeof(output));
        if (!append(output, sizeof(output))) return false;
      }
      totalRows += rows;
    }
    if (!flush()) return false;
    if (xSemaphoreTake(completed, pdMS_TO_TICKS(45000)) != pdTRUE || !connected()) {
      Serial.println("[BLE] Fin du raster continu non confirmee ; pas de nouvel essai automatique"); return false;
    }
    uint8_t feed[3];
    if (job.read(feed, 3) != 3 || !append(feed, 3) || !flush()) return false;
    delay(1500); // keep link alive while the final feed is executed
    Serial.printf("[BLE] Journal transmis : %u lignes, %u octets, %u ms\n",
      unsigned(totalRows), unsigned(accepted), unsigned(millis() - started));
    return connected();
  }
};
