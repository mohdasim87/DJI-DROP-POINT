# Compatible drop kits — which model to buy

**Status: current.**

Weights and load figures below are each maker's published specification,
read in November 2026. Confirm the exact model, mounting bracket and
connector for your aircraft with the seller before buying.

Set the kit you bought as `release.kit` in `aircraft.yaml`. DropPoint then
checks it against the aircraft model, the release backend and every payload
weight, and refuses to start on a mismatch. `droppoint --list-kits` prints
this list from the software.

## How DropPoint works with a drop kit

Every kit below is a self-contained DJI payload with its own drop button in
DJI Pilot 2. That gives two ways of working:

- **Operator-triggered (`external_operator`)** — works with every kit.
  DropPoint aims, checks the gates and counts down; the operator presses the
  kit's drop button (or the RC button mapped to it) on `DROP`. MANUAL mode
  only.
- **Software-triggered (`psdk_widget`)** — Matrice 400 only. DropPoint
  presses the kit's drop button itself through DJI's Widget Manager, which
  allows AUTO release. DJI offers this on the Matrice 400 and not on the
  Matrice 4T. The button to press differs per kit; find it with
  `droppoint --probe-widgets` and bench test with empty hooks first.

## DJI Matrice 4T

DJI's payload limit is 200 g including the kit, the companion computer and
the payload, so kit weight matters more than hook rating here.

| Buy this model | Maker | Connects to | Weight | Hooks | Maker's load per hook | Triggered by | AUTO | `release.kit` |
|---|---|---|---|---|---|---|---|---|
| **AD2 Two Drop Kit** | JZ | E-Port Lite | 43 g | 2, any order | 1 kg | Pilot 2 button or RC button | No | `jz_ad2` |
| FT4-P Dual-Channel Drop System | CZI (Cytop) | USB-C, PSDK | 50 g | 2, any order | 2 kg (500 g recommended) | Pilot 2 button | No | `czi_ft4p` |
| TH2 Throwing Hook | CZI | None — light sensor, power from the battery USB-C | 70 g | 2, in sequence | 500 g single, 300 g each | Aircraft bottom auxiliary light in Pilot 2 | No | `czi_th2` |
| Your own servo latch | — | Companion computer PWM pin | ~15 g | 1 | your design | DropPoint | **Yes** | `custom_pwm_servo` |

Notes for the Matrice 4T:

- The **JZ AD2** is the lightest and uses the E-Port Lite, which leaves the
  main E-Port for DropPoint's computer. That makes it the best fit on paper.
- The **CZI FT4-P** maker does not state which of the two aircraft ports it
  uses. If it needs the main E-Port, DropPoint's computer has to move to the
  E-Port Lite, where DJI allows fewer functions. Ask the seller.
- The **CZI TH2** needs the bottom auxiliary light set to Off, not Auto, or
  the aircraft can drop by itself in low light.
- Makers' hook ratings exceed what the aircraft may carry. DropPoint enforces
  the aircraft's 200 g limit, not the hook rating.

## DJI Matrice 400

| Buy this model | Maker | Connects to | Weight | Hooks | Maker's load per hook | Triggered by | AUTO | `release.kit` |
|---|---|---|---|---|---|---|---|---|
| **TH4 V3 Throwing Hook Drop System** (V2 also listed) | CZI | SkyPort V2 | 320 g | 4, any order | 10 kg | Pilot 2 button | With `psdk_widget` | `czi_th4` |
| PTS4 Visual Drop Kit (has an aiming camera) | JZ | SkyPort V2 | 210 g | 4, any order | 10 kg | Pilot 2 button | With `psdk_widget` | `jz_pts4` |
| UT4A Utility Drop Kit (siren and strobe) | JZ | JZ T-Port, needs JZ's Matrice 400 bracket | 178 g | 4 | 6 kg | Pilot 2 button | With `psdk_widget` | `jz_ut4a` |
| PL10 Lifting Kit | JZ | E-Port V2 | 80 g | 1 | 10 kg | Pilot 2 button or RC button | With `psdk_widget` | `jz_pl10` |
| Talon Drop System for Matrice 400 | DroMight | E-Port V2 | 482 g | 1 | limited by the aircraft | Pilot 2 button | With `psdk_widget` | `dromight_talon` |
| M400 4-hook Drop Kit | Cybowindow | E-Port V2 | 645 g | 4, any order | 6 kg | Pilot 2 button | With `psdk_widget` | `cybowindow_4hook` |
| Your own servo latch | — | Generic companion computer PWM pin (not Manifold 3) | ~50 g | 1 | your design | DropPoint | **Yes** | `custom_pwm_servo` |

Notes for the Matrice 400:

- CZI lists Matrice 400 support for the TH4 after a firmware update; check
  the kit's firmware when it arrives.
- The aircraft's 6 kg payload limit applies to the kit, the camera, the
  companion computer and the payload together.
- Winch and lowering kits (rope delivery) are a different kind of delivery
  and are not covered: DropPoint computes a free-fall drop.

## Not compatible

- Kits made for the Matrice 30, Matrice 300/350 OSDK port or Mavic 3 only.
- Any kit controlled from a separate hand-held transmitter: DropPoint can
  still give the countdown, but there is nothing for it to check or trigger.
