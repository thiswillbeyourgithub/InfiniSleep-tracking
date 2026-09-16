// Host harness for EventLogController, built against stub headers so the real .cpp compiles
// unchanged. Exists because the numbering and the release protocol are the parts a host depends on,
// and neither can be checked on the watch without a phone at the other end.
#include <cstdio>
#include <vector>

#include "components/log/EventLogController.h"

using namespace Pinetime::Controllers;

static int failures = 0;

#define CHECK(cond)                                                                                                            \
  do {                                                                                                                         \
    if (!(cond)) {                                                                                                             \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                                 \
      failures++;                                                                                                              \
    }                                                                                                                          \
  } while (0)

namespace {
  constexpr uint32_t t0 = 1800000000;  // an arbitrary but realistic epoch, 2027-01-15

  LoggedEvent Ev(uint32_t timestamp,
                 uint8_t slot = 3,
                 LoggedEventType type = LoggedEventType::Punctual,
                 uint8_t value = LoggedEvent::valueNotAsked,
                 bool flagged = false) {
    LoggedEvent event;
    event.timestamp = timestamp;
    event.slot = slot;
    event.type = type;
    event.value = value;
    event.flagged = flagged;
    return event;
  }

  std::vector<LoggedEvent> ReadAll(const EventLogProvider& log, uint32_t since = 0) {
    std::vector<LoggedEvent> all;
    LoggedEvent batch[8];
    while (true) {
      const uint8_t n = log.ReadEvents(since, batch, 8);
      if (n == 0) {
        break;
      }
      for (uint8_t i = 0; i < n; i++) {
        all.push_back(batch[i]);
      }
      since = all.back().sequence;
    }
    return all;
  }
}

