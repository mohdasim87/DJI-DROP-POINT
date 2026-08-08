# Setup

**Status: verified (build steps on a dev machine) / unverified (the PSDK
cross-build and the on-hardware run).**

This document takes a competent field engineer with no PSDK experience from
a fresh clone to a running dev-machine simulation. For the on-aircraft
install procedure, see `docs/DEPLOYMENT.md`; for physically wiring the
companion computer, see `docs/CONNECTION.md`.

## 1. Prerequisites

- CMake >= 3.16
- A C++17 compiler: MSVC 2019+, GCC 9+, or Clang 10+
- Git and network access (to fetch `yaml-cpp` via CMake `FetchContent` at
  configure time — this happens only on the *build* machine, never on the
  deployed aircraft package, which vendors the built library statically)
- Python 3 (only needed for `tools/build_dpk` when building the Manifold 3
  DPK package, and for the `--sim` reference math checks in
  `scripts/_proto_ballistics.py`)

For the PSDK (real hardware) build target specifically, additionally:

- `gcc-aarch64-linux-gnu` / `g++-aarch64-linux-gnu` cross toolchain (or
  build natively on the aarch64 companion computer)
- The Payload-SDK repository vendored under `third_party/psdk/` (not
  committed here — clone `https://github.com/dji-sdk/Payload-SDK` yourself;
  see `docs/PSDK-RESEARCH.md` for what was found in it)
- A registered DJI developer application and, for flight, real hardware

## 2. Build (core, tests, simulator, tools — no PSDK, no hardware)

```
git clone <this repo> droppoint
cd droppoint
cmake -S . -B build
cmake --build build --config Release
```

This builds:

- `droppoint_core_tests` — the full unit test suite (48 tests as of this
  writing, including all 11 regression vectors from spec section 6.5)
- `droppoint_sim_main` — the application running against the simulator
- `droppoint_calibrate`, `droppoint_replay`, `droppoint_lenscal` — the
  offline tools

## 3. Run the tests

```
ctest --test-dir build -C Release --output-on-failure
```

or run the test binary directly:

```
./build/tests/Release/droppoint_core_tests   # Linux/macOS path shown; Windows: build\tests\Release\droppoint_core_tests.exe
```

Expect all tests to pass. If a regression vector fails, **do not adjust the
expected value** — investigate the discrepancy (spec section 19's closing
rule) and report it; the vectors come from a validated reference
implementation.

## 4. Run the simulator

```
./build/src/app/Release/droppoint_sim_main --sim crosswind-5ms
```

Available scenarios: `hover`, `run-in`, `crosswind-3ms`, `crosswind-5ms`,
`crosswind-8ms`, `gusty-wind`, `wind-shear`, `sloping-terrain`,
`gnss-degradation`, `rtk-loss`, `telemetry-dropout`, `gimbal-slew`,
`mis-designated-target`.

This runs the entire guidance loop — designation, ballistics, gating, the
release state machine — with no aircraft, exactly as it would run against
real telemetry, just fed by `src/sim/` instead of `src/psdk/`.

## 5. Run the self-test against your config

```
./build/src/app/Release/droppoint_sim_main --selftest --config config
```

Validates `config/*.yaml` and reports which payload profiles are
calibrated. The PSDK-backed checks (link status, credential validity,
subscription topics, rangefinder, PPS) are skipped in this build and
labelled as such — they require the PSDK target and real hardware.

## 6. Building the PSDK (hardware) target

Not possible in this environment (hard constraint 2/3: no hardware, no
credentials, no completed PSDK registration). The shape of what this build
looks like:

```
cmake -S . -B build-psdk \
  -DDROPPOINT_BUILD_PSDK=ON \
  -DCMAKE_TOOLCHAIN_FILE=<aarch64 cross-toolchain file> \
  -DDROPPOINT_PSDK_ROOT=third_party/psdk
cmake --build build-psdk --config Release
```

See `src/psdk/README.md` for exactly which files are implemented today
versus left as a documented next step, and `docs/OPEN-QUESTIONS.md` for
every specific unknown a human with hardware needs to resolve.
