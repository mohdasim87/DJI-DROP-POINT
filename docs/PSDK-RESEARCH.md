# PSDK Research (Phase 0)

**Status: verified against public headers/docs — requires hardware to confirm runtime behaviour.**

Source: `dji-sdk/Payload-SDK` cloned at the commit checked out into
`psdk-upstream/` during this research pass (shallow clone, `master`, PSDK
release **v3.16.0** per repository state). All file paths below are relative
to that clone unless a `developer.dji.com` URL is given. See
`docs/OPEN-QUESTIONS.md` for the priority Pin Point question and everything
else this research could not resolve from public sources.

---

## 1. Build process, toolchain, dependencies (Linux aarch64)

- Root `CMakeLists.txt`: detects host via `uname -m` and selects a
  prebuilt static library path — `psdk_lib/lib/aarch64-linux-gnu-gcc/` for
  aarch64, `psdk_lib/lib/x86_64-linux-gnu-gcc/` for x86_64. PSDK ships as a
  **prebuilt static library** (`libpayloadsdk.a`), not source, for the core
  protocol/transport layer.
- Reference target: `samples/sample_c/platform/linux/manifold3/CMakeLists.txt`
  sets `CMAKE_C_COMPILER=aarch64-linux-gnu-gcc`,
  `CMAKE_CXX_COMPILER=aarch64-linux-gnu-g++`, C flags
  `-pthread -std=gnu99 -D_GNU_SOURCE`, C++ flags `-std=c++11 -pthread`, and
  links `libpayloadsdk.a` plus system `m` and `dl`.
- Cross-compilation toolchain: install `gcc-aarch64-linux-gnu` /
  `g++-aarch64-linux-gnu` on an x86_64 Ubuntu build host (this is the
  standard path DJI's own sample uses; no other build dependency is declared
  in the sample `CMakeLists.txt`).
- DropPoint's own `CMakeLists.txt` keeps `core/`, `hal/`, `sim/`, `app/` and
  the offline tools buildable with the **host** compiler and no PSDK
  dependency at all (`DROPPOINT_BUILD_PSDK=OFF` by default). Only
  `src/psdk/` requires the aarch64 cross toolchain and the vendored PSDK
  static library, consistent with the project's core/ isolation rule.

## 2. Application registration

Struct `T_DjiUserInfo`, `psdk_lib/include/dji_core.h`:

```c
typedef struct {
    char appName[32];
    char appId[16];
    char appKey[32];
    char appLicense[512];
    char developerAccount[64];
    char baudRate[7];
} T_DjiUserInfo;
```

Passed to `DjiCore_Init(const T_DjiUserInfo *userInfo)`. All five string
fields (`appName`, `appId`, `appKey`, `appLicense`, `developerAccount`) come
from a registered application at `developer.dji.com/user/apps/#all` and
**cannot be fabricated** — they are exactly the credential slots this
project leaves blank (see `config/credentials.example.yaml` and
`docs/DEPLOYMENT.md`). `baudRate` is a decimal string (e.g. `"460800"`).

Sample placeholders, `samples/sample_c/platform/linux/manifold3/application/dji_sdk_app_info.h`:
`USER_APP_NAME`, `USER_APP_ID`, `USER_APP_KEY`, `USER_APP_LICENSE`,
`USER_DEVELOPER_ACCOUNT`, `USER_BAUD_RATE` — same five credential fields plus
baud, confirming the struct above is the complete registration surface.

Additional identity-related calls found in `dji_core.h`:
`DjiCore_SetAlias` (≤31 chars, Pilot display name),
`DjiCore_SetFirmwareVersion`, `DjiCore_SetSerialNumber` (≤32 bytes),
`DjiCore_SetSdkDeviceType`.

## 3. Data Subscription topics

`psdk_lib/include/dji_fc_subscription.h` enumerates topics as
`DJI_FC_SUBSCRIPTION_TOPIC_*`. Relevant to DropPoint:

