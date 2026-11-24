# src/psdk/

This is the **only** directory in the repository allowed to include a DJI
Payload-SDK header. Everything here implements the `hal/i_*.h` interfaces
against real hardware; `core/`, `hal/`, `sim/`, `app/` and `hw/` never
reference anything in this directory or in DJI's headers.

## Status

**Not built by default** (`DROPPOINT_BUILD_PSDK=OFF`). The PSDK adapter
library compiles wherever the headers are available, but the executable,
the DJI sample platform sources and the `libpayloadsdk.a` link are
**Linux only** — the CMake file skips them with a message on other hosts.

None of this code has run against an aircraft. Every file is marked
`Status: unverified — requires hardware`. The `--selftest --no-link` and
`--version` paths are what CI and the install test exercise; everything
else awaits bench time.

## Layout

| File | HAL interface | PSDK surface used | Status |
|---|---|---|---|
| `app_registration.{h,cpp}` | — | `T_DjiUserInfo` wiring, reads credentials.yaml | Implemented + unit-tested (loader only) |
| `hal_uart.{h,cpp}` | — | `T_DjiHalUartHandler` — termios UART, device path + VID/PID from aircraft.yaml | Implemented; unverified |
| `platform_linux.{h,cpp}` | — | `DjiPlatform_Reg*Handler`: DJI osal/osal_fs/osal_socket + our UART or DJI manifold3 `hal_usb_bulk.c` per `psdk_link_mode` | Implemented; unverified |
| `telemetry_subscription.{h,cpp}` | `hal::ITelemetry` | `dji_fc_subscription.h`, polled `GetLatestValueOfTopic` | Implemented; unverified |
| `gimbal_psdk.{h,cpp}` | `hal::IGimbal` | GIMBAL_ANGLES topic via the telemetry poll (no Gimbal Manager) | Implemented; unverified |
| `rangefinder_camera_manager.{h,cpp}` | `hal::IRangefinder` | `DjiCameraManager_GetLaserRangingInfo` (advanced features only) + `NullRangefinder` | Implemented; unverified |
| `display_floating_window.{h,cpp}` | `hal::IDisplay` | `DjiWidgetFloatingWindow_ShowMessage` | Implemented; unverified |
| `audio_hms.{h,cpp}` | `hal::IAudio` | `DjiHmsCustomization_AlarmEnhancedCtrl` (Manifold 3 only) | Implemented; unverified |
| `widget_bridge.{h,cpp}` | — | `DjiWidget_*`: own Pilot 2 widget registration → `hw::OperatorInputs` | Implemented; unverified |
| `release_widget.{h,cpp}` | `hal::IRelease` | `DjiWidgetManager_SetWidgetState` — presses the drop kit's own widget (M400 only) | Implemented; unverified |
| `probe_widgets.{h,cpp}` | — | `DjiWidgetManager_*` file list/download/state subscribe for `--probe-widgets` | Implemented; unverified |
| `selftest.{h,cpp}` | — | report builder for `--selftest [--no-link]` | Implemented; unverified |
| `main_psdk.cpp` | — | `DjiCore_Init`/`ApplicationStart`, arg + path resolution, guidance loop wiring, sd_notify | Implemented; unverified |
| PWM servo release | `hal::IRelease` | **not PSDK** — sysfs PWM, lives in `src/hw/pwm_servo_release.*` | Implemented + unit-tested |
| Liveview markers | `hal::ILiveview` | none — liveview is an advanced feature; `hw::NullLiveview` always used | Intentionally absent |
| Pilot Pin Point target source | `hal::ITargetSource` | **does not exist in public PSDK** | Not implementable, see docs/OPEN-QUESTIONS.md #1 |

## Building for the aircraft

```bash
# On the companion computer or a Linux host with the PSDK checked out:
cmake -S droppoint -B build -DDROPPOINT_BUILD_PSDK=ON \
  -DDROPPOINT_PSDK_ROOT=/path/to/Payload-SDK -DCMAKE_BUILD_TYPE=Release
cmake --build build --target droppoint
```

`DROPPOINT_PSDK_ROOT` defaults to `third_party/psdk`, falling back to
`../psdk-upstream` when that checkout exists. On Linux the build compiles
DJI's own `osal*.c` and `manifold3/hal/hal_usb_bulk.c` straight from that
root (they are NOT copied into this repo) and links
`psdk_lib/lib/<arch>-linux-gnu-gcc/libpayloadsdk.a`.
