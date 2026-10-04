#pragma once
#include <BluetoothSerial.h>
#include <LittleFS.h>
#include <nvs_flash.h>

#if !defined(CONFIG_BT_SPP_ENABLED)
#error "Bluetooth Classic SPP requires the original ESP32."
#endif

// One continuous raster, acknowledged SPP writes, no fixed pauses between packets.
class JournalClassic {
  BluetoothSerial serial;
  SemaphoreHandle_t completed = xSemaphoreCreateBinary();
  // Use Arduino BluetoothSerial's packet size and transmit queue.
  uint8_t packet[330];
  size_t buffered = 0, accepted = 0;
  volatile uint32_t handle = 0, acknowledged = 0;
  volatile bool congested = false, failed = false;
  uint8_t rxMatch = 0;
  static JournalClassic *&owner() { static JournalClassic *instance = nullptr; return instance; }
  static void event(esp_spp_cb_event_t type, esp_spp_cb_param_t *p) {
    JournalClassic *self = owner();
    if (!self) return;
    if (type == ESP_SPP_OPEN_EVT) {
      self->handle = p->open.status == ESP_SPP_SUCCESS ? p->open.handle : 0;
      self->congested = false;
    } else if (type == ESP_SPP_CLOSE_EVT) {
      Serial.printf("[SPP] Fermeture statut=%d distante=%d ACK=%u\n", int(p->close.status), int(p->close.async), unsigned(self->acknowledged));
      self->handle = 0; self->failed = true;
    }
    else if (type == ESP_SPP_CONG_EVT) self->congested = p->cong.cong;
    else if (type == ESP_SPP_WRITE_EVT) {
      self->congested = p->write.cong;
      if (p->write.status == ESP_SPP_SUCCESS) self->acknowledged += p->write.len;
      else self->failed = true;
    }
  }
  bool flush() {
    if (!buffered) return true;
    uint32_t start = millis();
    while (congested && connected() && !failed && millis() - start < 15000UL) delay(1);
    if (!connected() || failed || congested ||
        serial.write(packet, buffered) != buffered) return false;
    accepted += buffered;
    start = millis();
    while (acknowledged < accepted && connected() && !failed && millis() - start < 15000UL) delay(1);
    if (failed || !connected() || acknowledged < accepted) {
      Serial.printf("[SPP] Envoi non confirme : ACK=%u attendu=%u connecte=%d erreur=%d congestion=%d attente=%u ms\n", unsigned(acknowledged), unsigned(accepted), int(connected()), int(failed), int(congested), unsigned(millis() - start));
      return false;
    }
    buffered = 0;
    return true;
  }
  bool append(const uint8_t *data, size_t n) {
    for (size_t i=0; i<n; i++) {
      packet[buffered++] = data[i];
      if (buffered == sizeof(packet) && !flush()) return false;
    }
    return true;
  }
public:
  bool begin() {
    if (nvs_flash_init() != ESP_OK || !completed) return false;
    owner() = this;
    serial.register_callback(event);
    serial.onData([this](const uint8_t *data, size_t n) {
      const uint8_t done[] = {0x1a, 0x0f, 0x0c};
      for (size_t i=0; i<n; i++) {
        if (data[i] == done[rxMatch]) rxMatch++;
        else rxMatch = data[i] == done[0] ? 1 : 0;
        if (rxMatch == 3) { xSemaphoreGive(completed); rxMatch = 0; }
      }
    });
    if (BT_REQUIRE_PIN) serial.setPin(BT_PIN);
    return serial.begin("Mini-Journal", true);
  }
  bool connected() { return handle && serial.connected(); }
  void disconnect() { serial.disconnect(); handle = 0; }
  void end() { disconnect(); serial.end(); }
  bool connect(const char *mac, const char *name) {
    bool ok;
    failed = false; handle = 0; congested = false;
    if (!strlen(mac)) ok = serial.connect(String(name));
    else {
      unsigned int bytes[6];
      if (sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6) return false;
      uint8_t address[6]; for (int i=0; i<6; i++) address[i]=bytes[i];
      esp_spp_sec_t security = BT_REQUIRE_PIN ? ESP_SPP_SEC_AUTHENTICATE : ESP_SPP_SEC_NONE;
      ok = serial.connect(address, 0, security, ESP_SPP_ROLE_MASTER); // discover SPP channel
    }
    uint32_t start = millis();
    while (ok && !handle && millis() - start < 1500UL) delay(1);
    Serial.printf("[SPP] Connexion=%s\n", ok && connected() ? "OK" : "refusee");
    return ok && connected();
  }
  void scan() {
    serial.discoverAsync([](BTAdvertisedDevice *d) {
      if (d->getName().find("M02") != std::string::npos)
        Serial.printf("[SPP] %s %s\n", d->getName().c_str(), d->getAddress().toString().c_str());
    }, 5000);
    delay(5500); serial.discoverAsyncStop(); serial.discoverClear();
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
    while (xSemaphoreTake(completed, 0) == pdTRUE) {}
    failed = false; acknowledged = 0; accepted = 0; buffered = 0; rxMatch = 0;
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
      Serial.println("[SPP] Fin du raster continu non confirmee ; pas de nouvel essai automatique"); return false;
    }
    uint8_t feed[3];
    if (job.read(feed, 3) != 3 || !append(feed, 3) || !flush()) return false;
    delay(1500); // keep link alive while the final feed is executed
    Serial.printf("[SPP] Journal transmis : %u lignes, %u octets, %u ms\n",
      unsigned(totalRows), unsigned(accepted), unsigned(millis() - started));
    return connected();
  }
};
