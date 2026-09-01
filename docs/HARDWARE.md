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

## 5. Shopping list

This list follows directly from the parts list and connection guide above.
**Exact SKUs depend on your aircraft model, region, and payload mechanics;**
use the descriptions here to confirm compatibility before ordering. All DJI
parts should be sourced from DJI or a DJI-authorised distributor.

### Required

| Item | Why you need it | Notes |
|---|---|---|
| DJI aircraft with a PSDK payload port | The platform DropPoint controls from | M300 RTK, M350 RTK, M30/M30T, or Mavic 3E/3T. Verify the aircraft has a usable E-Port/SkyPort/X-Port/OSDK port for your use case. |
| Companion computer (Linux aarch64) | Runs the `droppoint` binary | DJI Manifold 3 if you need the liveview AI-marker overlay; otherwise NVIDIA Jetson or Raspberry Pi 4/5 (text/audio-only operation). |
| DJI Payload SDK Authentication Chip | Required for the aircraft to recognise a third-party E-Port/E-Port Lite payload | Order from DJI/authorised distributor; without it the payload is not enumerated. |
| Aircraft-specific PSDK cable | Connects aircraft port to companion computer | SkyPort V2, X-Port, E-Port, or OSDK cable matching the aircraft model. Double-check connector/orientation (SkyPort V2 has a red alignment mark). |
| microSD card (64 GB or larger, U3/Class 10) | Boot media for the companion computer plus offline DEM tiles and logs | Size depends on operating-area DEM coverage. |
| Release servo + mechanical latch | Physically drops the payload | Servo must be rated for your payload mass and latch torque. Standard 50 Hz PWM hobby-servo interface. |
| Servo power supply / BEC | Powers the servo without browning out the companion computer | Choose voltage (5 V or 6 V typical) and current rating above the servo's stall current. |
| Companion-computer PWM output | Drives the release servo | Built-in PWM header, or an I2C PWM controller such as a PCA9685 board if the computer has no native PWM. |
| DC-DC regulator / power module | Converts aircraft port voltage (13.6/17/24 V) to companion-computer input voltage | Size for companion-computer peak load plus servo/BEC inefficiency. |
| Laptop with DJI Assistant 2 | One-time payload binding | Windows PC; only needed for initial setup, not in the field. |

### Cables, connectors, and small parts

- Servo extension leads matched to your servo/latch geometry.
- Jumper/Dupont wires for GPIO/PPS connections.
- USB-to-TTL UART adapter if using a Raspberry Pi (no native aircraft UART header).
- Short, shielded USB3 cable if using USB-bulk video/liveview on Jetson (keep away from GNSS antennas).
- PPS signal wire for M300 (pin A5 to a companion-computer GPIO interrupt pin) — optional but recommended for tight timestamp sync.

### Calibration and bench-test extras

| Item | Why you need it |
|---|---|
| Bench power supply | Power aircraft + companion computer on the bench without draining flight batteries |
| Multimeter / USB power meter | Verify rail voltages and catch servo-actuation brownouts |
| Servo tester | Exercise the release mechanism independently of DropPoint before first flight |
| Printed checkerboard (known square size) | Lens distortion/FOV calibration via OpenCV/MATLAB |
| Ground-control markers / tape | Measure actual drop impact vs. predicted impact during calibration drops |
| Spare microSD card | Field-rollback image; swap cards instead of debugging in the field |

### What not to order

- **RC controller app / Mobile SDK licence / APK tooling:** DropPoint does not
  install on the RC controller. DJI Pilot 2 stays in place; the interface is
  rendered from data sent by the companion computer.
- **Generic flight-controller telemetry cables:** the companion computer uses
  the DJI payload port, not a flight-controller UART such as MAVLink.
- **PSDK GPIO/PWM board from DJI:** the PSDK does not expose a generic GPIO/PWM
  API; release control comes from the companion computer's own PWM, not a DJI
  accessory.
