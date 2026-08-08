# Hardware

**Status: unverified — requires hardware.** Parts and power figures below
come from the build spec and DJI's own published specifications
(docs/PSDK-RESEARCH.md), not from a bench build in this environment.

## 1. Parts list

| Part | Notes |
|---|---|
| Companion computer | DJI Manifold 3 (required for the liveview AI-metadata marker channel, docs/PSDK-RESEARCH.md section 6) or a generic Linux aarch64 SBC (Jetson, RPi) for text/audio-only operation |
| DJI Authentication Chip | **Required** for any third-party payload on E-Port / E-Port Lite; without it the payload is not recognised at all. Procure from a DJI-authorised distributor |
| Release mechanism (servo + mechanical latch) | Customer/site-specific; `hal::IRelease` assumes a standard PWM hobby-servo interface, see `src/psdk/release_gpio.h` |
| PWM-capable GPIO (companion computer built-in, or an external I2C PWM controller e.g. PCA9685) | Drives the release servo — **not** via PSDK, see `docs/OPEN-QUESTIONS.md` #4 |
| SkyPort V2 / X-Port / E-Port cable (aircraft-specific) | Match exactly to your aircraft model; SkyPort V2 has a keyed red-mark orientation, see `docs/CONNECTION.md` |
| microSD card (companion computer boot media) | Sized for the offline DEM tiles for your operating area plus OS + logs |
| PPS wiring (M300 only) | Pin A5 to a companion-computer GPIO capable of edge-triggered interrupts, for hardware time sync (spec 5.5) |

## 2. Power budget

E-Port/SkyPort/X-Port supply one of 13.6 V, 17 V, or 24 V, requested by the
companion computer at enumeration. Budget for:

- Companion computer idle + full guidance-loop CPU load (consult your
  specific SBC's datasheet — not independently measured here)
- Release servo peak current draw during actuation (consult the servo
  datasheet; a typical hobby servo draws several hundred mA to ~1-2 A at
  stall — **measure your specific servo**, do not assume)
- Any USB peripherals (only relevant if using USB-bulk video link mode)

**Status: unverified.** Measure actual current draw on the bench with a
multimeter/USB power meter before committing to a specific voltage
request; undervoltage brownouts under servo-actuation transients are a
plausible failure mode that has not been characterised here.

## 3. Mounting

- Companion computer: follow the vendor's thermal and vibration mounting
  guidance. Vibration isolation matters more for gimbal/IMU-adjacent
  mounting than for E-Port body mounting.
- Release mechanism: mount so the payload's centre of mass at release does
  not induce excessive pitch/roll disturbance; this is airframe- and
  payload-specific and outside this document's scope.
- Antenna/RF: if using USB-bulk or network video link mode, keep USB
  cabling short and shielded — USB3 is a known RF noise source near GNSS
  antennas on some airframes (a general DJI/companion-computer integration
  caution, not specifically measured for this project).

## 4. Bench checklist before first flight

This mirrors `docs/CONNECTION.md` step 11 but as a standalone pre-flight
checklist:

1. Payload physically removed from the release mechanism.
2. Power the aircraft + companion computer from bench power (not battery),
   if your setup allows it, to avoid draining a flight battery during bench
   testing.
3. Run `droppoint --selftest`; confirm "release GPIO responding: OK" (once
   `src/psdk/release_gpio.cpp` is running against real hardware — today
   this is verified via `sim::SimRelease` only, see `docs/OPEN-QUESTIONS.md`).
4. Command a manual RELEASE from the widget/RC binding and visually confirm
   single-actuation, then a return to the safe position on disarm.
5. Only then attach the payload and proceed to a flight test.
