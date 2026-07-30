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

  ActivityRecord Rec(uint32_t timestamp, uint8_t hr = 60, uint16_t motion = 1234) {
    ActivityRecord record;
    record.timestamp = timestamp;
    record.heartRate = hr;
    record.motion = motion;
    record.kind = ActivityKind::Asleep;
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
  printf("capacity=%u  sizeof(record in ring)=%u bytes\n",
         ActivityLogController::capacity,
         (unsigned) (ActivityLogController::ramBudget / ActivityLogController::capacity));

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
    for (uint16_t i = 0; i < ActivityLogController::capacity + extra; i++) {
      log.Add(Rec(t0 + i * 300, static_cast<uint8_t>(50 + i % 50)));
    }
    CHECK(log.RecordCount() == ActivityLogController::capacity);
    CHECK(log.OldestTimestamp() == t0 + extra * 300);
    CHECK(log.NewestTimestamp() == t0 + (ActivityLogController::capacity + extra - 1) * 300);

    const auto all = ReadAll(log);
    CHECK(all.size() == ActivityLogController::capacity);
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
    log2.Add(Rec(t0 + 60u * 60 * 24 * 40));
    CHECK(log2.RecordCount() == 2);
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
      CHECK(fs.contents.size() == 8 + 34 * 6);  // header plus 34 packed records
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
    CHECK(fs.contents.size() == written + 6);
  }

  if (failures == 0) {
    printf("\nall checks passed\n");
    return 0;
  }
  printf("\n%d checks failed\n", failures);
  return 1;
}
