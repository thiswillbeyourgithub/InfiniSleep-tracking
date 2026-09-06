// Host harness for Ppg, the photoplethysmograph that turns HRS3300 samples into a heart rate.
//
// Exists because the only other way to time a measurement is to put the watch on a wrist and count
// seconds, and because the window arithmetic (how many samples buy how much frequency resolution,
// and how long that makes the wearer wait) is exactly the kind of thing a synthetic pulse can pin
// down and a wrist cannot.
//
// Ppg needs no stubs: it depends on arduinoFFT, which is a header, and on nothing else.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "components/heartrate/Ppg.h"

using namespace Pinetime::Controllers;

static int failures = 0;

#define CHECK(cond)                                                                                                                        \
  do {                                                                                                                                     \
    if (!(cond)) {                                                                                                                         \
      printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                              \
      failures++;                                                                                                                          \
    }                                                                                                                                      \
  } while (0)

namespace {
  constexpr float sampleRate = 1000.0f / Ppg::deltaTms;
  constexpr float pi = 3.14159265358979f;

  struct Reading {
    int sample;
    int bpm;
  };

  struct Run {
    std::vector<Reading> readings;

    bool Any() const {
      return !readings.empty();
    }

    // Seconds from the first sample to the first heart rate the wearer is shown.
    float FirstAt() const {
      return readings.empty() ? -1.0f : static_cast<float>(readings.front().sample + 1) / sampleRate;
    }

    int First() const {
      return readings.empty() ? 0 : readings.front().bpm;
    }

    int Last() const {
      return readings.empty() ? 0 : readings.back().bpm;
    }

    // Worst reading of the run, which is what a wearer watching the screen would notice.
    int WorstError(float bpm) const {
      int worst = 0;
      for (const Reading& reading : readings) {
        const int error = std::abs(reading.bpm - static_cast<int>(bpm));
        if (error > worst) {
          worst = error;
        }
      }
      return worst;
    }
  };

  // A pulse the sensor could plausibly see: a fundamental at the heart rate, a second harmonic for
  // the dicrotic notch, a slow baseline for breathing, white noise, all on the DC level the LED
  // returns. Amplitudes are in ADC counts, the units Hrs3300::ReadHrsAls hands over.
  //
  // Fed the way HeartRateTask feeds the sensor: one sample every deltaTms, then whatever
  // HeartRate() has to say about it. The ambient light and reset handling is copied from
  // HeartRateTask::Work, so a reading here is a reading on the watch.
  Run Measure(float bpm, float seconds, float amplitude = 200.0f, float noise = 10.0f, float drift = 0.0f, float harmonic = 0.25f) {
    Ppg ppg;
    Run run;
    uint32_t seed = 12345;
    const int samples = static_cast<int>(seconds * sampleRate);
    for (int idx = 0; idx < samples; idx++) {
      const float t = static_cast<float>(idx) / sampleRate;
      float value = 20000.0f + amplitude * sinf(2.0f * pi * (bpm / 60.0f) * t) +
                    harmonic * amplitude * sinf(4.0f * pi * (bpm / 60.0f) * t + 0.7f) + drift * sinf(2.0f * pi * 0.25f * t);
      if (noise > 0.0f) {
        seed = seed * 1103515245u + 12345u;
        value += noise * ((static_cast<float>((seed >> 16) & 0x7fff) / 16383.5f) - 1.0f);
      }
      const int8_t ambient = ppg.Preprocess(static_cast<uint16_t>(value), 100);
      int reading = ppg.HeartRate();
      if (ambient > 0) {
        ppg.Reset(true);
        reading = 0;
      } else if (reading < 0) {
        ppg.Reset(false);
        reading = 0;
      }
      if (reading > 0) {
        run.readings.push_back({idx, reading});
      }
    }
    return run;
  }

  // What the wearer should not have to wait longer than, in seconds.
  constexpr float latencyLimit = 7.0f;
}

int main() {
  printf("Ppg: %d samples a window at %.0f Hz, %.3f Hz a bin, %.1f bpm a bin\n",
         Ppg::dataLength,
         sampleRate,
         sampleRate / static_cast<float>(Ppg::dataLength),
         sampleRate / static_cast<float>(Ppg::dataLength) * 60.0f);

  // How strong the pulse is must not decide whether it is found at all: every magnitude in the
  // spectrum scales with it, so any threshold compared against one has to scale too.
  printf("accuracy and latency against pulse strength\n");
  for (float amplitude : {20.0f, 60.0f, 200.0f, 600.0f, 2000.0f}) {
    for (float bpm : {50.0f, 60.0f, 72.0f, 90.0f, 120.0f, 160.0f}) {
      Run run = Measure(bpm, 20.0f, amplitude);
      printf("  %6.0f counts %5.0f bpm: first %5.2f s = %3d bpm, at 20 s = %3d bpm, worst error %2d, %2zu readings\n",
             amplitude,
             bpm,
             run.FirstAt(),
             run.First(),
             run.Last(),
             run.WorstError(bpm),
             run.readings.size());
      CHECK(run.Any());
      CHECK(run.FirstAt() > 0.0f && run.FirstAt() <= latencyLimit);
      CHECK(run.WorstError(bpm) <= 3);
    }
  }

  printf("robustness to noise and a breathing baseline\n");
  for (float noise : {0.0f, 10.0f, 40.0f}) {
    for (float drift : {0.0f, 400.0f}) {
      for (float bpm : {50.0f, 72.0f, 120.0f}) {
        Run run = Measure(bpm, 20.0f, 200.0f, noise, drift);
        printf("  noise %4.0f drift %4.0f %5.0f bpm: first %5.2f s, at 20 s = %3d bpm, worst error %2d\n",
               noise,
               drift,
               bpm,
               run.FirstAt(),
               run.Last(),
               run.WorstError(bpm));
        CHECK(run.Any());
        CHECK(run.FirstAt() > 0.0f && run.FirstAt() <= latencyLimit);
        CHECK(run.WorstError(bpm) <= 3);
      }
    }
  }

  // Nothing on the wrist means no reading, however loud the noise. A watch that invents a number
  // here logs it as a heart rate, which is worse than showing nothing.
  printf("no pulse\n");
  for (float noise : {10.0f, 40.0f, 200.0f}) {
    Run run = Measure(72.0f, 20.0f, 0.0f, noise);
    printf("  noise only %4.0f: %2zu readings\n", noise, run.readings.size());
    CHECK(!run.Any());
  }
  {
    Run run = Measure(72.0f, 20.0f, 0.0f, 0.0f);
    printf("  flat signal: %2zu readings\n", run.readings.size());
    CHECK(!run.Any());
  }

  printf("%s\n", failures == 0 ? "PASS" : "FAILED");
  return failures == 0 ? 0 : 1;
}
