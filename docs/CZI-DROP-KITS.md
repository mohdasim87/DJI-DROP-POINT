# Using DropPoint with CZI (and similar) drop kits

**Status: current.** Everything here comes from the DJI Payload SDK v3.16.0
headers, DJI's developer documentation and CZI's published manuals. Bench
test with empty hooks before the first operational drop.

## Short answer

| Aircraft | CZI kit | Can DropPoint trigger the drop itself? | How it works |
|---|---|---|---|
| Matrice 400 | TH4 V2/V3 (SkyPort) | **Yes, in principle** — through the PSDK Widget Manager | DropPoint presses the kit's own drop widget in software and can read its state back |
| Matrice 4T | TH2 (light-triggered) | **No** | Operator switches the aircraft's bottom auxiliary light on in DJI Pilot 2; DropPoint gives guidance and the countdown |
| Matrice 4T | FT4-P (PSDK, E-Port) | **No** | Operator presses the kit's widget in DJI Pilot 2; DropPoint gives guidance and the countdown |

## Why the two aircraft differ

A CZI drop kit is itself a PSDK payload: it registers its own widgets with
DJI Pilot 2 and DJI gives no general way for one payload to command another.
The one exception is the **Widget Manager** (`dji_widget_manager.h`):

- `DjiWidgetManager_WidgetDownloadFileList` / `DownloadFileByIndex` — download
  another payload's widget configuration (so the drop widget's index can be
  found).
- `DjiWidgetManager_SubscribePayloadWidgetStates` — be told when that
  payload's widgets change (release confirmation).
- `DjiWidgetManager_SetWidgetState(position, {type, index, value})` — set a
  widget on that payload, exactly as an operator press would.

DJI's documentation states this is supported **only on the Matrice 400** and
only from a Linux PSDK device. The Matrice 4T has no equivalent, and the CZI
TH2 is not a PSDK payload at all: it is a photoresistor that fires when the
aircraft's bottom auxiliary light has been on for more than 200 ms (first
switch-on releases hook 1, the next releases hook 2).

## Release backends in `aircraft.yaml`

| `release.backend` | Use with | Auto release | Notes |
|---|---|---|---|
| `external_operator` | Any CZI kit, either aircraft | Not available | Default for both profiles. DropPoint never actuates anything; the floating window says `PRESS RELEASE ON DROP KIT` |
| `psdk_widget` | CZI TH4 on Matrice 400 | Available | Needs the kit's mount position and drop-widget index, found with the probe below |
| `pwm_servo` | Your own servo latch on a companion computer with PWM pins | Available | Not available on Manifold 3 (it has no PWM pins) |

Both profiles ship with `external_operator` because it works on day one and
cannot fire the wrong thing. Switch the Matrice 400 to `psdk_widget` only
after the probe and a bench test.

## Operator-actuated release (both aircraft)

1. Designate and confirm the target, then ARM in DropPoint.
2. DropPoint counts down. On `DROP`, trigger the kit:
   - TH4 / FT4-P: press the kit's drop widget in Pilot 2, or the RC button you
     mapped to it in Pilot 2's custom-button settings.
   - TH2: switch the bottom auxiliary light to **On**. Before flight set that
     light to **Off**, not Auto, or the aircraft can drop the payload on its
     own when it gets dark.
3. Press DropPoint's RELEASE widget to log the drop, or let the arm expire.

The time between the `DROP` call and the hook actually opening (your reaction
plus the kit's delay) goes into `mechanism_delay_s` for the payload profile.
Measure it with test drops; `tools/calibrate` fits it.

## Software-actuated release on the Matrice 400 (`psdk_widget`)

1. Mount the TH4 and note which port it is on.
2. Run `droppoint --probe-widgets <mount position>`. It downloads the kit's
   widget file and prints every widget's index, type and name.
3. Put the drop widget's index and type in `release.widget` in
   `aircraft.yaml` and set `backend: psdk_widget`.
4. Bench test with **no payload on the hooks** and confirm one press releases
   exactly one hook.

Open points that only hardware can settle: whether the TH4 accepts a
Widget Manager press the same way it accepts an operator press, whether its
anti-misdrop interlock needs a second widget set first, and which mount
position the kit reports on a SkyPort V3 adapter.

## Weight

- **Matrice 4T:** DJI's maximum payload is 200 g. A TH2 is 70 g and a
  Manifold 3 is 120 g, which leaves 10 g. With a light companion computer
  (about 40 g with cabling) and a TH2 there is roughly 90 g for the payload.
  CZI quotes 500 g per hook; that exceeds DJI's limit and is outside what
  DropPoint's shipped profile allows.
- **Matrice 400:** 6 kg maximum payload. H30T (920 g) + Manifold 3 (120 g) +
  TH4 (320 g) leaves about 4.4 kg.

`aircraft.yaml` carries `max_payload_kg` and `onboard_equipment_kg`; DropPoint
refuses to start if a payload profile would exceed the margin.
