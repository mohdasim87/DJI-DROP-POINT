# src/psdk/

This is the **only** directory in the repository allowed to include a DJI
Payload-SDK header. Everything here implements the `hal/i_*.h` interfaces
against real hardware; `core/`, `hal/`, `sim/`, and `app/` never reference
anything in this directory or in DJI's headers.

## Status

**Not built by default** (`DROPPOINT_BUILD_PSDK=OFF`). This target requires:

1. The `aarch64-linux-gnu-gcc`/`g++` cross toolchain (or building natively on
   the aarch64 companion computer).
2. The vendored Payload-SDK static library, expected at
   `third_party/psdk/psdk_lib/` (not committed to this repository — see
   `docs/DEPLOYMENT.md` for how to vendor it during the build).
3. Real DJI developer credentials in `config/credentials.yaml` (never
   committed; see `config/credentials.example.yaml`).

None of the above is available in this development environment, per hard
constraints 2 and 3 in the build spec. The files here are the real
implementation shape against the headers found in Phase 0
(`docs/PSDK-RESEARCH.md`), but they have never run against an aircraft.
Every function is marked with a `Status: unverified — requires hardware`
comment.

## Layout

| File | HAL interface | PSDK surface used | Status |
|---|---|---|---|
| `release_gpio.{h,cpp}` | `hal::IRelease` | **not PSDK** — Linux sysfs PWM directly, see docs/OPEN-QUESTIONS.md #4 | Implemented; unverified without hardware |
| `app_registration.{h,cpp}` | — | `T_DjiUserInfo` credential struct wiring, reads `config/credentials.yaml` | Implemented (loader + tests); `DjiCore_Init` call is a documented TODO |
| `telemetry_subscription.*` | `hal::ITelemetry` | `dji_fc_subscription.h` topics | **Not yet started** — tracked here, not stubbed |
| `gimbal_psdk.*` | `hal::IGimbal` | `dji_gimbal_manager.h`, subscription `..._GIMBAL_ANGLES` | **Not yet started** |
| `rangefinder_camera_manager.*` | `hal::IRangefinder` | `DjiCameraManager_GetLaserRangingInfo` | **Not yet started** |
| `display_floating_window.*` | `hal::IDisplay` | `DjiWidgetFloatingWindow_ShowMessage` | **Not yet started** |
| `audio_speaker.*` | `hal::IAudio` | `DjiWidget_RegSpeakerHandler` | **Not yet started** |
| `liveview_marker.*` | `hal::ILiveview` | `DjiLiveview_SendAiMetaToPilot` (Manifold 3 only) | **Not yet started** |
| `platform_linux.*` | — | `dji_platform.h` HAL registration (UART/USB-bulk transport) | **Not yet started** |
| Pilot Pin Point target source | `hal::ITargetSource` | **does not exist in public PSDK** | Not implementable, see docs/OPEN-QUESTIONS.md #1 |

`release_gpio.*` needs no PSDK headers at all (docs/OPEN-QUESTIONS.md #4)
and is directly testable on any Linux box with sysfs PWM permissions and a
bench servo — it is implemented in full. `app_registration.cpp` is also
buildable and tested on Windows (it only uses yaml-cpp and the filesystem). The remaining `ITelemetry`/`IGimbal`/`IRangefinder`/`IDisplay`/
`IAudio`/`ILiveview` PSDK adapters follow directly from the API surface
documented in `docs/PSDK-RESEARCH.md`, but writing and "testing" them
against a static PSDK library with fabricated credentials would not
actually verify anything beyond compilation, so per hard constraint 4 they
are left as an explicit next step for whoever holds the DJI developer
account and hardware, rather than committed as untested code that looks
more finished than it is. The interfaces they must implement
(`hal/i_telemetry.h` etc.) are complete and already exercised end-to-end by
`sim/sim_hal.cpp`.
