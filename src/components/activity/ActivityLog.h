#pragma once

#include <cstdint>

namespace Pinetime {
  namespace Controllers {

    /// What the watch believes the wearer was doing over an epoch.
    ///
    /// Deliberately small and deliberately not a mirror of any host application's constants:
    /// the watch should not encode one companion app's internals into its wire format. Hosts
    /// map these onto whatever they use.
    ///
    /// Note there is no "deep sleep". Distinguishing sleep stages is not something actigraphy
    /// and an occasional heart rate reading can honestly support, so the firmware does not
    /// claim it.
    enum class ActivityKind : uint8_t {
      Unknown = 0,
      Awake = 1,
      Asleep = 2,
      NotWorn = 3,
    };

    /// One epoch of recorded activity.
    ///
    /// Field order is chosen so the struct is exactly 8 bytes with no padding on the target,
    /// which keeps the in memory layout and the on the wire layout the same size. The wire
    /// encoding is still written out explicitly by the BLE service rather than memcpy'd, so
    /// the protocol does not silently depend on the compiler's layout choices.
    struct ActivityRecord {
      /// Unix epoch seconds, UTC.
      uint32_t timestamp = 0;
      /// Accumulated actigraphy counts over the epoch. notMeasured when there is no usable
      /// accelerometer, which is a real case rather than a theoretical one.
      uint16_t motion = motionNotMeasured;
      /// Beats per minute, or heartRateNotMeasured.
      uint8_t heartRate = heartRateNotMeasured;
      ActivityKind kind = ActivityKind::Unknown;

      static constexpr uint16_t motionNotMeasured = 0xFFFF;
      static constexpr uint8_t heartRateNotMeasured = 0;
    };

    static_assert(sizeof(ActivityRecord) == 8, "ActivityRecord is expected to pack into 8 bytes");

    /// Where the BLE service gets its records from.
    ///
    /// Kept this narrow on purpose. The transport has no opinion about how sleep is tracked,
    /// which is what lets it be reviewed and merged independently of any particular sleep
    /// tracking implementation.
    class ActivityLogProvider {
    public:
      virtual ~ActivityLogProvider() = default;

      /// Number of records currently held.
      virtual uint16_t RecordCount() const = 0;

      /// Oldest and newest timestamps held, both 0 when there are no records.
      virtual uint32_t OldestTimestamp() const = 0;
      virtual uint32_t NewestTimestamp() const = 0;

      /// Copies up to maxRecords records strictly newer than sinceTimestamp into out, oldest
      /// first, and returns how many were written. Fewer than maxRecords means the caller has
      /// reached the end of what is stored.
      virtual uint8_t ReadRecords(uint32_t sinceTimestamp, ActivityRecord* out, uint8_t maxRecords) const = 0;

      /// The host states it has durably stored everything up to and including upToTimestamp,
      /// so the space may be reclaimed.
      ///
      /// This exists because the watch otherwise has no way to know whether anything kept what
      /// it sent, which is how activity logs end up growing until the flash is full.
      virtual void Release(uint32_t upToTimestamp) = 0;
    };
  }
}
