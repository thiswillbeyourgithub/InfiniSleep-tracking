# InfiniSleep tracking

A personal fork of [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime), the open source firmware for the [PineTime](https://pine64.org/devices/pinetime/), with one goal: make the watch record sleep on its own and hand the recording to a companion app afterwards.

The work lives on the `infinisleep-health` branch. A [Logging app](#logging-what-happens-through-the-day) for whatever else the wearer wants recorded through the day, and a [Pomodoro app](#pomodoro-app) ported from wasp-os, ride along, both unrelated to the sleep work and described further down. The other half is [Gadgetbridge-infinisleep-tracking](https://codeberg.org/thiswillbeyourgithub/Gadgetbridge-infinisleep-tracking), the companion app that collects what this firmware records. Neither half is useful without the other.

## Ready to install, no toolchain needed

**Built firmware is published on the [Releases page](../../releases).** Download the `pinetime-mcuboot-app-dfu-*.zip` from the newest release and flash it straight to the watch. There is nothing to compile and no docker image to pull.

- **From the phone**, open the zip with Gadgetbridge, which knows how to send a PineTime DFU package. Stock Gadgetbridge does this, not only the companion fork.
- **From a computer**, `python dfu.py -z pinetime-mcuboot-app-dfu-<version>.zip -a <MAC> --legacy`, using the `dfu.py` from the InfiniTime repository. The one from wasp-os fails with a UUID error.

Those files are **compiled by me, on my own machine**, from the commit each release names. No CI produced them, they are not signed, and nothing about them is reproducible beyond the fact that the source they came from is right here. If you would rather not take my word for it, the build instructions below produce the same thing from the same commit.

Updating from an earlier build of this fork **resets every watch setting once**: watch face, brightness, wake modes, steps goal and heart rate interval all go back to their defaults. The settings file carries a version, this release changed it, and a file whose version does not match is discarded rather than misread.

Flashing an interrupted DFU leaves the watch in its bootloader and it can be flashed again, so this is recoverable, but it is still firmware. Read the upstream flashing notes if it is your first time.

## Why

A stock PineTime can only report what it measures at the moment something is listening. Sleep happens exactly when the phone is least likely to be connected, so sleep charts stay empty no matter what the watch measured. This fork gives the watch a small log of its own and a way to hand it over later.

It builds on two upstream efforts, neither of which is merged:

- [PR 2174, InfiniSleep: SleepTk Port](https://github.com/InfiniTimeOrg/InfiniTime/pull/2174) by cyberneel, which this fork is based on. It adds the Sleep app, the sleep cycle goal and the gradual wake alarm.
- [PR 2304, Add Sleeptracking](https://github.com/InfiniTimeOrg/InfiniTime/pull/2304) by sillydan1, a smaller independent take on the same problem, which logs to a file on the watch.

## What this fork adds

- A heart rate reading taken at each tracker epoch, rather than a setting that promised one and did nothing.
- An actigraphy count accumulated from the accelerometer.
- A ring of records (timestamp, motion, heart rate, kind), mirrored to `/.system/activity.dat` so it survives a reboot. Stored packed, at 3 bytes a record on a watch that measures no motion and 5 where it does, which is 682 records or 409: about 7 nights at a 5 minute epoch, or 21 at 15 minutes. The log starts narrow and widens for good on the first record that carries motion, so a watch whose accelerometer never answers does not spend a third of its log saying so. Nothing is quantised, and the format on the wire is the same either way.
- A BLE service, `00060000-78fc-48fe-8e23-433b3a1942d0`, that hands the records over in batches and only reclaims the space once the host says it stored them.
- Both sampling rates as settings, on a new Sensors page in the Sleep app.
- Heart rate measured on a timer outside any sleep session, every 5, 15, 30 or 60 minutes, set from a new entry in the settings menu and off by default. It stands down while the sleep tracker runs, and with the screen on it records whatever the heart rate app is measuring rather than starting a measurement of its own, so it never takes the sensor from the wearer. A measurement left running in the app also survives the night now: the poll wakes the sensor for its reading and hands it back instead of ending it.
- Every settled reading a manual heart rate check produces logged as well, marked awake, so the one measurement the wearer actually asked for is collected like the rest instead of only appearing on screen.
- A heart rate in about three seconds rather than six and a half, estimated off half a sample window and refined as full windows arrive, with the spread shown under the number in the heart rate app until the windows agree on it. Upstream cannot say anything at all before a full window is in.
- One upstream bug behind part of that wait, and behind measurements that never converged at all: the check that rejects a window left with a baseline residual compared it against a fixed number, while every magnitude in the spectrum scales with how strong the pulse is. A pulse above roughly 60 ADC counts was therefore thrown away at every heart rate, however long the wearer waited. Both the fix and the early estimate are measured on the host against a synthetic pulse rather than on a wrist with a stopwatch.
- Watch faces that show only figures they can stand behind: `?` rather than a heart rate of `0` while the sensor has not converged or sees no skin, and no step count at all when nothing is counting steps.
- Awake time told apart from sleep within a session. A button press or a wake gesture during the night marks the next 15 minutes awake, and the 5 minutes before it, since waking is not instantaneous. Two of them within half an hour mark the whole stretch between. A wrist raise deliberately does not count, because rolling over triggers it.
- A marks page, reachable by swiping up from the tracking page while a session runs, with a **Not asleep yet** button. It rewrites everything since the session started as awake, for the night that begins with an hour of reading in bed.
- Sessions under five minutes discarded rather than handed over, and one awake record written at each end of a session, so a companion app charts the stretch that was tracked instead of everything back to the previous sample.
- A backoff below 25 percent battery: the epoch floors at 30 minutes and the accelerometer poll at 1 second, so the tracker does not flatten the battery before morning, and the log is written to flash on the way past that threshold so a battery that dies at 4am does not take the night with it.

The BLE service is deliberately kept independent of how sleep is tracked, so it could be reviewed on its own.

## Motion tracking is untested, because this watch's accelerometer is broken

The BMA accelerometer in the unit this was written on never answers on I2C. It reads back a chip id of `0x00`, the About screen shows `Accel. ??? 00/2`, and the same bus reads the touch panel's ids correctly, so the bus is fine and the chip is not. Draining the battery flat did not clear it either, which rules out a latched state.

The motion code is written and shipped anyway, for watches whose sensor works, but it is **off by default** and has to be turned on from the Sensors page in the Sleep app. Until then, records report `0xFFFF`, meaning not measured. The firmware also refuses to wake the accelerometer overnight when no sensor answered at boot, so turning the setting on costs nothing on a watch like this one.

The step count the watch faces showed was a permanent `0` for the same reason, so it is hidden when no accelerometer answered at boot, and the Steps page in the settings menu carries a `Steps: on / off` toggle for a wearer with a working sensor who would rather not see one. On a watch like this one that page reads `Steps: no sensor` and does not pretend to toggle anything.

**Anyone reading this should treat the motion side as unverified on hardware.** Heart rate, the log, the BLE transfer and the settings do work.

## Companion app

[Gadgetbridge-infinisleep-tracking](https://codeberg.org/thiswillbeyourgithub/Gadgetbridge-infinisleep-tracking), branch `infinisleep`, is the companion app for this firmware. Stock Gadgetbridge does not know about this service and will simply ignore it, so the log stays on the watch until it is overwritten.

There is no need to uninstall the Gadgetbridge you already have. The fork builds under its own application id, so the two sit side by side on the phone and keep separate databases:

```bash
./gradlew assembleMainlineNopebble
adb install -r app/build/outputs/apk/mainline/nopebble/*.apk
```

## Pomodoro app

A port of the [Pomodoro application](https://wasp-os.readthedocs.io/en/latest/apps.html#pomodoro-application) from wasp-os, written by the same author as [SleepTk](https://github.com/thiswillbeyourgithub/SleepTk_pinetime_sleep_tracker), the wasp-os sleep tracker that the InfiniSleep work above ultimately descends from. Nothing in it touches sleep tracking. It is simply an app this watch was missing.

It runs a queue of timers that chain into each other. Type `25,5` and the watch counts down 25 minutes, vibrates, counts down 5, vibrates, and starts over, until it is stopped or 99 rounds have passed. The Timer app in stock InfiniTime runs one countdown once.

- Queues are typed on a keypad. `Then` chains one interval to the next, `Go` starts the queue.
- Swipe left and right for presets, up and down for how many times each alert vibrates, once a second.
- Alerts are randomised above three vibrations: sometimes one long buzz of varying strength, sometimes a burst of short random pulses. A pattern that changes every second is much harder to tune out than the same pulse over and over.
- `+1` pushes the current interval one minute further out without touching the queue.
- It keeps running in the background. Leave the app and the watch wakes itself and brings the app back when an interval elapses.
- The queue and the vibration count are saved, so they survive a reboot.

Two controls differ from wasp-os, both forced by the firmware. Swiping down changes the vibration count instead of leaving the app, so the side button is the way out of the keypad; and the side button skips an alert, which wasp-os gave no way to do short of waiting it out. The app icon is the clock glyph rather than the wasp-os tomato, because the icon font has no tomato in it.

## Timer and stopwatch

Both stock apps were changed so that leaving them does not throw away what they were doing.

The timer used to buzz once, for 35 milliseconds, when it ran out. From a pocket that was easy to miss, which defeats the point of setting one. It now rings the way the alarm does, a pulse every second, until someone presses Stop or the side button, and gives up after a minute if nobody answers. The countdown itself always ran in the background: it is a FreeRTOS timer, and the watch wakes itself and brings the app back when it expires.

The stopwatch kept its state inside the screen, and the screen is deleted as soon as anything else is shown, so glancing at the watch face during a run lost it. Upstream fixed this by moving the state into a controller that outlives the screen, and that work is taken from upstream unchanged rather than rewritten, so nothing here has to be reconciled when this fork eventually merges with it. Laps, pauses and the elapsed time all survive leaving the app.

## Logging what happens through the day

Sleep is not the only thing worth recording, and the watch is the only thing already on the wrist when something happens. A **Logging app** keeps a log of whatever the wearer decided is worth a tap: medication taken, a nap, feeling sick, a pain score.

Nothing about what can be logged is compiled in. The watch holds a table of slots the [companion app](#companion-app) pushes to it, and the app's menu is built out of that table at runtime, so a new thing to log costs an edit on the phone rather than a firmware build. The watch stores numbers and never names. A slot is what the phone calls an **event type**, and this side is where the names on the wire live.

- Up to 32 slots, each with a number the watch logs, a label of 15 bytes and a kind: **punctual**, **continuous** (a start and a later stop), a **rating** from 0 to 10, or a **group** of other slots. Groups go two levels deep at most, and a table whose top level holds a single group opens inside it, because a screen with one button on it is a step for nothing.
- A rating is picked on a slider rather than with plus and minus buttons. Ten taps to say ten is a different act from one tap to say one, and a rating entered that way carries how awkward it was to enter.
- A continuous slot is two independent events, a start and a stop, against the same slot. **The watch never pairs them**: a reboot or a flat battery during a nap would leave it holding a session it can never close. It does show whether such a slot is running, read back from the events it still holds, and losing that to a reboot costs nothing because the phone does the pairing.
- Logging is one tap, and the flag comes after, on the confirmation that names what was just written. A flag is wanted on maybe one event in ten, mostly to say the time needs fixing, so it is not worth a question in front of every log.
- **There is no time picker on the watch.** An event is logged at the moment it happens, and something noticed an hour late is flagged here and given its real time on the phone, which is one screen instead of a row of offsets nobody would reach for in the dark.
- Events are 10 bytes each in a ring of their own, mirrored to `/.system/eventlog.dat`, released only once the phone says it stored them, and **released by sequence number rather than by time**, because two events can land in the same minute, which the activity log's ordering rule forbids and this one has to allow.
- The ring, the file mirror and the release are the activity log's, parameterised and shared rather than copied. The BLE service `00070000-78fc-48fe-8e23-433b3a1942d0` hands events out and takes the slot table in, the table arriving as one transaction the watch accepts or refuses whole, since half a table is worse than none.
- The table lives in `/.system/logslots.dat` with the revision the phone gave it, reported back in the status, so the phone can tell whether the wrist is showing what it last sent.

The event log, the slot table and their validation are tested on the host, in `tests/eventlog/` and `tests/logslots/`.

## Apps left out

Paint, Paddle, Twos, Dice and the Metronome are not built into this firmware. They cost flash that the work above needed and none of them were being used. Navigation and Motion are left out upstream already, so nothing else is missing.

The list is the commented block at the top of [src/displayapp/apps/CMakeLists.txt](src/displayapp/apps/CMakeLists.txt), one line per app. Note that `USERAPP_TYPES` is a cmake cache variable: a stale `build/CMakeCache.txt` keeps the old list, so changing which apps are built needs `build/` deleted first, not just a rebuild. Invoking cmake directly also accepts `-DENABLE_USERAPPS=...` to replace the whole list, though the docker image offers no way to pass it through.

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
                               # both record layouts, the widening, and the delta span
tests/eventlog/run.sh          # the event log: numbering, the release by sequence, reboots
tests/logslots/run.sh          # the slot table: what a phone may push and what is refused
tests/heartrate/run.sh         # heart rate latency and accuracy, likewise, on a synthetic pulse
tests/pomodoro/run.sh          # the pomodoro state machine, stepped through an hour in microseconds
tests/stopwatch/run.sh         # the stopwatch, including the thousand hours it takes to wrap round
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
- 9a571a29 fix(infinisleep): disable the wake alarm when the tracker is stopped
- 574002b3 chore: drop the Metronome app
- 5aaaebf8 feat(sleep): show the activity log record count on the Info page
- f5f2b860 fix(activity): stop publishing self started measurements as live heart rate
- ca0976f6 feat(sleep): add a log page showing what is waiting to be collected
- 5839e9ca feat(activity): record when a host last asked for records
- 8baf09cd feat(activity): record the quarter hour after a look at the watch as awake
- 1f0cfe37 feat(activity): let the log take records back from the newest end
- e23dd77a feat(infinisleep): discard a session that lasted under five minutes
- 59267b6d feat(activity): bound a session with an awake record at each end
- 1f3666c1 feat(activity): let the log rewrite the kind of records it already holds
- 4f745214 feat(activity): mark the minutes before a look at the watch as awake too
- 5a1501d0 feat(infinisleep): add a marks page with "Not asleep yet"
- fb4f7c8e feat(settings): let the wearer turn steps off, and hide them with no sensor
- 8464a0ff fix(watchface): show "?" rather than 0 for a heart rate that has no reading
- 30ab9f09 fix(activity): stop a background poll from killing a manual measurement
- 0765c1d6 feat(activity): log the readings a manual heart rate check produces
- 43d0f260 chore(apps): put the sleep app before steps in the app list
- 44fc01be fix(heartrate): compare the DC residual to the peak, not to a fixed number
- e71ea2ad perf(heartrate): show a coarse reading off half a window, then refine it
- ddc31cd5 feat(heartrate): show the reading as a range until it settles
- 2cffa95c test: share the host stubs between harnesses, and fake FreeRTOS timers
- 55463e76 feat(pomodoro): add the controller behind a chained timer queue
- bad42e93 feat(pomodoro): add the app screen and wire it into the launcher
- 738499c3 feat(timer): ring until told to stop instead of buzzing once
- c08e7f19 feat(stopwatch): keep a run going after the app is left
- 7d365dd5 feat(activity): stop spending two bytes a record saying motion was not measured
- 87a28801 refactor(log): share the ring, the file mirror and the release
- 8ea4dcd2 feat(log): keep the events the wearer logs, numbered so a host can acknowledge them
- 8f5f2b6c feat(log): hold the table of slots the phone pushes, and refuse one that does not hold together
- d8bbef7f feat(log): hand the event log and the slot table over BLE
- f560f784 feat(log): let an event be flagged after it was logged
- 40147a8d refactor(datetime): one place that turns the clock into epoch seconds
- 9c3524e7 feat(logging): a watch app built out of the table the phone pushed

Plus the commits that write this list, which cannot list their own hash, and the odd formatting or gitignore commit not worth a line.

## Upstream

Everything else, including getting started, the app and watch face lists, the code walkthrough and the flashing guides, is unchanged and documented in the [upstream README](https://github.com/InfiniTimeOrg/InfiniTime#readme) and under [doc/](doc/).

## Licenses

Unchanged from upstream: GNU General Public License version 3 or, at your option, any later version. It integrates [FreeRTOS](https://freertos.org) (MIT), [LVGL](https://lvgl.io/) (MIT), [NimBLE](https://github.com/apache/mynewt-nimble) (Apache 2.0) and [Jetbrains Mono](https://www.jetbrains.com/lp/mono/) (Apache 2.0). Credit for InfiniTime itself belongs upstream.

---

The work in this fork was done with [Claude Code](https://claude.com/claude-code).
