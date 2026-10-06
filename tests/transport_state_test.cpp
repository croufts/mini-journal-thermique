#include "transport_state.h"
#include <cassert>
#include <iostream>
using namespace JournalTransport;

State connected() {
  State s;
  s.reset(); s.opened(42, true);
  s.d.phase = Phase::raster;
  return s;
}

int main() {
  {
    PacketPacer p;
    assert(p.remaining(0) == 0);
    p.sent(100, 330);
    assert(p.remaining(100) == 110 && p.remaining(209) == 1);
    assert(p.remaining(210) == 0);
    // A delayed ACK allows one packet, not a burst that catches up lost time.
    assert(p.remaining(5000) == 0);
    p.sent(5000, 330); assert(p.remaining(5000) == 110);
    p.sent(UINT32_MAX - 49, 330);
    assert(p.remaining(50) == 10 && p.remaining(60) == 0);
    p.sent(100, 1); assert(p.remaining(100) == 1);
  }
  const uint8_t done[] = {0x1a, 0x0f, 0x0c};
  {
    auto s = connected(); s.beginWrite(330);
    s.wrote(99, false, 1, 330, true);
    s.congestion(99, true); s.closed(99, 1, true);
    s.received(99, done, sizeof(done));
    assert(s.handle == 42 && s.awaitingWrite && !s.congested);
    assert(s.d.failure == Failure::none && s.d.acknowledged == 0);
    assert(s.d.receivedCount == 0 && s.d.ignoredEvents == 4);
    s.wrote(42, true, 0, 330, false);
    assert(!s.awaitingWrite && s.d.acknowledged == 330);
    s.wrote(42, true, 0, 330, false); // Duplicate cannot credit another packet.
    assert(s.d.acknowledged == 330 && s.d.ignoredEvents == 5);
  }
  {
    auto s = connected(); s.beginWrite(330);
    s.wrote(42, true, 0, 329, false);
    assert(s.d.failure == Failure::ackLength && s.d.acknowledged == 0);
    s.fail(Failure::disconnected);
    assert(s.d.failure == Failure::ackLength); // Preserve the original cause.
  }
  {
    auto s = connected(); s.beginWrite(330);
    s.wrote(42, false, 7, 330, false);
    assert(s.d.failure == Failure::writeStatus && s.d.writeStatus == 7);
    assert(s.d.acknowledged == 0);
  }
  {
    auto s = connected(); s.congestion(42, true);
    assert(s.congested);
    s.congestion(42, false); assert(!s.congested);
    s.closed(42, 3, true);
    assert(s.handle == 0 && s.d.failure == Failure::disconnected);
    assert(s.d.closeStatus == 3 && s.d.remoteClose);
  }
  {
    auto s = connected();
    s.received(42, done, sizeof(done));
    assert(!s.d.imageCompleted && s.d.earlyCompletions == 1);
    s.received(42, done, 2); // A prefix from initialization cannot finish a job.
    s.armCompletion(); s.received(42, done + 2, 1);
    assert(!s.d.imageCompleted);
    s.received(42, done, 1); s.received(42, done + 1, 2);
    assert(s.d.imageCompleted);
    s.reset(); s.opened(43, true);
    s.received(42, done, sizeof(done));
    assert(!s.d.imageCompleted && s.d.ignoredEvents == 1);
  }
  {
    auto s = connected(); uint8_t bytes[40];
    for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = uint8_t(i + 30);
    s.received(42, bytes, sizeof(bytes));
    assert(s.d.receivedCount == 16);
    for (size_t i = 0; i < 16; ++i) assert(s.d.received[i] == bytes[24 + i]);
  }
  std::cout << "Transport state: stale events, ACK failures, split/early replies and bounded diagnostics passed\n";
}
