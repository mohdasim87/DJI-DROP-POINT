# DropPoint

A payload-drop guidance system for DJI enterprise multirotors, built on the
DJI Payload SDK (PSDK). The operator flies normally in DJI Pilot 2 and
designates where a payload should land; DropPoint continuously computes
where the payload would land if released right now — accounting for wind,
wind gradient, air density, drag, aircraft velocity, and system latency —
and either guides the operator to the release moment (manual mode) or
releases at the computed instant itself (auto mode, gated and abortable).

Built for search-and-rescue and emergency supply delivery: medical packs,
water, radios, flotation.

**Status: not flight-verified.** The guidance logic is unit tested and flown
in simulation; the hardware binary builds and links against DJI's Payload SDK
3.16.0 on Linux, and the ARM64 packages are installed, upgraded and removed
on a clean machine in CI. Nothing has run on an aircraft — see
`docs/OPEN-QUESTIONS.md` for what only hardware can settle.

**Supported aircraft profiles:** DJI Matrice 4T (`config/profiles/m4t`) and
DJI Matrice 400 (`config/profiles/m400`). Start with
`docs/DropPoint-How-To.pdf`, then `docs/aircraft/M4T.md` or
`docs/aircraft/M400.md`. Using a CZI drop kit: `docs/CZI-DROP-KITS.md`.

## Quick start (no hardware required)

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/src/app/Release/droppoint_sim_main --sim crosswind-5ms
```

See `docs/SETUP.md` for full prerequisites and every build target.

## Repository layout

```
droppoint/
  CMakeLists.txt   README.md
  config/          aircraft.yaml  payloads.yaml  lenses.yaml  system.yaml
  src/
    core/          pure C++17, zero DJI/platform dependencies, fully unit-tested
    hal/           i_telemetry.h  i_gimbal.h  i_rangefinder.h  i_display.h
                   i_audio.h  i_release.h  i_liveview.h  i_target_source.h
    psdk/          the only place DJI headers appear (not built by default)
    sim/           simulator implementation of every HAL interface
    app/           mission_state.*  guidance_loop.*  release_manager.*  main.cpp
    util/          ring_buffer.h  timesync.*  logging.*  config.*  drop_log.*
  tools/           calibrate/  replay/  lenscal/  dashboard/
  data/dem/        offline DEM tiles (empty in this repo — vendor per site)
  tests/           core/ unit + regression + property tests
  docs/            see below
  scripts/         install.sh, rollback.sh, systemd unit, Jetson USB setup
```

## Documentation

| Doc | What's in it |
|---|---|
| `docs/DropPoint-How-To.pdf` | Illustrated how-to: which package for which drone, install, self-test, simulator, operator controls |
| `docs/aircraft/M4T.md`, `docs/aircraft/M400.md` | Per-aircraft hardware, weight budget, install and flying steps |
| `docs/DROP-KITS.md` | Which drop-kit model to buy for each aircraft (JZ, CZI, DroMight, Cybowindow) and the `release.kit` id for each |
| `docs/CZI-DROP-KITS.md` | Using CZI TH2 / FT4-P / TH4 drop kits and what DropPoint can and cannot trigger |
| `docs/PSDK-RESEARCH.md` | Phase 0 findings from the real Payload-SDK headers/samples |
| `docs/OPEN-QUESTIONS.md` | Everything Phase 0 couldn't resolve, with the exact question a human answers on hardware — **start here** |
| `docs/ARCHITECTURE.md` | The core/hal/sim/psdk/app boundary and why it's shaped this way |
| `docs/CONNECTION.md` | Wiring diagrams, step-by-step connection procedure, troubleshooting table |
| `docs/SETUP.md` | Toolchain to a running system |
| `docs/HARDWARE.md` | Parts, power budget, mounting |
| `docs/OPERATION.md` | Flight procedure checklist, every hold reason and its remedy, quick-reference card |
| `docs/CALIBRATION.md` | Ballistic drops, lens calibration, video-latency sweep |
| `docs/DEPLOYMENT.md` | Packaging, install/upgrade/rollback — and why there is no APK |

Every document is marked `Status: verified` or `Status: unverified —
requires hardware` per section, so nothing untested reads as proven.

## The one rule that matters most

`src/core/` never includes a DJI header, a platform header, or performs
I/O. That's what makes the ballistics, geodesy, and designation math
(`core/ballistics.*`, `core/raycast.*`, `core/projection.*`,
`core/terrain.*`, `core/gating.*`) fully unit-testable with no aircraft —
see `tests/`, including all 11 regression vectors and the timestamp-
alignment test from the build spec's section 6.5/14.

## Safety

Auto release is gated, armed, single-shot, and abortable (spec section 8):
it never fires without a latched, confirmed, fresh target and every hard
gate passing, it re-checks those gates at loop rate through the entire
countdown, a hardware abort overrides it unconditionally from any state,
and the release output defaults to its safe state on startup, crash,
watchdog timeout, disarm, and shutdown. See `docs/OPERATION.md` and
`src/app/release_manager.h`.
