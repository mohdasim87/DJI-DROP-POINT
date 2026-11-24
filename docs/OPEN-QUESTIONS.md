# Open Questions

Every entry below records something Phase 0 could not resolve from the public
Payload-SDK headers, samples, or documentation, with the exact question a
human should answer once real hardware and/or a DJI developer account is
available. Nothing in this file is guessed; where the SDK is silent, the
software behind the affected interface is stubbed and the gap is called out
in the relevant HAL interface's header comment.

**Status: unverified — requires hardware/DJI account**, unless stated
otherwise.

---

## 1. [PRIORITY] Can PSDK read DJI Pilot 2's Pin Point (tap-to-designate) coordinate?

**Answer: No.** Pin Point is a Mobile SDK (DJI Pilot app) feature, not exposed
to Payload SDK applications.

**Evidence:**
- No occurrence of "pinpoint" (case-insensitive) anywhere under
  `psdk_lib/include/` in a fresh clone of `dji-sdk/Payload-SDK`.
- The only "tap" hits in the PSDK headers are `DjiCameraManager_TapZoomAtTarget`
  and related tap-zoom functions in `dji_camera_manager.h` — camera zoom
  control, unrelated to coordinate designation.
- DJI's own Mobile SDK (Android) API reference documents `DJICamera.PinPoint`
  and `DJICamera.PinPointInfo` as Mobile-SDK-only classes, explicitly scoped
  to the H20 series camera object reachable only from the Pilot/Mobile SDK
  object tree (`developer.dji.com/api-reference/android-api/Components/Camera/DJICamera_PinPointInfo.html`,
  `.../DJICamera_PinPoint.html`). There is no PSDK equivalent topic, struct,
  or query function.
- PSDK's nearest capability is `DjiCameraManager_GetLaserRangingInfo`
  (`dji_camera_manager.h`), a **polling** function returning the laser
  rangefinder's current lat/lon/altitude/distance/screen-position — this is
  "wherever the optical axis or AI target box currently points," not "the
  coordinate the operator last tapped in Pilot." It is a different feature
  with different semantics (see 3.2/3.3 in the build spec).

