// Host harness for ActivityLogController, built against stub headers so the real .cpp compiles
// unchanged. Exists because the packed record cannot be tested on the watch without a night.
#include <cstdio>
#include <vector>

#include "components/activity/ActivityLogController.h"

using namespace Pinetime::Controllers;

static int failures = 0;

#define CHECK(cond)                                                                                                                        \
  do {                                                                                                                                     \
    if (!(cond)) {                                                                                                                         \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                                             \
      failures++;                                                                                                                          \
    }                                                                                                                                      \
  } while (0)

namespace {
  constexpr uint32_t t0 = 1800000000;  // an arbitrary but realistic epoch, 2027-01-15

  /// What a watch whose accelerometer never answers records, which is a real watch rather than a
  /// hypothetical one, and the case the narrow layout exists for.
  constexpr uint16_t noMotion = ActivityRecord::motionNotMeasured;

  /// How many records fit in each layout. Spelled out rather than derived so that a change to
  /// either stride has to be looked at here too.
  constexpr uint16_t narrowSlots = 682;  // 2048 / 3
  constexpr uint16_t wideSlots = 409;    // 2048 / 5

  ActivityRecord Rec(uint32_t timestamp, uint8_t hr = 60, uint16_t motion = 1234, ActivityKind kind = ActivityKind::Asleep) {
    ActivityRecord record;
    record.timestamp = timestamp;
    record.heartRate = hr;
    record.motion = motion;
    record.kind = kind;
    return record;
  }

  std::vector<ActivityRecord> ReadAll(const ActivityLogController& log, uint32_t since = 0) {
    std::vector<ActivityRecord> all;
    ActivityRecord batch[20];
    while (true) {
      const uint8_t n = log.ReadRecords(since, batch, 20);
      if (n == 0) {
        break;
      }
      for (uint8_t i = 0; i < n; i++) {
        all.push_back(batch[i]);
      }
      since = all.back().timestamp;
    }
    return all;
  }
}

