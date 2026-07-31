# InfiniSleep tracking

A personal fork of [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime), the open source firmware for the [PineTime](https://pine64.org/devices/pinetime/), with one goal: make the watch record sleep on its own and hand the recording to a companion app afterwards.

The work lives on the `infinisleep-health` branch. The other half is [Gadgetbridge-infinisleep-tracking](https://codeberg.org/thiswillbeyourgithub/Gadgetbridge-infinisleep-tracking), the companion app that collects what this firmware records. Neither half is useful without the other.

## Why

A stock PineTime can only report what it measures at the moment something is listening. Sleep happens exactly when the phone is least likely to be connected, so sleep charts stay empty no matter what the watch measured. This fork gives the watch a small log of its own and a way to hand it over later.

It builds on two upstream efforts, neither of which is merged:

- [PR 2174, InfiniSleep: SleepTk Port](https://github.com/InfiniTimeOrg/InfiniTime/pull/2174) by cyberneel, which this fork is based on. It adds the Sleep app, the sleep cycle goal and the gradual wake alarm.
- [PR 2304, Add Sleeptracking](https://github.com/InfiniTimeOrg/InfiniTime/pull/2304) by sillydan1, a smaller independent take on the same problem, which logs to a file on the watch.

## What this fork adds

- A heart rate reading taken at each tracker epoch, rather than a setting that promised one and did nothing.
- An actigraphy count accumulated from the accelerometer.
- A ring of 341 records (timestamp, motion, heart rate, kind), stored packed at 6 bytes each and mirrored to `/.system/activity.dat` so it survives a reboot. About three and a half nights at a 5 minute epoch.
- A BLE service, `00060000-78fc-48fe-8e23-433b3a1942d0`, that hands the records over in batches and only reclaims the space once the host says it stored them.
- Both sampling rates as settings, on a new Sensors page in the Sleep app.
- Heart rate measured on a timer outside any sleep session, every 5, 15, 30 or 60 minutes, set from a new entry in the settings menu and off by default. It stands down while the sleep tracker runs and only measures with the screen off, so it never takes the sensor from the heart rate app.
- A backoff below 25 percent battery: the epoch floors at 30 minutes and the accelerometer poll at 1 second, so the tracker does not flatten the battery before morning, and the log is written to flash on the way past that threshold so a battery that dies at 4am does not take the night with it.

The BLE service is deliberately kept independent of how sleep is tracked, so it could be reviewed on its own.

## Motion tracking is untested, because this watch's accelerometer is broken

The BMA accelerometer in the unit this was written on never answers on I2C. It reads back a chip id of `0x00`, the About screen shows `Accel. ??? 00/2`, and the same bus reads the touch panel's ids correctly, so the bus is fine and the chip is not. Draining the battery flat did not clear it either, which rules out a latched state.

The motion code is written and shipped anyway, for watches whose sensor works, but it is **off by default** and has to be turned on from the Sensors page in the Sleep app. Until then, records report `0xFFFF`, meaning not measured. The firmware also refuses to wake the accelerometer overnight when no sensor answered at boot, so turning the setting on costs nothing on a watch like this one.

**Anyone reading this should treat the motion side as unverified on hardware.** Heart rate, the log, the BLE transfer and the settings do work.

## Companion app

[Gadgetbridge-infinisleep-tracking](https://codeberg.org/thiswillbeyourgithub/Gadgetbridge-infinisleep-tracking), branch `infinisleep`, is the companion app for this firmware. Stock Gadgetbridge does not know about this service and will simply ignore it, so the log stays on the watch until it is overwritten.

There is no need to uninstall the Gadgetbridge you already have. The fork builds under its own application id, so the two sit side by side on the phone and keep separate databases:

```bash
./gradlew assembleMainlineNopebble
adb install -r app/build/outputs/apk/mainline/nopebble/*.apk
```

## Building

Unchanged from upstream, so the docker route from [doc/buildWithDocker.md](doc/buildWithDocker.md) works as it does there:

```bash
git submodule update --init
docker build -t infinitime-build ./docker            # once, and it takes a while
docker run --rm -it -v ${PWD}:/sources --user $(id -u):$(id -g) infinitime-build
```

The flashable file lands in `build/output/pinetime-mcuboot-app-dfu-<version>.zip`. Send it over BLE with the `dfu.py` from this repository, not the one from wasp-os, which fails with a UUID error:

```bash
python dfu.py -z build/output/pinetime-mcuboot-app-dfu-<version>.zip -a <MAC> --legacy
```

Gadgetbridge can install the same zip from its firmware update screen.

One warning specific to this tree: an incremental build can miss a header change and silently link object files that disagree about the layout of a class, which corrupts RAM at runtime rather than failing to build. Delete `build/` when a header changed.

```bash
tests/activity/run.sh          # activity log tests, on the host: no hardware, no docker
```

## Commits in this fork

Oldest first, as conventional commits. The history was squashed into one commit per subject, so bug fixes are folded into whatever introduced them.

- cfc4ec5f feat(motion): report why the accelerometer failed to initialise, on the About screen
- 0f0ad621 feat(activity): keep a log of recorded activity and serve it over BLE, with host tests
- 2dcd595e feat(infinisleep): write one activity record per tracker epoch, with heart rate, motion and a low battery backoff
- 8418c4ff feat(infinisleep): rework the sleep pages and add a Sensors page
- 971d979b chore: drop four games for flash, and ignore the local build helpers
- e31ae1b4 feat(infinisleep): put the sensor settings on top of the sleep pages
- 3a7fd504 fix(infinisleep): make the Auto button set the wake up time and nothing else
- ae0fc430 feat(settings): add a heart rate polling interval to the settings menu
- 67d2cbdf feat(activity): measure heart rate on a timer outside sleep sessions

Plus the commits that write this list, which cannot list their own hash.

## Upstream

Everything else, including getting started, the app and watch face lists, the code walkthrough and the flashing guides, is unchanged and documented in the [upstream README](https://github.com/InfiniTimeOrg/InfiniTime#readme) and under [doc/](doc/).

## Licenses

Unchanged from upstream: GNU General Public License version 3 or, at your option, any later version. It integrates [FreeRTOS](https://freertos.org) (MIT), [LVGL](https://lvgl.io/) (MIT), [NimBLE](https://github.com/apache/mynewt-nimble) (Apache 2.0) and [Jetbrains Mono](https://www.jetbrains.com/lp/mono/) (Apache 2.0). Credit for InfiniTime itself belongs upstream.

---

The work in this fork was done with [Claude Code](https://claude.com/claude-code).
