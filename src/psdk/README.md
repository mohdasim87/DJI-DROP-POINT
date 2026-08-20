# src/psdk/

This is the **only** directory in the repository allowed to include a DJI
Payload-SDK header. Everything here implements the `hal/i_*.h` interfaces
against real hardware; `core/`, `hal/`, `sim/`, and `app/` never reference
anything in this directory or in DJI's headers.

## Status

**Not built by default** (`DROPPOINT_BUILD_PSDK=OFF`). This target requires:

1. The `aarch64-linux-gnu-gcc`/`g++` cross toolchain (or building natively on
   the aarch64 companion computer).
2. The vendored Payload-SDK static library, expected at the path set by
   `DROPPOINT_PSDK_ROOT` (default `../psdk-upstream`). On Linux the CMake build
   links the correct `libpayloadsdk.a` for the target processor automatically.
3. Real DJI developer credentials in `config/credentials.yaml` (never
   committed; see `config/credentials.example.yaml`).

None of the above is available in this development environment, per hard
constraints 2 and 3 in the build spec. The files here compile against the real
PSDK headers found in `D:\DJI Payload\psdk-upstream`, but they have never run
against an aircraft. Every function is marked with a
`Status: unverified — requires hardware` comment.

## Layout

| File | HAL interface | PSDK surface used | Status |
|---|---|---|---|
| `release_gpio.{h,cpp}` | `hal::IRelease` | **not PSDK** — Linux sysfs PWM directly, see docs/OPEN-QUESTIONS.md #4 | Implemented; unverified without hardware |
| `app_registration.{h,cpp}` | — | `T_DjiUserInfo` credential struct wiring, reads `config/credentials.yaml` | Implemented (loader + tests); `DjiCore_Init` wiring is in `main_psdk.cpp` |
| `telemetry_subscription.*` | `hal::ITelemetry` | `dji_fc_subscription.h` topics | Implemented; unverified |
| `gimbal_psdk.*` | `hal::IGimbal` | `dji_gimbal_manager.h`, subscription `..._GIMBAL_ANGLES` | Implemented; unverified |
| `rangefinder_camera_manager.*` | `hal::IRangefinder` | `DjiCameraManager_GetLaserRangingInfo` | Implemented; unverified |
| `display_floating_window.*` | `hal::IDisplay` | `DjiWidgetFloatingWindow_ShowMessage` | Implemented; unverified |
| `audio_speaker.*` | `hal::IAudio` | `DjiWidget_RegSpeakerHandler` | Implemented; unverified |
| `liveview_marker.*` | `hal::ILiveview` | `DjiLiveview_SendAiMetaToPilot` (Manifold 3 only) | Implemented; unverified |
| `platform_linux.*` | — | `dji_platform.h` HAL registration (UART/USB-bulk transport) | Implemented; unverified |
| `main_psdk.cpp` | — | `DjiCore_Init`, `DjiCore_ApplicationStart` | Implemented; unverified |
| Crosshair target source | `hal::ITargetSource` | Source B (gimbal centre raycast) | Implemented in `app/`; unverified |
| Pilot Pin Point target source | `hal::ITargetSource` | **does not exist in public PSDK** | Not implementable, see docs/OPEN-QUESTIONS.md #1 |

`release_gpio.*` needs no PSDK headers at all (docs/OPEN-QUESTIONS.md #4)
and is directly testable on any Linux box with sysfs PWM permissions and a
bench servo — it is implemented in full. `app_registration.cpp` is also
buildable and tested on Windows (it only uses yaml-cpp and the filesystem).
The remaining adapters compile against the PSDK headers on Windows
(`DROPPOINT_BUILD_PSDK=ON`), but because the PSDK static library is a Linux
archive they cannot be linked into an executable here. On a Linux/aarch64
build machine the same source links into the real `droppoint` binary.

The interfaces they implement (`hal/i_telemetry.h` etc.) are complete and
already exercised end-to-end by `sim/sim_hal.cpp`.

## Building for the aircraft

```bash
# On the companion computer or a Linux cross-build host with the PSDK vendored
cmake -S droppoint -B build-aarch64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
  -DDROPPOINT_BUILD_PSDK=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-aarch64 --target droppoint
```