int main() {
  printf("ramBudget=%u  records: %u without motion, %u with\n",
         ActivityLogController::ramBudget,
         ActivityLogController::maxCapacity,
         wideSlots);

  {
    printf("round trip, and timestamps land on the minute relative to the base\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    log.Add(Rec(t0 + 17, 55, 9));         // odd seconds, becomes the base
    log.Add(Rec(t0 + 17 + 300 + 42, 61));  // 5 minutes and 42 seconds later
    CHECK(log.RecordCount() == 2);

    const auto all = ReadAll(log);
    CHECK(all.size() == 2);
    CHECK(all[0].timestamp == t0 + 17);  // the first record keeps its exact time
    CHECK(all[0].heartRate == 55);
    CHECK(all[0].motion == 9);
    CHECK(all[0].kind == ActivityKind::Asleep);
    CHECK(all[1].timestamp == t0 + 17 + 300);  // the 42 seconds are rounded off
    CHECK(all[1].heartRate == 61);
    CHECK(log.OldestTimestamp() == all[0].timestamp);
    CHECK(log.NewestTimestamp() == all[1].timestamp);
  }

  {
    printf("out of order and same minute records are dropped\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    log.Add(Rec(t0));
    log.Add(Rec(t0 - 60));  // clock moved backwards
    log.Add(Rec(t0));       // exactly the same instant
    log.Add(Rec(t0 + 59));  // same minute, would round onto the previous record
    CHECK(log.RecordCount() == 1);
    log.Add(Rec(t0 + 60));
    CHECK(log.RecordCount() == 2);
  }

  {
    printf("the ring drops the oldest when full, and stays readable\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    const uint16_t extra = 10;
    CHECK(log.Capacity() == ActivityLogController::maxCapacity);  // nothing stored yet
    for (uint16_t i = 0; i < wideSlots + extra; i++) {
      log.Add(Rec(t0 + i * 300, static_cast<uint8_t>(50 + i % 50)));
    }
    CHECK(log.Capacity() == wideSlots);  // the records carry motion, so fewer of them fit
    CHECK(log.RecordCount() == wideSlots);
    CHECK(log.OldestTimestamp() == t0 + extra * 300);
    CHECK(log.NewestTimestamp() == t0 + (wideSlots + extra - 1) * 300);

    const auto all = ReadAll(log);
    CHECK(all.size() == wideSlots);
    bool ordered = true;
    for (size_t i = 1; i < all.size(); i++) {
      ordered = ordered && all[i].timestamp > all[i - 1].timestamp;
    }
    CHECK(ordered);
  }

  {
    printf("release trims from the front only\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    for (uint16_t i = 0; i < 10; i++) {
      log.Add(Rec(t0 + i * 300));
    }
    log.Release(t0 + 4 * 300);
    CHECK(log.RecordCount() == 5);
    CHECK(log.OldestTimestamp() == t0 + 5 * 300);
    log.Release(t0 + 100 * 300);
    CHECK(log.RecordCount() == 0);
    CHECK(log.OldestTimestamp() == 0);

    // and the ring still works afterwards
    log.Add(Rec(t0 + 200 * 300));
    CHECK(log.RecordCount() == 1);
    CHECK(log.NewestTimestamp() == t0 + 200 * 300);
  }

  {
    printf("dropping trims from the back only, and leaves the ring usable\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    for (uint16_t i = 0; i < 10; i++) {
      log.Add(Rec(t0 + i * 300));
    }
    log.DropSince(t0 + 7 * 300);
    CHECK(log.RecordCount() == 7);
    CHECK(log.OldestTimestamp() == t0);
    CHECK(log.NewestTimestamp() == t0 + 6 * 300);

    // A timestamp older than everything held empties it, which is the session that is taken
    // back in full.
    log.DropSince(t0);
    CHECK(log.RecordCount() == 0);

    log.Add(Rec(t0 + 100 * 300));
    CHECK(log.RecordCount() == 1);
    CHECK(log.NewestTimestamp() == t0 + 100 * 300);

    // A timestamp newer than everything held drops nothing.
    log.DropSince(t0 + 200 * 300);
    CHECK(log.RecordCount() == 1);
  }

  {
    printf("dropping across a wrap keeps the records that are kept readable\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    // Fill past capacity so head is well away from zero and the ring has wrapped.
    for (uint16_t i = 0; i < wideSlots + 20; i++) {
      log.Add(Rec(t0 + i * 300, static_cast<uint8_t>(50 + i % 50)));
    }
    const uint32_t cut = log.NewestTimestamp() - 5 * 300;
    log.DropSince(cut);
    CHECK(log.RecordCount() == wideSlots - 6);
    CHECK(log.NewestTimestamp() == cut - 300);

    const auto all = ReadAll(log);
    CHECK(all.size() == static_cast<size_t>(wideSlots - 6));
    bool ordered = true;
    for (size_t i = 1; i < all.size(); i++) {
      ordered = ordered && all[i].timestamp > all[i - 1].timestamp;
    }
    CHECK(ordered);
  }

  {
    printf("remarking rewrites the tail, only the kind asked for, and moves nothing\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    log.Add(Rec(t0 + 0 * 300));
    log.Add(Rec(t0 + 1 * 300));
    log.Add(Rec(t0 + 2 * 300, 60, 1234, ActivityKind::Unknown));  // a background poll in the way
    log.Add(Rec(t0 + 3 * 300));
    log.Add(Rec(t0 + 4 * 300));

    log.Remark(t0 + 2 * 300, ActivityKind::Asleep, ActivityKind::Awake);

    const auto all = ReadAll(log);
    CHECK(all.size() == 5);
    CHECK(all[0].kind == ActivityKind::Asleep);  // before the cut, left alone
    CHECK(all[1].kind == ActivityKind::Asleep);
    CHECK(all[2].kind == ActivityKind::Unknown);  // at the cut, but not the kind asked for
    CHECK(all[3].kind == ActivityKind::Awake);
    CHECK(all[4].kind == ActivityKind::Awake);
    CHECK(all[0].timestamp == t0);  // nothing moved, and nothing was added or dropped
    CHECK(all[4].timestamp == t0 + 4 * 300);
    CHECK(all[3].heartRate == 60);
    CHECK(log.RecordCount() == 5);
  }

  {
    printf("remarking reaches across a wrap, and only dirties the log when it changed something\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    for (uint16_t i = 0; i < wideSlots + 20; i++) {
      log.Add(Rec(t0 + i * 300));
    }
    log.Flush();

    fs.contents.clear();
    const uint32_t cut = log.NewestTimestamp() - 3 * 300;
    log.Remark(cut, ActivityKind::Asleep, ActivityKind::Awake);
    log.Flush();
    CHECK(!fs.contents.empty());  // the correction reached flash

    const auto all = ReadAll(log);
    CHECK(all.size() == wideSlots);
    CHECK(all[all.size() - 5].kind == ActivityKind::Asleep);
    CHECK(all[all.size() - 4].kind == ActivityKind::Awake);
    CHECK(all.back().kind == ActivityKind::Awake);

    // Newer than everything held, so there is nothing to correct and nothing to write.
    fs.contents.clear();
    log.Remark(log.NewestTimestamp() + 300, ActivityKind::Asleep, ActivityKind::Awake);
    log.Flush();
    CHECK(fs.contents.empty());
  }

  {
    printf("a release is remembered, so the watch can say whether a host is collecting\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    CHECK(!log.HasBeenCollected());
    CHECK(!log.HasBeenRead());
    log.Add(Rec(t0));
    CHECK(!log.HasBeenCollected());

    // Reading is stamped even when there is nothing to hand back, since what it records is that
    // the request arrived.
    ActivityRecord out[4];
    CHECK(log.ReadRecords(t0, out, 4) == 0);
    CHECK(log.HasBeenRead());

    // Stamped even when the release frees nothing, since a host with nothing left to acknowledge
    // is still a host that is collecting.
    log.Release(t0 - 300);
    CHECK(log.HasBeenCollected());
    CHECK(log.RecordCount() == 1);
  }

  {
    printf("a gap longer than the delta can hold rebases instead of losing the night\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    log.Add(Rec(t0));
    log.Release(t0);  // collected, so the ring is empty but the base is still t0
    CHECK(log.RecordCount() == 0);

    // Refill from a much later date. The first record after an empty ring resets the base, so
    // this alone must not overflow anything.
    const uint32_t later = t0 + 60u * 60 * 24 * 100;  // 100 days on
    log.Add(Rec(later));
    CHECK(log.RecordCount() == 1);
    CHECK(log.NewestTimestamp() == later);
  }

  {
    printf("records older than the span are dropped rather than the new one\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    log.Add(Rec(t0));
    log.Add(Rec(t0 + 300));
    // 100 days later, with the old records never collected. A rebase cannot help, since the
    // records held are themselves that old.
    const uint32_t later = t0 + 60u * 60 * 24 * 100;
    log.Add(Rec(later));
    CHECK(log.RecordCount() == 1);
    CHECK(log.NewestTimestamp() == later);
    CHECK(log.OldestTimestamp() == later);

    // 40 days is inside the span, so nothing is lost.
    FS fs2;
    ActivityLogController log2(fs2);
    log2.Init();
    log2.Add(Rec(t0));
    log2.Add(Rec(t0 + 60u * 60 * 24 * 10));  // 10 days on, still within reach of the base
    CHECK(log2.RecordCount() == 2);

    // Just past it, so the oldest goes and the rest stay, rather than the log being emptied.
    log2.Add(Rec(t0 + 60u * 60 * 24 * 12));
    CHECK(log2.RecordCount() == 2);
    CHECK(log2.OldestTimestamp() == t0 + 60u * 60 * 24 * 10);
    CHECK(log2.NewestTimestamp() == t0 + 60u * 60 * 24 * 12);
  }

  {
    printf("save and load round trip through the file\n");
    FS fs;
    {
      ActivityLogController log(fs);
      log.Init();
      for (uint16_t i = 0; i < 40; i++) {
        log.Add(Rec(t0 + i * 300, static_cast<uint8_t>(40 + i), static_cast<uint16_t>(i * 7)));
      }
      log.Release(t0 + 5 * 300);  // so the ring is rotated when it is written out
      log.Flush();
      CHECK(fs.contents.size() == 8 + 34 * 5);  // header plus 34 wide records
    }

    ActivityLogController reloaded(fs);
    reloaded.Init();
    CHECK(reloaded.RecordCount() == 34);
    CHECK(reloaded.OldestTimestamp() == t0 + 6 * 300);
    CHECK(reloaded.NewestTimestamp() == t0 + 39 * 300);

    const auto all = ReadAll(reloaded);
    CHECK(all.size() == 34);
    CHECK(all[0].heartRate == 46);
    CHECK(all[0].motion == 42);
    CHECK(all.back().heartRate == 79);
  }

  {
    printf("a file from an older firmware is discarded, not misread\n");
    FS fs;
    fs.exists = true;
    fs.contents = {1, 8, 3, 0, 0, 0, 0, 0};  // version 1, 8 byte records
    ActivityLogController log(fs);
    log.Init();
    CHECK(log.RecordCount() == 0);
  }

  {
    printf("a truncated file is discarded, not misread\n");
    FS fs;
    {
      ActivityLogController log(fs);
      log.Init();
      for (uint16_t i = 0; i < 10; i++) {
        log.Add(Rec(t0 + i * 300));
      }
      log.Flush();
    }
    fs.contents.resize(fs.contents.size() - 10);
    ActivityLogController truncated(fs);
    truncated.Init();
    CHECK(truncated.RecordCount() == 0);
  }

  {
    printf("flush only writes when something changed\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();
    log.Add(Rec(t0));
    log.Flush();
    const size_t written = fs.contents.size();
    fs.contents.clear();
    log.Flush();  // nothing changed since
    CHECK(fs.contents.empty());
    log.Add(Rec(t0 + 300));
    log.Flush();
    CHECK(fs.contents.size() == written + 5);
  }

  {
    printf("a watch that measures no motion holds half again as many records\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    for (uint16_t i = 0; i < narrowSlots + 7; i++) {
      log.Add(Rec(t0 + i * 300, static_cast<uint8_t>(50 + i % 50), noMotion));
    }
    CHECK(log.Capacity() == narrowSlots);
    CHECK(log.RecordCount() == narrowSlots);
    CHECK(log.OldestTimestamp() == t0 + 7 * 300);
    CHECK(log.NewestTimestamp() == t0 + (narrowSlots + 6) * 300);

    const auto all = ReadAll(log);
    CHECK(all.size() == narrowSlots);
    bool intact = true;
    for (size_t i = 0; i < all.size(); i++) {
      intact = intact && all[i].motion == noMotion && all[i].kind == ActivityKind::Asleep &&
               all[i].heartRate == static_cast<uint8_t>(50 + (i + 7) % 50);
      if (i > 0) {
        intact = intact && all[i].timestamp == all[i - 1].timestamp + 300;
      }
    }
    CHECK(intact);

    log.Flush();
    CHECK(fs.contents.size() == 8u + narrowSlots * 3u);  // three bytes a record, not five
  }

  {
    printf("the first record that carries motion widens the log without losing the night\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    for (uint16_t i = 0; i < 30; i++) {
      log.Add(Rec(t0 + i * 300, static_cast<uint8_t>(60 + i), noMotion));
    }
    CHECK(log.Capacity() == narrowSlots);

    log.Add(Rec(t0 + 30 * 300, 99, 4321));
    CHECK(log.Capacity() == wideSlots);
    CHECK(log.RecordCount() == 31);

    const auto all = ReadAll(log);
    CHECK(all.size() == 31);
    CHECK(all[0].timestamp == t0);
    CHECK(all[0].heartRate == 60);
    CHECK(all[0].motion == noMotion);  // it still says what it measured, which was nothing
    CHECK(all[29].heartRate == 89);
    CHECK(all.back().timestamp == t0 + 30 * 300);
    CHECK(all.back().heartRate == 99);
    CHECK(all.back().motion == 4321);
  }

  {
    printf("widening a full and wrapped log keeps the newest records that fit\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    // Past the narrow capacity, so the ring has wrapped and the oldest record is not at slot
    // zero, which is the case the rotation inside the widening is there for.
    for (uint16_t i = 0; i < narrowSlots + 40; i++) {
      log.Add(Rec(t0 + i * 300, static_cast<uint8_t>(50 + i % 50), noMotion));
    }
    CHECK(log.RecordCount() == narrowSlots);

    const uint32_t newestBefore = log.NewestTimestamp();
    log.Add(Rec(newestBefore + 300, 77, 4321));
    CHECK(log.Capacity() == wideSlots);
    CHECK(log.RecordCount() == wideSlots);
    CHECK(log.NewestTimestamp() == newestBefore + 300);
    CHECK(log.OldestTimestamp() == newestBefore + 300 - (wideSlots - 1) * 300);

    const auto all = ReadAll(log);
    CHECK(all.size() == wideSlots);
    bool ordered = true;
    for (size_t i = 1; i < all.size(); i++) {
      ordered = ordered && all[i].timestamp == all[i - 1].timestamp + 300;
    }
    CHECK(ordered);
    CHECK(all.back().motion == 4321);
    CHECK(all[all.size() - 2].motion == noMotion);
  }

  {
    printf("the layout survives a reboot, so a watch does not widen again every boot\n");
    FS narrowFs;
    {
      ActivityLogController log(narrowFs);
      log.Init();
      for (uint16_t i = 0; i < 20; i++) {
        log.Add(Rec(t0 + i * 300, 61, noMotion));
      }
      log.Flush();
    }
    ActivityLogController narrow(narrowFs);
    narrow.Init();
    CHECK(narrow.Capacity() == narrowSlots);
    CHECK(narrow.RecordCount() == 20);
    CHECK(ReadAll(narrow).back().motion == noMotion);

    FS wideFs;
    {
      ActivityLogController log(wideFs);
      log.Init();
      for (uint16_t i = 0; i < 20; i++) {
        log.Add(Rec(t0 + i * 300, 61, static_cast<uint16_t>(i * 11)));
      }
      log.Flush();
    }
    ActivityLogController wide(wideFs);
    wide.Init();
    CHECK(wide.Capacity() == wideSlots);
    CHECK(!wide.IsDirty());  // loading a file is not a change to what it holds
    CHECK(wide.RecordCount() == 20);
    CHECK(ReadAll(wide).back().motion == 19 * 11);
  }

  {
    printf("clearing starts over narrow, since an empty log needs no room for motion\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();
    log.Add(Rec(t0, 60, 1234));
    CHECK(log.Capacity() == wideSlots);
    log.Clear();
    CHECK(log.Capacity() == narrowSlots);
    log.Add(Rec(t0 + 300, 60, noMotion));
    CHECK(log.Capacity() == narrowSlots);
    CHECK(log.RecordCount() == 1);
  }

  {
    printf("corrections and drops reach the kind without disturbing the delta beside it\n");
    FS fs;
    ActivityLogController log(fs);
    log.Init();

    for (uint16_t i = 0; i < 6; i++) {
      log.Add(Rec(t0 + i * 300, static_cast<uint8_t>(70 + i), noMotion));
    }
    log.Remark(t0 + 3 * 300, ActivityKind::Asleep, ActivityKind::Awake);
    log.DropSince(t0 + 5 * 300);

    const auto all = ReadAll(log);
    CHECK(all.size() == 5);
    CHECK(all[2].kind == ActivityKind::Asleep);
    CHECK(all[3].kind == ActivityKind::Awake);
    CHECK(all[4].kind == ActivityKind::Awake);
    bool intact = true;
    for (size_t i = 0; i < all.size(); i++) {
      intact = intact && all[i].timestamp == t0 + i * 300 && all[i].heartRate == static_cast<uint8_t>(70 + i) &&
               all[i].motion == noMotion;
    }
    CHECK(intact);
  }

  {
    printf("a version 2 file, from before motion moved out of the record, is discarded\n");
    FS fs;
    fs.exists = true;
    fs.contents = {2, 6, 3, 0, 0, 0, 0, 0};  // version 2, 6 byte records
    ActivityLogController log(fs);
    log.Init();
    CHECK(log.RecordCount() == 0);
  }

  if (failures == 0) {
    printf("\nall checks passed\n");
    return 0;
  }
  printf("\n%d checks failed\n", failures);
  return 1;
}
