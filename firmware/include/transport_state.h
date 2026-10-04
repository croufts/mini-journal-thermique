#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace JournalTransport {
enum class Phase : uint8_t { idle, connecting, settling, initialization, raster, completion, feed, finished };
enum class Failure : uint8_t { none, connection, disconnected, congestion, queue, writeStatus, ackLength,
                              ackTimeout, file, completionTimeout };
inline const char *phaseName(Phase p) {
  switch (p) {
    case Phase::connecting: return "connexion";
    case Phase::settling: return "stabilisation";
    case Phase::initialization: return "initialisation";
    case Phase::raster: return "image";
    case Phase::completion: return "confirmation-image";
    case Phase::feed: return "avance-papier";
    case Phase::finished: return "termine";
    default: return "repos";
  }
}
inline const char *failureName(Failure f) {
  switch (f) {
    case Failure::connection: return "connexion-refusee";
    case Failure::disconnected: return "connexion-perdue";
    case Failure::congestion: return "congestion-expiree";
    case Failure::queue: return "file-envoi-refusee";
    case Failure::writeStatus: return "ecriture-bluetooth";
    case Failure::ackLength: return "longueur-ACK-invalide";
    case Failure::ackTimeout: return "delai-ACK";
    case Failure::file: return "lecture-fichier";
    case Failure::completionTimeout: return "delai-confirmation-image";
    default: return "aucune";
  }
}
struct Diagnostic {
  Phase phase = Phase::idle;
  Failure failure = Failure::none;
  uint32_t accepted = 0, acknowledged = 0, expected = 0, rows = 0, elapsed = 0;
  uint32_t ignoredEvents = 0, earlyCompletions = 0;
  int writeStatus = -1, closeStatus = -1;
  bool remoteClose = false, imageCompleted = false;
  uint8_t received[16] = {};
  uint8_t receivedCount = 0;
};

// The ESP adapter guards every access with the same short critical section.
// No protocol interpretation outside the current connection can affect an attempt.
struct State {
  Diagnostic d;
  uint32_t handle = 0;
  bool congested = false, awaitingWrite = false, completionArmed = false;
  uint32_t pendingBytes = 0;
  uint8_t rxMatch = 0;
  void reset() { *this = State(); d.phase = Phase::connecting; }
  bool accepts(uint32_t h) {
    if (handle && h == handle) return true;
    ++d.ignoredEvents; return false;
  }
  void fail(Failure f) { if (d.failure == Failure::none) d.failure = f; }
  void opened(uint32_t h, bool success) {
    if (!h || !success || d.phase != Phase::connecting || handle) {
      ++d.ignoredEvents; return;
    }
    handle = h; congested = false;
  }
  void closed(uint32_t h, int status, bool remote) {
    if (!accepts(h)) return;
    d.closeStatus = status; d.remoteClose = remote;
    handle = 0; fail(Failure::disconnected);
  }
  void congestion(uint32_t h, bool value) { if (accepts(h)) congested = value; }
  void beginWrite(uint32_t n) {
    pendingBytes = n; awaitingWrite = true; d.accepted += n;
  }
  void wrote(uint32_t h, bool success, int status, uint32_t n, bool congestionValue) {
    if (!accepts(h)) return;
    if (!awaitingWrite) { ++d.ignoredEvents; return; }
    d.writeStatus = status; congested = congestionValue; awaitingWrite = false;
    if (!success) { fail(Failure::writeStatus); return; }
    if (n != pendingBytes) { fail(Failure::ackLength); return; }
    d.acknowledged += n;
  }
  void armCompletion() { completionArmed = true; rxMatch = 0; d.imageCompleted = false; }
  void received(uint32_t h, const uint8_t *bytes, size_t n) {
    if (!accepts(h)) return;
    const uint8_t done[] = {0x1a, 0x0f, 0x0c};
    for (size_t i = 0; i < n; ++i) {
      if (d.receivedCount == sizeof(d.received)) {
        memmove(d.received, d.received + 1, sizeof(d.received) - 1);
        --d.receivedCount;
      }
      d.received[d.receivedCount++] = bytes[i];
      rxMatch = bytes[i] == done[rxMatch] ? rxMatch + 1 : bytes[i] == done[0] ? 1 : 0;
      if (rxMatch == sizeof(done)) {
        if (completionArmed) d.imageCompleted = true;
        else ++d.earlyCompletions;
        rxMatch = 0;
      }
    }
  }
};
}
