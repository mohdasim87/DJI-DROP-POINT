# Operation

**Status: unverified — requires hardware for the Pilot-rendered widget/audio
experience described below; the underlying state machine and gating logic
are exercised today via `droppoint_sim_main --sim <scenario>` and
`tests/`.**

## 1. Pre-flight

1. Confirm `droppoint --selftest` reports no `[FAIL]` lines and review any
   `[WARN]` (e.g. an uncalibrated payload profile) before flying a mission
   where accuracy matters.
2. Select the payload profile (PAYLOAD widget/list) matching what is
   physically loaded.
3. Select RELEASE MODE: MANUAL or AUTO (spec section 8). Manual keeps a
   human finger on the release trigger at all times; auto still requires a
   confirmed target and passing gates before it will ever fire, and a
   hardware abort overrides it unconditionally.
4. Take off and fly normally in DJI Pilot 2 — DropPoint never takes control
   of the aircraft itself, only the payload and its own UI surfaces.

## 2. Designating a target (Click To Drop)

In priority order (spec section 3):

1. **Pilot Pin Point** — not available with the public PSDK as of this
   build (docs/OPEN-QUESTIONS.md #1); skipped automatically.
2. **Gimbal crosshair (always available):** slew the gimbal so the
   crosshair sits on the intended landing spot, then press **SET TARGET**.
   This fires the laser rangefinder and latches the result. Works well in
   gloves/sunlight since it needs no touch precision.
3. **Virtual cursor (Source C):** use the CURSOR X / CURSOR Y range-bar
   widgets to move a cursor rendered on the video to a point not under the
   crosshair, then press **SET TARGET**.

After latching, the floating window shows distance/bearing from the
aircraft, ground elevation, and whether that elevation is LRF-corrected or
raw-DEM (a raw-DEM reading is a soft-gate warning — range the target with
the laser before arming if accuracy matters, spec 3.5/3.6).

**Designate from a high depression angle.** Shallow designation angles
below 30° trigger a soft warning; below 15° the system refuses the
designation outright (spec 3.5).

Press **CONFIRM TARGET** once the floating window's summary matches what
you intended. This matters more in auto mode — a mis-designated target in
auto mode is unrecoverable once armed.

## 3. Arming and release

1. Press **ARM** (widget or the mapped RC hardware button). Arming requires
   a confirmed, fresh target and every hard gate passing; if it's refused,
   the floating window's HOLD line says exactly what to fix.
2. Once armed and inside the approach corridor, the audio countdown begins
   automatically: a tone at T-5, then "three", "two", "one". T-0 ("drop") is
   issued early by the measured mechanism delay so actual separation
   happens at the computed release point — this is intentional, not a
   timing bug.
3. **Manual mode:** press RELEASE (widget or RC button) at any point during
   the countdown; the servo fires immediately on that press.
4. **Auto mode:** the system fires itself at the computed instant, leading
   by the mechanism delay, with no further operator action — but it is
   re-checking every hard gate at 10 Hz the entire time and will abort
   audibly ("abort") and disarm if anything trips, including if the
   predicted impact diverges from the latched target (solver divergence or
   bad wind data) or the impact would land inside a configured no-drop
   polygon.
5. **Hardware abort** (mapped RC button) disarms instantly from any state,
   including mid-countdown, in both modes. Use it any time something looks
   wrong — it costs nothing but a re-arm.
6. After release, the system returns to IDLE automatically. **Every drop
   requires a fresh arm** — nothing persists across drops.

## 4. Every hold reason and its remedy

| Floating window message | What it means | What to do |
|---|---|---|
| `NO TARGET - AIM AND PRESS SET TARGET` | Nothing latched yet | Designate a target (section 2) |
| `TARGET EXPIRED - RE-DESIGNATE` | Latched target older than 120 s | Re-designate |
| `DEPRESSION ANGLE TOO LOW - RE-DESIGNATE FROM STEEPER ANGLE` | Designated below 15° depression | Climb or reposition, then re-designate |
| `HOLD - CROSSWIND EXCEEDS PROFILE LIMIT` | Crosswind above the loaded payload's limit | Wait, reposition, or switch to a payload profile rated for the conditions |
| `HOLD - CLIMB, BELOW MINIMUM RELEASE AGL` | Too low for the loaded payload | Climb to at least the profile's minimum release AGL |
| `HOLD - GNSS SATELLITE COUNT LOW` | Fewer than the configured minimum satellites | Reposition for better sky view; wait for GNSS to recover |
| `HOLD - RTK FIX REQUIRED` | Aircraft/site requires RTK FIX but only FLOAT or none is available | Wait for RTK FIX, or fly closer to the base station |
| `HOLD - TELEMETRY STALE` | No fresh telemetry within the configured window | Check the aircraft link; this usually self-clears |
| `HOLD - STABILISE, ACCELERATION TOO HIGH` | Aircraft accelerating too hard for a stable solution | Stabilise the aircraft (reduce stick inputs) |
| `HOLD - NO TERRAIN ELEVATION AVAILABLE` | Missing DEM tile for the target location | Load the correct DEM tile before the mission, or designate a different point |
| `HOLD - SOLVER DID NOT CONVERGE` | The inverse ballistics solver failed to find a release point | Re-designate; check wind/telemetry sanity |
| `ABORT - PREDICTED IMPACT DIVERGES FROM TARGET` | Predicted impact moved too far from the latched target mid-countdown | Re-arm once the divergence resolves; investigate wind data if persistent |
| `ABORT - IMPACT INSIDE NO-DROP POLYGON` | Predicted impact would land inside a configured exclusion zone | Reposition; do not attempt to bypass the polygon |
| `TGT ELEV FROM DEM ONLY - RANGE THE TARGET FIRST` (soft) | Target elevation is raw-DEM, not LRF-corrected | Fire the laser rangefinder at/near the target before arming for best accuracy |
| `SHALLOW DESIGNATION ANGLE - ACCURACY DEGRADED` (soft) | Depression angle between 15° and 30° | Consider re-designating from a steeper angle if accuracy matters |

## 5. Quick-reference card (laminate-ready, one page)

```
DROPPOINT QUICK REFERENCE
==========================
DESIGNATE:  slew crosshair onto target -> SET TARGET -> check summary -> CONFIRM TARGET
ARM:        press ARM (needs confirmed target + all gates clear)
COUNTDOWN:  tone(T-5) -> "three" -> "two" -> "one" -> "drop" (fires early by mechanism delay)
MANUAL:     press RELEASE any time during countdown
AUTO:       fires itself; still gate-checked at 10Hz; will abort audibly if unsafe
ABORT:      hardware abort button -- works instantly, any state, any time
AFTER DROP: system returns to IDLE -- every drop needs a fresh ARM

IF IN DOUBT: ABORT. Re-arming costs seconds; a bad release does not undo.
```