int main() {
  printf("ramBudget=%u  events=%u\n", EventLogController::ramBudget, EventLogController::capacity);

  {
    printf("an event round trips, and is numbered from one\n");
    FS fs;
    EventLogController log(fs);
    log.Init();

    CHECK(log.EventCount() == 0);
    CHECK(log.NewestSequence() == 0);

    CHECK(log.Add(Ev(t0 + 42, 7, LoggedEventType::Valued, 8, true)) == 1);
    CHECK(log.Add(Ev(t0 + 400, 2, LoggedEventType::Started)) == 2);
    CHECK(log.EventCount() == 2);
    CHECK(log.OldestSequence() == 1);
    CHECK(log.NewestSequence() == 2);

    const auto all = ReadAll(log);
    CHECK(all.size() == 2);
    CHECK(all[0].timestamp == t0 + 42);  // the first event keeps its exact time, as the base
    CHECK(all[0].slot == 7);
    CHECK(all[0].type == LoggedEventType::Valued);
    CHECK(all[0].value == 8);
    CHECK(all[0].flagged);
    CHECK(all[0].sequence == 1);
    CHECK(all[1].timestamp == t0 + 42 + 300);  // rounded down to the minute from the base
    CHECK(all[1].slot == 2);
    CHECK(all[1].type == LoggedEventType::Started);
    CHECK(all[1].value == LoggedEvent::valueNotAsked);
    CHECK(!all[1].flagged);
    CHECK(all[1].sequence == 2);
  }

  {
    printf("several events in the same minute are all kept\n");
    FS fs;
    EventLogController log(fs);
    log.Init();

    CHECK(log.Add(Ev(t0, 1)) == 1);
    CHECK(log.Add(Ev(t0 + 5, 2)) == 2);
    CHECK(log.Add(Ev(t0 + 9, 3)) == 3);
    CHECK(log.EventCount() == 3);

    const auto all = ReadAll(log);
    CHECK(all.size() == 3);
    CHECK(all[0].slot == 1);
    CHECK(all[1].slot == 2);
    CHECK(all[2].slot == 3);
    bool sameMinute = true;
    for (const auto& event : all) {
      sameMinute = sameMinute && event.timestamp == t0;
    }
    CHECK(sameMinute);  // the seconds are not kept, which is what the numbering is for
  }

  {
    printf("an event dated before the newest is refused\n");
    FS fs;
    EventLogController log(fs);
    log.Init();

    CHECK(log.Add(Ev(t0 + 600)) == 1);
    CHECK(log.Add(Ev(t0)) == 0);  // the clock moved backwards
    CHECK(log.EventCount() == 1);
    CHECK(log.Add(Ev(t0 + 600)) == 2);  // the same minute is fine
    CHECK(log.EventCount() == 2);
  }

  {
    printf("a value outside the scale is stored as no value at all\n");
    FS fs;
    EventLogController log(fs);
    log.Init();

    log.Add(Ev(t0, 1, LoggedEventType::Valued, 0));
    log.Add(Ev(t0 + 60, 2, LoggedEventType::Valued, LoggedEvent::maxValue));
    log.Add(Ev(t0 + 120, 3, LoggedEventType::Valued, 11));
    log.Add(Ev(t0 + 180, 4, LoggedEventType::Valued, 255));

    const auto all = ReadAll(log);
    CHECK(all.size() == 4);
    CHECK(all[0].value == 0);  // zero is a reading, not a missing one
    CHECK(all[1].value == 10);
    CHECK(all[2].value == LoggedEvent::valueNotAsked);
    CHECK(all[3].value == LoggedEvent::valueNotAsked);
  }

  {
    printf("the ring drops the oldest when full, and the numbering carries on\n");
    FS fs;
    EventLogController log(fs);
    log.Init();

    const uint16_t extra = 5;
    for (uint16_t i = 0; i < EventLogController::capacity + extra; i++) {
      log.Add(Ev(t0 + i * 60, static_cast<uint8_t>(i % 200)));
    }
    CHECK(log.EventCount() == EventLogController::capacity);
    CHECK(log.OldestSequence() == extra + 1u);
    CHECK(log.NewestSequence() == EventLogController::capacity + extra);

    const auto all = ReadAll(log);
    CHECK(all.size() == EventLogController::capacity);
    bool ordered = true;
    for (size_t i = 0; i < all.size(); i++) {
      ordered = ordered && all[i].sequence == extra + 1 + i && all[i].timestamp == t0 + (extra + i) * 60;
    }
    CHECK(ordered);
  }

  {
    printf("a release trims from the front only, by sequence\n");
    FS fs;
    EventLogController log(fs);
    log.Init();

    for (uint16_t i = 0; i < 10; i++) {
      log.Add(Ev(t0 + i * 60));
    }
    log.ReleaseEvents(4);
    CHECK(log.EventCount() == 6);
    CHECK(log.OldestSequence() == 5);
    CHECK(ReadAll(log).front().sequence == 5);

    log.ReleaseEvents(2);  // a host repeating itself takes nothing away
    CHECK(log.EventCount() == 6);

    log.ReleaseEvents(1000);
    CHECK(log.EventCount() == 0);
    CHECK(log.NewestSequence() == 0);

    // The numbering carries on where it left off, so a host cannot be given a sequence twice.
    CHECK(log.Add(Ev(t0 + 100 * 60)) == 11);
  }

  {
    printf("reading pages through everything after what the host already has\n");
    FS fs;
    EventLogController log(fs);
    log.Init();

    for (uint16_t i = 0; i < 20; i++) {
      log.Add(Ev(t0 + i * 60, static_cast<uint8_t>(i)));
    }

    LoggedEvent batch[8];
    CHECK(log.ReadEvents(0, batch, 8) == 8);
    CHECK(batch[0].sequence == 1);
    CHECK(log.ReadEvents(8, batch, 8) == 8);
    CHECK(batch[0].sequence == 9);
    CHECK(log.ReadEvents(16, batch, 8) == 4);
    CHECK(batch[3].sequence == 20);
    CHECK(log.ReadEvents(20, batch, 8) == 0);
    CHECK(log.HasBeenRead());
  }

  {
    printf("what is running is read back from the events still held\n");
    FS fs;
    EventLogController log(fs);
    log.Init();

    CHECK(!log.IsRunning(4));
    log.Add(Ev(t0, 4, LoggedEventType::Started));
    CHECK(log.IsRunning(4));
    CHECK(!log.IsRunning(5));  // a different slot is a different thing

    log.Add(Ev(t0 + 60, 5, LoggedEventType::Started));
    log.Add(Ev(t0 + 120, 5, LoggedEventType::Stopped));
    CHECK(log.IsRunning(4));  // the other slot's stop says nothing about this one
    CHECK(!log.IsRunning(5));

    log.Add(Ev(t0 + 180, 4, LoggedEventType::Punctual));
    CHECK(log.IsRunning(4));  // something punctual against the same slot changes nothing

    log.Add(Ev(t0 + 240, 4, LoggedEventType::Stopped));
    CHECK(!log.IsRunning(4));
    log.Add(Ev(t0 + 300, 4, LoggedEventType::Started));
    CHECK(log.IsRunning(4));

    // Collected, so the watch no longer knows, which is the deal: the log is the record and this
    // is a convenience derived from it.
    log.ReleaseEvents(log.NewestSequence());
    CHECK(!log.IsRunning(4));
  }

  {
    printf("save and load round trip through the file, numbering included\n");
    FS fs;
    {
      EventLogController log(fs);
      log.Init();
      for (uint16_t i = 0; i < 12; i++) {
        log.Add(Ev(t0 + i * 60, static_cast<uint8_t>(i), LoggedEventType::Valued, static_cast<uint8_t>(i % 11), i % 2 == 0));
      }
      log.ReleaseEvents(4);  // so the ring is rotated when it is written out
      log.Flush();
      CHECK(fs.contents.size() == 12 + 8 * 4);  // header plus 8 events
    }

    EventLogController reloaded(fs);
    reloaded.Init();
    CHECK(!reloaded.IsDirty());
    CHECK(reloaded.EventCount() == 8);
    CHECK(reloaded.OldestSequence() == 5);
    CHECK(reloaded.NewestSequence() == 12);

    const auto all = ReadAll(reloaded);
    CHECK(all.size() == 8);
    CHECK(all[0].slot == 4);
    CHECK(all[0].value == 4);
    CHECK(all[0].flagged);
    CHECK(all[0].timestamp == t0 + 4 * 60);
    CHECK(all.back().slot == 11);
    CHECK(!all.back().flagged);

    // A host that had stored up to 12 asks for what came after, and is told nothing came after,
    // rather than being handed the same events under new numbers.
    CHECK(reloaded.Add(Ev(t0 + 100 * 60)) == 13);
  }

  {
    printf("a file from an older firmware is discarded, not misread\n");
    FS fs;
    fs.exists = true;
    fs.contents = {0, 4, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // version 0
    EventLogController log(fs);
    log.Init();
    CHECK(log.EventCount() == 0);
  }

  {
    printf("flush only writes when something changed\n");
    FS fs;
    EventLogController log(fs);
    log.Init();
    log.Add(Ev(t0));
    log.Flush();
    const size_t written = fs.contents.size();
    fs.contents.clear();
    log.Flush();  // nothing changed since
    CHECK(fs.contents.empty());
    log.Add(Ev(t0 + 60));
    log.Flush();
    CHECK(fs.contents.size() == written + 4);
  }

  {
    printf("clearing keeps the numbering, so a host can still tell events apart\n");
    FS fs;
    EventLogController log(fs);
    log.Init();
    log.Add(Ev(t0));
    log.Add(Ev(t0 + 60));
    log.Clear();
    CHECK(log.EventCount() == 0);
    CHECK(log.Add(Ev(t0 + 120)) == 3);
  }

  if (failures == 0) {
    printf("\nall checks passed\n");
    return 0;
  }
  printf("\n%d checks failed\n", failures);
  return 1;
}
