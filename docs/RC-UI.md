# DropPoint on the DJI RC / Pilot 2 screen

**Status: unverified — requires hardware.** This page predates the widget
implementation and is partly out of date: where it says widgets are not yet
registered, or that RC switches must be polled, that is no longer true. The
current operator controls, the ten widgets and their RC button mapping are in
`docs/DropPoint-How-To.pdf` (section 8) and the aircraft guides under
`docs/aircraft/`.

## What the pilot sees

DropPoint does **not** replace DJI Pilot 2 and does **not** install anything on
the RC controller. It runs on the companion computer and publishes:

- a **floating text window** in the liveview (distance, miss, gates, state)
- an optional **widget panel** with buttons / release mode selector
- audio cues through the companion-computer speaker / PSDK speaker route
- optional liveview AI-marker overlay (Manifold 3 only)

All of this is rendered by DJI Pilot 2 from data sent over the normal aircraft
radio link.

## ASCII mockup of the RC/tablet screen

```
+----------------------------------------------------------+
|  DJI Pilot 2 liveview                                    |
|                                                          |
|     +------------------------------------------+         |
|     |  DropPoint floating window               |         |
|     |  [COUNTDOWN]  T-3  miss: 2.1 m           |         |
|     |  RNG 127 m  BRG 194   AGL 48 m           |         |
|     |  [OK] LRF elevation                      |         |
|     |  [HOLD] crosswind 6.2 m/s > limit          |         |
|     +------------------------------------------+         |
|                                                          |
|                    [video feed]                          |
|                                                          |
+----------------------------------------------------------+
|  DropPoint widget panel (optional)                       |
|  [SET TARGET] [CONFIRM] [ARM] [RELEASE] [ABORT]          |
|  Mode: ( ) Manual  (*) Auto                              |
|  Payload: [water_5l_____________]  [Calibrate]           |
+----------------------------------------------------------+
```

In practice the floating window sits in a corner of the liveview and the
widget panel lives in a Pilot side panel; the exact position is determined
by the DJI widget config files, not by this code.

## State-by-state screen content

| State | Floating window | Widget buttons | Audio |
|---|---|---|---|
| **IDLE** | `NO TARGET - AIM AND PRESS SET TARGET` | SET TARGET active | silence |
| **TARGET LATCHED** | distance/bearing/elevation, source (DEM/LRF) | CONFIRM active | short beep |
| **CONFIRMED** | `TARGET CONFIRMED - PRESS ARM` | ARM active | silence |
| **ARMED** | `ARMED - APPROACHING RELEASE CORRIDOR` | RELEASE active (Manual) or disabled (Auto) | silence |
| **COUNTDOWN** | `T-3 ... T-2 ... T-1 ... DROP` | ABORT active | tone at T-5, then "three, two, one, drop" |
| **RELEASED** | `RELEASED - RETURN TO IDLE` | all inactive | "released" |
| **ABORT** | `ABORT - <reason>` | SET TARGET active | "abort" |

## What is implemented today

- Floating-window text via `DjiWidgetFloatingWindow_ShowMessage`.
- Audio countdown hook via `hal::IAudio`/`psdk::AudioSpeaker`; the PSDK speaker
  handler is registered, but the actual audio output path needs confirmation
  on real hardware (see `docs/OPEN-QUESTIONS.md` #10).
- Liveview marker packet format via `DjiLiveview_SendAiMetaToPilot` (Manifold 3
  only).

## What still needs hardware-specific setup

- **Widget buttons** (`SET TARGET`, `CONFIRM`, `ARM`, `RELEASE`, `ABORT`) are
  not yet registered because DropPoint needs the DJI widget configuration binary
  files for the target screen size and language. See
  `docs/OPEN-QUESTIONS.md` #11. Until then the functions can be driven from
  mapped RC switch channels (docs/OPEN-QUESTIONS.md #3) or from a ground
  station/keyboard via a separate TCP/UDP bridge if one is added.
- **RC switch mapping** for ARM/RELEASE/ABORT must be configured by the
  operator in DJI Pilot's RC settings and recorded in `config/aircraft.yaml`.

## How to make it work end-to-end

1. **Build the real binary on a Linux/aarch64 machine**
   ```bash
   cmake -S droppoint -B build-aarch64 \
         -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
         -DDROPPOINT_BUILD_PSDK=ON \
         -DCMAKE_BUILD_TYPE=Release
   cmake --build build-aarch64 --target droppoint
   ```

2. **Prepare the companion computer**
   - Install Ubuntu/Linux on the Manifold 3 / Jetson / Raspberry Pi.
   - Wire aircraft port → companion computer (see `docs/CONNECTION.md`).
   - Wire release servo to companion-computer PWM (see `docs/OPEN-QUESTIONS.md` #4).
   - Install DJI Authentication Chip if required by the aircraft port.

3. **Install DropPoint**
   ```bash
   sudo ./scripts/install.sh
   ```
   This copies the binary, config, DEM tiles, and enables the systemd service.

4. **Configure credentials**
   Copy `config/credentials.example.yaml` to `/etc/droppoint/credentials.yaml`
   and fill in the real DJI app values. This file is never overwritten by
   upgrades.

5. **Edit aircraft/payload config**
   - Confirm `config/aircraft.yaml` matches the real aircraft model, link mode,
     baud rate, and RC button mapping.
   - Calibrate each payload profile (mass, drag area, mechanism delay) using
     `droppoint_calibrate` and bench drops (see `docs/CALIBRATION.md`).

6. **Bench test before flight**
   - Remove payload from the release mechanism.
   - Power aircraft + companion computer on the bench.
   - Run `droppoint --selftest` and fix any `[FAIL]` lines.
   - Trigger a manual release and verify the servo actuates once, then returns
     to safe position on disarm.

7. **Controlled flight test**
   - Fly to a safe altitude over an open area.
   - Designate a target using the gimbal crosshair, confirm, arm, and release
     manually.
   - Compare actual impact to predicted impact using the logged CSV.
   - Iterate payload calibration until miss is within tolerance.

Only after successful controlled test drops should auto-release be used in an
operational mission.
