# DropPoint on the DJI Matrice 4T

**Status: unverified — requires hardware.** Built and tested in simulation and
on an ARM64 Linux machine with no aircraft attached. Nothing below has flown.

Config profile: `config/profiles/m4t/` · Package: `droppoint-m4t_<version>_arm64.deb`

## What the Matrice 4T allows

DJI limits what a third-party companion computer may do on this aircraft.
From DJI's Open Capability List (Matrice 4E/4T, E-Port column):

| Function | Available | Effect on DropPoint |
|---|---|---|
| Data subscription (position, attitude, velocity, gimbal angles) | Yes | Guidance works |
| Custom widgets and floating window | Yes | All operator controls and status text work |
| RC button mapping for widgets | Yes | ARM / RELEASE / ABORT / SET TARGET can be bound to RC buttons in Pilot 2 |
| Camera management (laser rangefinder, zoom) | **No** | Target elevation comes from terrain data, not the laser |
| Gimbal management | No | Not needed; the operator aims with the RC |
| Liveview overlay (AI boxes, AR drawing) | **No** | No marker on the video; status is text only |
| RC sound / vibration alarm | No | Countdown is shown as text, not spoken |

The "No" rows become available only with a DJI Manifold 3 or with DJI's
advanced-permission approval. See the weight section for why a Manifold 3 is
not practical on this aircraft.

## Weight — read this first

DJI's maximum payload for the Matrice 4T is **200 g**. Everything you bolt on
counts.

| Build | Fixed equipment | Left for the payload |
|---|---|---|
| Manifold 3 (120 g) + CZI TH2 (70 g) | 190 g | 10 g |
| Light companion computer with cabling (~40 g) + CZI TH2 (70 g) | ~110 g | ~90 g |
| Light companion computer (~40 g) + own micro-servo latch (~15 g) | ~55 g | ~145 g |

The shipped profile assumes the second row (`onboard_equipment_kg: 0.110`)
and ships two payload profiles of 40 g and 80 g. DropPoint refuses to start if
a payload profile plus the fixed equipment exceeds `max_payload_kg`. Weigh
your actual build and edit `aircraft.yaml`.

CZI advertises 500 g per hook for the TH2. That is beyond DJI's limit for the
aircraft and is not supported by the shipped profile.

## Hardware

| Item | Notes |
|---|---|
| DJI E-Port Development Kit | Breaks the aircraft E-Port out to UART, USB and power |
| DJI SDK Certified Chip | Required for third-party payloads on E-Port; without it the aircraft ignores the payload |
| Companion computer, ARM64 Linux, glibc 2.31 or newer | As light as possible; needs one UART or a USB-serial adapter |
| Drop mechanism | CZI TH2, CZI FT4-P, or your own servo latch (see below) |

Connection points from DJI's device-connection guide:

- The E-Port coaxial USB-C cable is not keyed. If one end is reversed the kit
  will not power up; flip the other end to match.
- Set the kit's USB mode switch to **Host** and its E-Port dip switch to **ON**.
- Connect everything **before** powering the aircraft. The Matrice 4T E-Port
  does not support hot-plugging.
- Wire the kit's UART to the companion computer and set `uart_device` in
  `aircraft.yaml` (default `/dev/ttyUSB0`). Baud rate 921600.

## Drop mechanism options

| Mechanism | Port used | Who triggers it | DropPoint setting |
|---|---|---|---|
| CZI TH2 | None (photoresistor under the aircraft, powered from the battery USB-C) | Operator switches the bottom auxiliary light on in Pilot 2 | `release.backend: external_operator` (shipped default) |
| CZI FT4-P | The E-Port | Operator presses the kit's widget in Pilot 2 | `external_operator`; DropPoint's computer must then use the E-Port Lite with a USB-serial adapter, which DJI lists with fewer functions |
| Own servo latch | Companion computer PWM pin | DropPoint | `release.backend: pwm_servo`; the only option here that allows AUTO release |

With the TH2, set the aircraft's bottom auxiliary light to **Off** (not Auto)
before flight, or the aircraft can release the payload by itself in low light.
Details in `docs/CZI-DROP-KITS.md`.

## Target elevation without the laser

Because the laser rangefinder is not available to a third-party computer on
this aircraft, DropPoint works out the ground height at the target like this:

1. **Terrain tiles + take-off tie (best available).** Put SRTM `.hgt` tiles for
   your area in `/opt/droppoint/data/dem/`. While the aircraft sits on the
   ground before take-off, DropPoint records its altitude and shifts the
   terrain data to match. Shown as `DEM+HOME`.
2. **Flat ground at take-off height.** With no tile for the area, DropPoint
   assumes the target is at the same height as the take-off point. Shown as
   `FLAT`, with the warning `FLAT TERRAIN ASSUMED`. Only valid on flat sites.

Start DropPoint before take-off. If it starts in the air there is no take-off
tie and it will hold with `NO TERRAIN ELEVATION AVAILABLE`.

A 10 m error in target height moves the impact point by about 2.5 m for a
light payload in a 5 m/s wind, so on sloping ground without terrain tiles,
expect misses of that order.

## Install

On the companion computer, with no internet needed:

```
sudo dpkg -i droppoint-m4t_<version>_arm64.deb
sudo nano /etc/droppoint/credentials.yaml     # DJI app name, ID, key, licence, account
sudo nano /opt/droppoint/config/aircraft.yaml # uart_device, onboard_equipment_kg, release
sudo -u droppoint /opt/droppoint/bin/droppoint --selftest --no-link --config /opt/droppoint/config
sudo systemctl start droppoint
```

Run the self-test as the `droppoint` service user, as shown. Then, with the
aircraft powered and connected, run it again without `--no-link` and read
`/var/log/droppoint/selftest.txt`. Bind the payload once in DJI Assistant 2.

## Flying a drop

1. Power on with DropPoint already connected. Wait on the ground until the
   floating window appears in Pilot 2.
2. Take off, climb to the release height, and **hover for at least 5 seconds**
   so DropPoint can measure the wind from the aircraft's lean.
3. Put the crosshair on the landing spot, press SET TARGET, check the range
   and bearing shown, press CONFIRM.
4. Press ARM. Follow `MOVE 4m BRG 265` until it reads `DROP OK`.
5. Trigger the drop kit. Press RELEASE in DropPoint to log it.

Only MANUAL mode is offered with a CZI kit on this aircraft.

## To verify on the aircraft before relying on it

- Which UART device the E-Port kit appears as, and that 921600 baud links.
- That `GIMBAL_ANGLES` and the other subscription topics arrive on this model.
- Heading, pitch and roll signs: nose down should read negative pitch, right
  roll positive, and gimbal straight down −90°.
- `cda_airframe` for the wind estimate: hover in a wind you have measured with
  an anemometer and adjust until DropPoint's estimate matches.
- Payload profiles: five measured drops each, then `droppoint_calibrate`.