| Purpose | Topic | Notes |
|---|---|---|
| Fused position | `..._POSITION_FUSED` | primary position source |
| Attitude | `..._QUATERNION` | FRD→NED rotation |
| Velocity | `..._VELOCITY` | ground-fixed NEU |
| GPS position/velocity/detail | `..._GPS_POSITION`, `..._GPS_VELOCITY`, `..._GPS_DETAILS`, `..._GPS_SIGNAL_LEVEL` | |
| RTK | `..._RTK_POSITION`, `..._RTK_VELOCITY`, `..._RTK_POSITION_INFO`, `..._RTK_CONNECT_STATUS` | FIX/FLOAT state lives in `..._RTK_POSITION_INFO` |
| Barometric altitude | `..._ALTITUDE_BAROMETER` | ICAO-model pressure altitude, up to 200 Hz |
| Fused altitude / height | `..._ALTITUDE_FUSED`, `..._HEIGHT_FUSION`, `..._HEIGHT_RELATIVE` | |
| Gimbal | `..._GIMBAL_ANGLES`, `..._GIMBAL_STATUS`, `..._GIMBAL_CONTROL_MODE`, `..._THREE_GIMBAL_DATA` | multi-gimbal aircraft (M300) expose per-position variants |
| RC | `..._RC`, `..._RC_WITH_FLAG_DATA` | up to 100 Hz / 50 Hz respectively; stick/switch positions only, no button-event callback (see Open Question 3) |
| Flight anomaly | `..._FLIGHT_ANOMALY` | carries `strongWindLevel1`/`strongWindLevel2` boolean flags |

**Wind topic: does not exist.** Confirmed by exhaustive search of
`dji_fc_subscription.h` for "wind" — the only hits are the two boolean
anomaly flags above, inside `T_DjiFcSubscriptionFlightAnomaly`. No speed or
direction measurement is published. This is recorded as the answer to the
build spec's explicit Phase 0 question and drives `core/wind.h`'s design
(the hover-attitude estimator is the primary wind source, not a fallback of
last resort). See `docs/OPEN-QUESTIONS.md` #2.

## 4. Widget API

`psdk_lib/include/dji_widget.h`:

```c
typedef enum {
    DJI_WIDGET_TYPE_BUTTON = 1,
    DJI_WIDGET_TYPE_SWITCH = 2,
    DJI_WIDGET_TYPE_SCALE = 3,          // range bar, 0-100
    DJI_WIDGET_TYPE_LIST = 4,
    DJI_WIDGET_TYPE_INT_INPUT_BOX = 5,
} E_DjiWidgetType;
```

- **Action-bar limit: 8 custom widgets** — DJI tutorial page
  `developer.dji.com/doc/payload-sdk-tutorial/en/function-overview/basic-function/custom-widget.html`:
  "supports setting up to 8 custom widgets." This directly constrains
  section 9's widget list (12 conceptual controls); DropPoint's widget
  layout groups CURSOR X/Y as a single Source-C sub-panel and moves
  low-frequency settings (SURFACE, TEMP, CALIBRATE) into the bench dashboard
  or a LIST widget with sub-selection, to fit 8 slots. See
  `docs/OPERATION.md` for the exact final mapping.
- **RC hardware button mapping: no dedicated binding API** (see Open
  Question 3). Only the read-only RC topic exists.
- **Floating window:** `DjiWidgetFloatingWindow_ShowMessage(const char *str)`,
  max length `DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN = 255` bytes, bandwidth
  capped at 2 KB/s. State via `DjiWidgetFloatingWindow_GetChannelState`.
- **TTS/speaker:** `DjiWidget_RegSpeakerHandler` registers a
  `T_DjiWidgetSpeakerHandler` with `SetWorkMode`
  (`DJI_WIDGET_SPEAKER_WORK_MODE_TTS` / `..._VOICE`), `SetPlayMode`
  (single / loop), `SetVolume`, `StartPlay`/`StopPlay`, and
  `ReceiveTtsData`/`ReceiveVoiceData` callbacks that stream audio buffer
  chunks to the app. The audio countdown (spec 9) is implemented against
  this handler in `src/psdk/audio_speaker.*`.

## 5. Camera Manager

`psdk_lib/include/dji_camera_manager.h`:

- **Laser rangefinder:** `DjiCameraManager_GetLaserRangingInfo(position, T_DjiCameraManagerLaserRangingInfo*)`,
  documented update rate **up to 5 Hz**. Struct fields: `longitude`,
  `latitude` (float64 degrees), `altitude` (int32, 0.1 m units), `distance`
  (int32, 0.1 m units), `screenX`/`screenY` (int16, 0.1% of screen),
  `enable_lidar` (bool), `exception` (uint8 status code). This is the
  measurement Source B and the Step 7 LRF datum correction consume — note
  its 5 Hz ceiling directly bounds how often the DEM bias in
  `core/terrain.*` can be refreshed from a live rangefinder read.
