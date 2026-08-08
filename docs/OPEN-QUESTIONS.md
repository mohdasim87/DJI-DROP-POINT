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

**Answer: No public API to map custom PSDK actions to physical RC buttons.**

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