**Conclusion:** Source A (`ITargetSource` fed by Pilot's own Pin Point) is
**not implementable** with the public PSDK as of this clone. The designation
module is still built with `ITargetSource` as an interface (see
`src/hal/i_target_source.h`) specifically so this can slot in later without
restructuring, per spec 2.1 — but the default, and currently only,
implementations are Source B (gimbal crosshair) and Source C (virtual
cursor), both driven by the shared pixel-to-ground solver in `core/raycast.*`.

**Question for a human with a DJI developer account / DJI support contact:**
Is there an undocumented or partner-only PSDK API (or a newer PSDK release
after this clone) that exposes Pilot's Pin Point result to a companion
computer? If DJI adds this, wire it in as `PinPointTargetSource : ITargetSource`
in `src/psdk/` and give it top priority in the designation source list.

---

## 2. Does any wind data topic exist in the Data Subscription API?

**Answer: No dedicated wind topic exists.**

**Evidence:** The only "wind" references in `dji_fc_subscription.h` are the
`strongWindLevel1` / `strongWindLevel2` bit flags inside
`T_DjiFcSubscriptionFlightAnomaly` — boolean anomaly flags for the flight
controller's own go-home wind-safety logic, not a speed/direction
measurement. `dji_flight_controller.h` mentions wind only in a comment about
position drift. No `DJI_FC_SUBSCRIPTION_TOPIC_WIND*` symbol exists.

**Consequence:** `core/wind.h`'s hover-attitude estimator (spec 6.3) is not
an optional fallback — it is the **only** source of wind data this system
can obtain from the aircraft. It runs continuously, and its output is
gated for validity by ground-speed/hover conditions rather than trusted at
all times. `HAL::ITelemetry::GetWind()` is defined so a future wind topic
(or an external wind sensor added to the companion computer) can populate
it directly. See `src/hal/i_telemetry.h`.

**Question for a human:** Does the target aircraft carry any external wind
sensor (anemometer) reachable over UART/I2C from the companion computer that
could feed `ITelemetry::GetWind()` directly? If so, document its interface
and wire an adapter in `src/psdk/` (or `src/hal/` if fully aircraft-agnostic)
ahead of the hover-estimate fallback.

---

## 3. RC hardware button mapping

**Corrected answer: Yes — through the widget configuration, not through an
API call.** The original answer below was wrong. `widget_config.json`
supports a `customize_rc_buttons_config` block per widget (see DJI's sample
at `samples/sample_c/module_sample/widget/widget_file/en_big_screen/`), which
lets the operator bind that widget to an RC hardware button in DJI Pilot 2's
custom-button settings. DropPoint enables it for SET TARGET, CONFIRM, ARM,
RELEASE, ABORT, CLEAR TARGET and MODE. The RC-channel polling approach
described below is no longer used. Still to confirm on hardware: which RC
buttons Pilot 2 offers for binding on the RC Plus 2, and the press latency.

**Original answer (superseded): No public API to map custom PSDK actions to physical RC buttons.**

**Evidence:** No "custom button", "RC button mapping", or action-bar-to-RC
binding function exists anywhere under `psdk_lib/include/`. RC data is only
available as a read-only telemetry topic
(`DJI_FC_SUBSCRIPTION_TOPIC_RC` / `..._RC_WITH_FLAG_DATA`), i.e. stick and
switch *positions*, not an event/callback hook a PSDK app can bind an action
to.

**Consequence:** Spec section 9's requirement to "map ARM, RELEASE and
ABORT to RC hardware buttons" cannot be done via a first-class PSDK
binding API. The only implementable approach is to **poll the RC topic and
treat specific control positions as button-press events** (e.g. a spring-
loaded three-position switch mapped in DJI Pilot's own RC customization
menu to a value read by our RC topic subscription, or a dedicated switch
channel the operator assigns via Pilot's RC settings outside of our app).
This is what `src/hal/i_telemetry.h`'s `RcButtonEvent` polling model
implements; it is a workaround, not a native binding, and it depends on
the operator pre-configuring their RC in Pilot to expose a distinct
channel/switch for each of ARM, RELEASE, ABORT.

**Question for a human with RC hardware in hand:** Confirm which physical
RC switches/buttons are free on the target aircraft's controller, configure
them in DJI Pilot's RC customization screen to drive distinct channels, and
record the exact channel-to-function mapping in `config/aircraft.yaml`
(`rc_button_map` section, currently placeholder).

---

## 4. GPIO / PWM for the release servo

**Answer: PSDK provides no generic GPIO or PWM control API.**

**Evidence:** No `dji_gpio.h` or `dji_pwm.h` exists in `psdk_lib/include/`.
The only GPIO mention in the whole SDK is a note in `dji_power_management.h`
about GPIO12 needing to be pulled down on Manifold 3 before requesting high
voltage output — irrelevant to general-purpose I/O.

**Consequence:** The release servo cannot be driven "through PSDK." It must
be driven through the companion computer's own OS-level GPIO/PWM (Linux
sysfs `/sys/class/gpio` or `/sys/class/pwm`, or a dedicated PWM controller
chip over I2C/UART such as a PCA9685), entirely outside the PSDK library.
`src/hal/i_release.h` reflects this: its implementation for real hardware
lives in `src/psdk/release_gpio.*` and talks to the OS/kernel driver
directly, never to a DJI header.

**Question for a human:** Confirm the exact GPIO/PWM chip and pin used on
the reference companion computer (see `docs/HARDWARE.md` placeholder) and
the pulse width range needed to open/close the payload release mechanism's
servo, so `payloads.yaml` can carry calibrated min/max pulse values instead
of placeholders.

---

## 5. DPK packaging requirement for Manifold 3 / M400 / M4 / M4D liveview overlay

**Answer: Confirmed — DPK is required, and is Manifold-3-specific packaging,
not specifically tied to the liveview overlay feature alone.**

**Evidence:**
- `tools/build_dpk/build_dpk.sh` and its `README.md` in the upstream
  Payload-SDK repo describe DPK as the packaging tool that turns a
  development build into a `.dpk` installable for Manifold 3, driven by an
  `app.json` manifest (`user_app_id`, `firmware_version`, `platform`,
  localized name/description/maintainer fields).
- `samples/sample_c/platform/linux/manifold3/CMakeLists.txt` invokes
  `build_dpk.sh` as a post-build step automatically for the Manifold 3
  sample target.
- DJI's own tutorial page
  (`developer.dji.com/doc/payload-sdk-tutorial/en/manifold-quick-start/build-dpk.html`)
  states DPK converts "a development-state Manifold application package
  into a release-state installation package."
- Upstream issue `dji-sdk/Payload-SDK#431` confirms DPK only supports
  **static** library dependencies (no bundled `.so` files), which
  constrains our build (e.g. link SQLite and any DEM library statically for
  the Manifold 3 target).
- `DjiLiveview_SendAiMetaToPilot` itself is documented (comment in
  `dji_liveview.h`) as **Manifold 3 only** — i.e. the AI metadata overlay
  channel this project uses for the video marker is unavailable on other
  companion-computer classes regardless of packaging format.

**Not confirmed:** whether M400/M4/M4D-class companion computers specifically
require DPK, versus a plain `.deb`, to get the liveview overlay working —
the upstream repo's DPK tooling and CMake integration target Manifold 3
explicitly; no M400/M4/M4D sample was found in this clone.

**Question for a human:** When targeting an M400/M4/M4D-class companion
computer, confirm with DJI support or a newer SDK release whether
`DjiLiveview_SendAiMetaToPilot` is supported there at all, and if so whether
it requires DPK packaging or plain `.deb` install suffices. Until answered,
`docs/DEPLOYMENT.md` ships **both** a `.deb` and a DPK build target, and the
video marker degrades to the null `i_liveview` implementation (text + audio
only) on any platform where the metadata channel is unavailable or
unverified.

---

## 6. RC baud rate ceiling per aircraft

The build spec (section 5.3) states Mavic 3E/3T supports 921600 baud only,
while M350 RTK and M30/30T accept a range up to 1,000,000. This was supplied
in the build spec itself, not independently re-derived from the PSDK repo in
this research pass (no per-aircraft baud table was found in the public
headers — baud rate is a free-form string field, `T_DjiUserInfo.baudRate`,
with no enumerated allowed values in code). Treat the per-aircraft ceilings
in `config/aircraft.yaml` as carried over from the build spec until a human
confirms against the DJI developer forum or hardware testing per aircraft.

---

## 7. Exact RC/hardware abort latency

Spec section 8.1 requires the hardware abort button to disarm "instantly."
Given RC button state can only be observed via polling the RC subscription
topic at whatever rate it delivers (spec doesn't give a number found in
headers beyond "up to 100 Hz" for `DJI_FC_SUBSCRIPTION_TOPIC_RC`), worst-case
abort latency is bounded by that topic's actual delivery rate on real
hardware, which has not been measured.

**Question for a human on real hardware:** Measure end-to-end abort latency
(RC switch flip to servo returning to safe state) and confirm it is
acceptable for the mission profile; tune the guidance loop's telemetry
polling rate if not.

---

## 8. NED-to-ENU quaternion / gimbal-angle conversion signs

`src/psdk/psdk_util.h` converts PSDK's body-FRD-to-NED quaternion into DropPoint's
ENU Euler angles (Z-Y-X Tait-Bryan) and converts the PSDK gimbal-angle topic
(degrees, x=pitch, y=roll, z=yaw, NED frame) into the same ENU convention.
The transformation matrices are derived by coordinate similarity, but no
real-flight data is available to verify that the sign of yaw, pitch, and roll
matches the aircraft/gimbal physical behaviour. An error here would rotate the
raycast ray incorrectly and produce wrong ground points, especially at long
slant ranges.

**Question for a human on real hardware:** With the aircraft on the ground and
the gimbal nadir, confirm the converted ENU Euler angles are zero/level. Then
with the aircraft yawed 90° right (clockwise when viewed from above), confirm
`AircraftState.yaw_rad` increases (ENU convention: 0 = East, positive
counter-clockwise). With the gimbal pitched 45° down, confirm
`GimbalState.pitch_rad` is approximately -0.785 rad.

---

## 9. Rangefinder altitude and distance units

**Resolved from the header.** `dji_camera_manager.h` (v3.16.0) documents
`altitude` and `distance` as `Unit: 0.1m`, and longitude/latitude in degrees.
The earlier statement here that the header gave no scaling was wrong. What
remains open is only which altitude datum the laser result uses, below.

**Question for a human on real hardware:** Capture a set of
`DjiCameraManager_GetLaserRangingInfo` readings at known slant ranges and
compare `info.distance`/`info.altitude` to the truth to determine the exact
unit. If the scaling differs from 0.1, update
`RangefinderCameraManager::GetLatestReading()` and the documentation.

---

## 10. Audio TTS trigger mechanism

**Updated.** The widget speaker handler is for a loudspeaker *on the payload*
(Pilot sends it audio to play); it cannot make the RC speak, so DropPoint no
longer registers it. There is no PSDK call that makes Pilot 2 read out a
phrase. What exists is `DjiHmsCustomization_AlarmEnhancedCtrl`
(`dji_hms_customization.h`), which plays an alarm sound and/or vibrates the
RC and is documented as Manifold 3 only. DropPoint uses it for the countdown
on Manifold 3 (one pulse per cue, three for "drop") and falls back to
text-only on other companion computers. Untested: what the alarm actually
sounds like and whether pulses one second apart are distinguishable.

**Question for a human with DJI support / hardware:** Confirm how a PSDK app
initiates spoken countdown phrases. If the companion computer must drive its
own speaker, document the required audio hardware and update
`src/psdk/audio_speaker.cpp` to play the buffered TTS data or a pre-rendered
countdown clip.

---

## 11. Widget UI configuration / action buttons

**Implemented, untested on Pilot 2.** DropPoint now generates
`widget_config.json` (plain JSON plus PNG icons — not binary files, as this
entry previously said) and registers ten widgets: SET TARGET, CONFIRM, ARM,
RELEASE, ABORT on the main interface, and CLEAR TARGET, MODE, PAYLOAD, TEMP,
SURFACE on the settings page. Still to confirm on hardware: that Pilot 2
accepts the generated file, how the five main-interface widgets are laid out
on the RC Plus 2, and the icon sizes it expects.

**Question for a human with hardware:** Produce or obtain the DJI widget
configuration binary files for the target screen size/language and wire them
in via `DjiWidget_RegDefaultUiConfigByDirPath` in `main_psdk.cpp`. Map the
button indices to the `CycleInputs` fields (set_target, confirm_target, arm,
release, abort) so the app can be fully controlled from Pilot as well as from
RC switches.

---

## 12. What a third-party companion computer may do on Matrice 4T and Matrice 400

**Answer (from DJI's Open Capability List, read 2026-11):** only basic
functions — data subscription, custom widgets, floating window, power
management, time sync, custom HMS. Camera management (laser rangefinder,
zoom), gimbal management and liveview AI/AR overlays are "advanced" and are
open only to a Manifold 3 or to developers DJI has granted advanced
permission. `aircraft.yaml` `advanced_features` gates every such call.

**Question for a human:** if the laser rangefinder is wanted on a Matrice 4T
(where a Manifold 3 uses almost the whole 200 g payload allowance), apply to
DJI for PSDK advanced permission and record the outcome here.

---

## 13. Driving a CZI drop kit through the Widget Manager (Matrice 400)

`dji_widget_manager.h` lets one Linux PSDK device set another payload's
widget; DJI documents it for Matrice 400 only. DropPoint's `psdk_widget`
release backend uses it. Unknown until tried: whether the CZI TH4 accepts a
Widget Manager press like an operator press, the index and type of its drop
widget, whether its anti-misdrop interlock needs another widget set first,
and the mount position it reports on a SkyPort V3 adapter. Procedure:
`docs/CZI-DROP-KITS.md`.

---

## 14. Altitude datums for the take-off terrain tie and the laser result

`POSITION_FUSED.altitude` is documented as WGS-84 ellipsoid height. SRTM tiles
are heights above mean sea level. DropPoint ties the two together at the
take-off point, which removes the offset locally. Not confirmed: that the
fused altitude really is ellipsoidal on these aircraft (some DJI fields named
the same way are barometric), and which datum
`T_DjiCameraManagerLaserRangingInfo.altitude` uses. A constant offset between
the laser result and the fused altitude would shift every laser-ranged target
height by that amount.

**Check on hardware:** on the ground at a surveyed point, log
`POSITION_FUSED.altitude`; range a point of known height and log the laser
altitude; compare both with the survey.

---

## 15. Subscription topics on Matrice 4T and Matrice 400

DropPoint needs `QUATERNION`, `VELOCITY`, `POSITION_FUSED` and
`GIMBAL_ANGLES`, and uses `STATUS_FLIGHT`, `RTK_POSITION_INFO` and
`ALTITUDE_OF_HOMEPOINT` if present. The header does not say which topics each
aircraft publishes or at what maximum rate. `droppoint --selftest` on the
aircraft lists each topic as acquired or missing; record the result here for
both aircraft. If `GIMBAL_ANGLES` is missing, crosshair designation cannot
work and the gimbal attitude will have to come from somewhere else.