- **Live zoom ratio:** `DjiCameraManager_GetOpticalZoomParam(position, T_DjiCameraManagerOpticalZoomParam*)`
  → `{ currentOpticalZoomFactor, maxOpticalZoomFactor }` (both float32).
  This is the `zoom_ratio` consumed by `core/raycast.cpp` Step 1 and
  `core/projection.cpp`.
- Gimbal attitude is **not** read through Camera Manager; see below.

`psdk_lib/include/dji_gimbal_manager.h`:

- `T_DjiGimbalManagerRotation { rotationMode, pitch, roll, yaw (float32, degrees), time }`
  is the **command** struct for driving the gimbal, not a telemetry read.
  Gimbal *attitude feedback* for our raycast solver comes from the Data
  Subscription topics `..._GIMBAL_ANGLES` / `..._THREE_GIMBAL_DATA` (section
  3 above), not from Camera Manager. `dji_gimbal_manager.h` is used by
  `src/psdk/` only if/when the crosshair-slew control path
  (`DjiGimbalManager_Rotate`) is implemented for Source B; reading current
  gimbal pose for the solver always goes through subscription topics.

## 6. Liveview API

`psdk_lib/include/dji_liveview.h`:

- `DjiLiveview_SendAiMetaToPilot(T_DjiLiveViewStandardMetaData*)` —
  **Manifold 3 only**, per an explicit comment in the header. This is a hard
  platform constraint: the video marker (spec section 9) only functions on
  Manifold-3-class companion computers, and the system must remain fully
  usable without it, which is why `i_liveview.h` ships a null
  implementation by design, not just for testing.
- Struct layout (`T_DjiLiveViewStandardMetaData`, `T_DjiLiveViewBoundingBox`,
  `T_DjiLiveViewTarget2dBox`), all `__attribute__((packed))`:

```c
typedef struct {
    uint16_t cx;        // box centre X, units of 1/10000 * screen width
    uint16_t cy;        // box centre Y, units of 1/10000 * screen height
    uint16_t w;          // box width,  units of 1/10000 * screen width
    uint16_t h;          // box height, units of 1/10000 * screen height
    uint32_t distance;   // millimetres
} T_DjiLiveViewTarget2dBox;

typedef struct {
    uint16_t id;
    uint8_t  type;    // E_DjiLiveViewTargetObjectType, or user-defined
    uint8_t  state;   // E_DjiLiveViewTargetObjectState
    T_DjiLiveViewTarget2dBox box;
} T_DjiLiveViewBoundingBox;

typedef struct {
    uint8_t boxCount;
    T_DjiLiveViewBoundingBox boxData[1];  // flexible array
} T_DjiLiveViewStandardMetaData;
```

**Settable fields:** `cx`, `cy`, `w`, `h` (box geometry, normalised
0–10000 fixed point), `distance` (mm), `id`, `type`, `state`, `boxCount`.
**No colour field and no free-text label field exist in this struct.**
Per-box label *text* is instead registered out-of-band via
`DjiLiveview_RegUserAiTargetLableList()` — a fixed label table indexed by
`type`/`id`, not an arbitrary per-frame string. **This means the spec's
"label with miss distance and state" requirement cannot be satisfied with
live-updating numeric text through this channel** — miss distance changes
every frame and a label-list lookup is static. DropPoint therefore:
  - uses `type`/`state` to select from a small fixed set of pre-registered
    labels (e.g. "TARGET", "IMPACT", "CURSOR") rather than embedding numbers, and
  - puts the numeric, fast-changing state (miss distance, countdown) in the
    floating window and audio channels instead, exactly as spec section 9
    already requires as the fallback when "the metadata struct proves too
    constrained." That condition is met, so `src/psdk/liveview_marker.cpp`
    implements box-position-only markers with static labels, and the
    floating window/audio remain the source of truth for numeric state.
- `E_DjiLiveViewTargetObjectType` includes generic values usable for our
  markers without misusing person/face/car semantics:
  `DJI_LIVEVIEW_OBJ_TYPE_UNKNOWN = 1`, `..._BEACON = 20`, `..._GPS = 30`,
  `..._MOVING_TARGET = 34`. DropPoint uses `BEACON` for the latched target,
  `GPS` for the predicted impact point, and `MOVING_TARGET` for the live
  virtual cursor, to get distinguishable default rendering without
  registering custom types. **No person/face types are ever used**, per hard
  constraint 5.

## 7. GPIO / PWM / UART

