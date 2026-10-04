#pragma once
#include <BluetoothSerial.h>
#include <LittleFS.h>
#include <nvs_flash.h>
#include "transport_state.h"

#if !defined(CONFIG_BT_SPP_ENABLED)
#error "Bluetooth Classic SPP requires the original ESP32."
#endif

// One continuous raster, acknowledged SPP writes, no fixed pauses between packets.
class JournalClassic {
  BluetoothSerial serial;
  JournalTransport::State state;
  portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
  struct Lock {
    portMUX_TYPE *mux;
    explicit Lock(portMUX_TYPE &value) : mux(&value) { portENTER_CRITICAL(mux); }
    ~Lock() { portEXIT_CRITICAL(mux); }
  };
  // Use Arduino BluetoothSerial's packet size and transmit queue.
  uint8_t packet[330];
  size_t buffered = 0;
  uint32_t started = 0;
  bool prepared = false;
  JournalTransport::State snapshot() { Lock lock(stateMux); return state; }
  void phase(JournalTransport::Phase value) { Lock lock(stateMux); state.d.phase = value; }
  bool fail(JournalTransport::Failure reason) {
    Lock lock(stateMux); state.fail(reason); state.d.elapsed = millis() - started;
    prepared = false; return false;
  }
  bool healthy() {
    auto current = snapshot();
    return current.handle && current.d.failure == JournalTransport::Failure::none && serial.connected();
  }
  bool stableFor(uint32_t duration) {
    uint32_t begin = millis();
    while (millis() - begin < duration) {
      if (!healthy()) return fail(JournalTransport::Failure::disconnected);
      delay(5);
    }
    return healthy() || fail(JournalTransport::Failure::disconnected);
  }
  static JournalClassic *&owner() { static JournalClassic *instance = nullptr; return instance; }
  static void event(esp_spp_cb_event_t type, esp_spp_cb_param_t *p) {
    JournalClassic *self = owner();
    if (!self) return;
    Lock lock(self->stateMux);
    if (type == ESP_SPP_OPEN_EVT) {
      self->state.opened(p->open.handle, p->open.status == ESP_SPP_SUCCESS);
    } else if (type == ESP_SPP_CLOSE_EVT) {
      self->state.closed(p->close.handle, int(p->close.status), p->close.async);
    }
    else if (type == ESP_SPP_CONG_EVT) self->state.congestion(p->cong.handle, p->cong.cong);
    else if (type == ESP_SPP_WRITE_EVT) {
      self->state.wrote(p->write.handle, p->write.status == ESP_SPP_SUCCESS,
                        int(p->write.status), p->write.len, p->write.cong);
    } else if (type == ESP_SPP_DATA_IND_EVT)
      self->state.received(p->data_ind.handle, p->data_ind.data, p->data_ind.len);
  }
  bool flush(bool finalRaster = false) {
    if (!buffered) return true;
    uint32_t start = millis();
    while (snapshot().congested && healthy() && millis() - start < 15000UL) delay(1);
    if (!healthy()) return fail(JournalTransport::Failure::disconnected);
    if (snapshot().congested) return fail(JournalTransport::Failure::congestion);
    {
      Lock lock(stateMux);
      if (finalRaster) state.armCompletion();
      state.beginWrite(buffered); // Before enqueueing: a callback may arrive immediately.
    }
    if (serial.write(packet, buffered) != buffered) {
      Lock lock(stateMux);
      state.d.accepted -= buffered; state.awaitingWrite = false;
      state.fail(JournalTransport::Failure::queue); prepared = false;
      state.d.elapsed = millis() - started; return false;
    }
    start = millis();
    while (snapshot().awaitingWrite && healthy() && millis() - start < 15000UL) delay(1);
    if (!healthy()) return fail(JournalTransport::Failure::disconnected);
    if (snapshot().awaitingWrite) return fail(JournalTransport::Failure::ackTimeout);
    buffered = 0;
    return true;
  }
  bool append(const uint8_t *data, size_t n, bool finalRaster = false) {
    for (size_t i=0; i<n; i++) {
      packet[buffered++] = data[i];
      if (buffered == sizeof(packet) && !flush(finalRaster && i + 1 == n)) return false;
    }
    return true;
  }
public:
  bool begin() {
    if (nvs_flash_init() != ESP_OK) return false;
    owner() = this;
    serial.register_callback(event);
    // Avoid filling Arduino's receive queue. The registered event callback above
    // processes these same bytes WITH their connection handle.
    serial.onData([](const uint8_t *, size_t) {});
    if (BT_REQUIRE_PIN) serial.setPin(BT_PIN);
    return serial.begin("Mini-Journal", true);
  }
  bool connected() { return snapshot().handle && serial.connected(); }
  JournalTransport::Diagnostic diagnostic() {
    auto current = snapshot().d;
    if (current.phase != JournalTransport::Phase::idle && current.phase != JournalTransport::Phase::finished)
      current.elapsed = millis() - started;
    return current;
  }
  void disconnect() {
    { Lock lock(stateMux); state.handle = 0; }
    prepared = false; serial.disconnect();
  }
  void end() { disconnect(); serial.end(); }
  bool connect(const char *mac, const char *name) {
    bool ok;
    started = millis(); prepared = false; buffered = 0;
    { Lock lock(stateMux); state.reset(); }
    if (!strlen(mac)) ok = serial.connect(String(name));
    else {
      unsigned int bytes[6];
      if (sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6) return fail(JournalTransport::Failure::connection);
      uint8_t address[6]; for (int i=0; i<6; i++) address[i]=bytes[i];
      esp_spp_sec_t security = BT_REQUIRE_PIN ? ESP_SPP_SEC_AUTHENTICATE : ESP_SPP_SEC_NONE;
      ok = serial.connect(address, 0, security, ESP_SPP_ROLE_MASTER); // discover SPP channel
    }
    uint32_t start = millis();
    while (ok && !snapshot().handle && millis() - start < 1500UL) delay(1);
    Serial.printf("[SPP] Connexion=%s\n", ok && connected() ? "OK" : "refusee");
    return (ok && connected()) || fail(JournalTransport::Failure::connection);
  }
  bool prepare(uint8_t density) {
    using namespace JournalTransport;
    phase(Phase::settling);
    Serial.println("[SPP] Stabilisation de la connexion (2 s), avant toute image");
    if (!stableFor(2000)) return false;
    phase(Phase::initialization);
    const uint8_t prologue[] = {0x1b, 0x40, 0x1b, 0x61, 0};
    const uint8_t energy[] = {0x1f, 0x11, 0x02, density};
    { Lock lock(stateMux); state.d.expected = sizeof(prologue) + (density ? sizeof(energy) : 0); }
    if (!append(prologue, sizeof(prologue)) ||
        (density && !append(energy, sizeof(energy))) || !flush() || !stableFor(300)) return false;
    prepared = true;
    Serial.println("[SPP] Initialisation transmise et confirmee ; envoi de l'image autorise");
    return true;
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

  bool send(File &job) {
    using namespace JournalTransport;
    if (!prepared || !healthy()) return fail(Failure::connection);
    job.seek(0);
    uint8_t input[79], output[72];
    if (job.read(input, 4) != 4) return fail(Failure::file);
    size_t skip = input[0] == 0x10 ? 5 : 1;
    if (!job.seek(job.position() + skip)) return fail(Failure::file);
    phase(Phase::raster);
    // Merge stored bands into one GS v 0 image: band boundaries must not stop
    // the motor or re-trigger the print head across a line of text.
    size_t rasterStart = job.position();
    uint16_t imageRows = 0;
    while (job.available() > 3) {
      uint8_t band[8];
      if (job.read(band, 8) != 8) return fail(Failure::file);
      uint16_t stride = band[4] | (band[5] << 8), rows = band[6] | (band[7] << 8);
      imageRows += rows;
      if (!job.seek(job.position() + size_t(stride) * rows)) return fail(Failure::file);
    }
    if (!job.seek(rasterStart)) return fail(Failure::file);
    { Lock lock(stateMux); state.d.expected += 8 + uint32_t(imageRows) * 72 + 3; }
    const uint8_t wholeRaster[] = {0x1d, 0x76, 0x30, 0, 72, 0,
      uint8_t(imageRows), uint8_t(imageRows >> 8)};
    if (!append(wholeRaster, sizeof(wholeRaster))) return false;
    uint32_t totalRows = 0;
    while (job.available() > 3) {
      uint8_t raster[8];
      if (job.read(raster, 8) != 8) return fail(Failure::file);
      uint16_t stride = raster[4] | (raster[5] << 8), rows = raster[6] | (raster[7] << 8);
      for (int y = 0; y < rows; y++) {
        if (job.read(input, stride) != stride) return fail(Failure::file);
        if (stride == 79) {
          memset(output, 0, sizeof(output));
          for (int x = 0; x < 576; x++) {
            int source = x * 626 / 576;
            if (input[source / 8] & (0x80 >> (source % 8))) output[x / 8] |= 0x80 >> (x % 8);
          }
        } else memcpy(output, input, sizeof(output));
        bool finalRow = totalRows + y + 1 == imageRows;
        { Lock lock(stateMux); state.d.rows = totalRows + y + 1; }
        if (!append(output, sizeof(output), finalRow)) return false;
      }
      totalRows += rows;
    }
    if (!flush(true)) return false;
    phase(Phase::completion);
    uint32_t waitStarted = millis();
    while (!snapshot().d.imageCompleted && healthy() && millis() - waitStarted < 45000UL) delay(2);
    if (!healthy()) return fail(Failure::disconnected);
    if (!snapshot().d.imageCompleted) return fail(Failure::completionTimeout);
    phase(Phase::feed);
    uint8_t feed[3];
    if (job.read(feed, 3) != 3) return fail(Failure::file);
    if (!append(feed, 3) || !flush() || !stableFor(1500)) return false;
    prepared = false;
    { Lock lock(stateMux); state.d.phase = Phase::finished; state.d.elapsed = millis() - started; }
    Serial.printf("[SPP] Journal transmis : %u lignes, %u octets, %u ms\n",
      unsigned(totalRows), unsigned(snapshot().d.accepted), unsigned(millis() - started));
    return connected();
  }
};