- **No `dji_gpio.h` or `dji_pwm.h` exists anywhere in the public headers.**
  The only GPIO reference in the entire SDK is a note in
  `dji_power_management.h` about GPIO12 needing to be pulled down before
  requesting high-voltage E-Port output on Manifold 3 — unrelated to
  general-purpose control. **The release servo is driven entirely outside
  PSDK**, through the companion computer's own OS GPIO/PWM (Linux sysfs or
  an I2C PWM controller). See Open Question 4.
- **UART** is exposed as a HAL registration surface the *application*
  implements and hands to PSDK, not as a control API for arbitrary external
  UART devices: `dji_platform.h`'s `T_DjiHalUartHandler` (`UartInit`,
  `UartDeInit`, `UartWriteData`, `UartReadData`, `UartGetStatus`,
  `UartGetDeviceInfo`), registered via `DjiPlatform_RegHalUartHandler`. This
  is the plumbing PSDK itself uses to talk to the aircraft over the
  companion-computer's UART port — it is not a general "talk to any UART
  peripheral" facility, so it is irrelevant to driving the release
  mechanism and is only referenced in `src/psdk/platform_linux.*` for the
  aircraft link itself.

## 8. PSDK link mode macros

`samples/sample_c/platform/linux/manifold3/application/dji_sdk_config.h`:

```c
#define DJI_USE_ONLY_UART                  (0)
#define DJI_USE_UART_AND_USB_BULK_DEVICE   (1)
#define DJI_USE_UART_AND_NETWORK_DEVICE    (2)
#define DJI_USE_ONLY_USB_BULK_DEVICE       (3)
#define DJI_USE_ONLY_NETWORK_DEVICE        (4)
```

Confirms the build spec's three original macros exactly, plus the two newer
`ONLY_*` modes referenced in spec 5.3. `CONFIG_HARDWARE_CONNECTION` selects
the active mode at compile time in the sample; DropPoint exposes the same
selection through a CMake cache variable
(`DROPPOINT_PSDK_LINK_MODE`) forwarded to a generated
`dji_sdk_config.h`, so switching aircraft/link mode does not require hand-
editing vendored PSDK sample files.

## 9. DPK packaging

Confirmed required for Manifold 3 distribution: `tools/build_dpk/build_dpk.sh`
(Python 3 + `dpkg` dependency) packages a static-only build plus an
`app.json` manifest (`user_app_id`, `firmware_version`, `platform`,
localized `name_*`/`description_*`/`maintainer_*` fields, optional
`is_ai_rendering`) into a `.dpk`. The Manifold 3 sample's `CMakeLists.txt`
invokes it as a post-build step automatically. Upstream issue
`dji-sdk/Payload-SDK#431` confirms **DPK supports only statically linked
dependencies** — no bundled `.so` files — which is why DropPoint statically
links SQLite and its DEM reader for the Manifold 3 / DPK build target (see
`docs/DEPLOYMENT.md`).

Whether M400/M4/M4D-class computers require DPK specifically (as opposed to
a plain `.deb`) for the liveview overlay is **not confirmed** by this
research pass — no M400/M4/M4D sample exists in the cloned repository. See
Open Question 5.

---

## Summary table for quick reference

| Capability | Available via PSDK? | API |
|---|---|---|
| Pilot Pin Point coordinate | **No** | — (Mobile SDK only) |
| Wind speed/direction | **No** | — (anomaly flags only) |
| RC custom button binding | **No** | — (poll RC topic only) |
| GPIO/PWM for release servo | **No** | — (OS-level, outside PSDK) |
| Laser rangefinder | Yes | `DjiCameraManager_GetLaserRangingInfo`, ≤5 Hz |
| Live zoom ratio | Yes | `DjiCameraManager_GetOpticalZoomParam` |
| Gimbal attitude (telemetry) | Yes | Data Subscription `..._GIMBAL_ANGLES` etc. |
| Gimbal commanded rotation | Yes | `DjiGimbalManager_Rotate` |
| Video AI marker overlay | Yes, Manifold 3 only | `DjiLiveview_SendAiMetaToPilot` (numeric labels not supported) |
| Floating window text | Yes | `DjiWidgetFloatingWindow_ShowMessage`, ≤255 bytes, 2 KB/s |
| TTS / audio countdown | Yes | `DjiWidget_RegSpeakerHandler` |
| Widgets | Yes, ≤8 in action bar | `dji_widget.h` |
