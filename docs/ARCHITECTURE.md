# Architecture

**Status: verified (design and code structure) / unverified (runtime behaviour on real hardware).**

## The one rule that shapes everything

`src/core/` contains zero DJI headers, zero platform headers, and performs
no I/O. Every function is `structs in -> structs out`. This is what makes
the ballistics, geodesy, and designation math unit-testable on a laptop with
no aircraft, and it is enforced by `src/core/CMakeLists.txt` only linking
the C++ standard library.

Everything that needs the real world — telemetry, a gimbal, a rangefinder,
a speaker, a servo — is expressed as an abstract interface in `src/hal/`.
Two things implement those interfaces:

- `src/sim/` — a simulator, usable today, no hardware required.
- `src/psdk/` — the real PSDK-backed implementation, requires hardware and
  DJI credentials (see `docs/OPEN-QUESTIONS.md` for what's still stubbed).

`src/app/` never knows which one it's talking to.

```
                     +-------------------+
                     |     src/app/      |   state machine, guidance loop,
                     |  (no DJI headers) |   release manager, main()
                     +---------+---------+
                               |
                               v
                     +-------------------+
                     |     src/hal/      |   pure interfaces
                     | (no DJI headers)  |
                     +----+---------+----+
                          |         |
             +------------+         +------------+
             v                                    v
   +-------------------+               +-----------------------+
   |     src/sim/       |               |      src/psdk/        |
   | (no DJI headers)   |               | (the ONLY DJI headers) |
   +-------------------+               +-----------------------+

                     +-------------------+
                     |     src/core/     |   ballistics, geodesy,
                     | (no DJI headers,  |   projection, raycast,
                     |     no I/O)       |   terrain, gating, wind
                     +-------------------+
              (used directly by app/, sim/, and eventually psdk/)
```

## Data flow, one guidance-loop cycle (10 Hz)

1. `app::GuidanceLoop::Tick()` reads `hal::ITelemetry::GetAircraftState()`
   and `hal::IGimbal::GetAttitude()`.
2. If the operator pressed SET TARGET, the loop tries each
   `hal::ITargetSource` in priority order (spec section 3): Pilot Pin Point
   (unavailable, see `docs/OPEN-QUESTIONS.md` #1), gimbal crosshair, virtual
   cursor. Each ultimately calls `core::raycast::CastPixelToGround`.
3. Any fresh laser-rangefinder reading updates `core::terrain::DatumBias`
   (spec section 3.4 step 7).
4. If a target is latched, `core::ballistics::SolveInverse` computes the
   release point/time and `core::ballistics::IntegrateForward` produces the
   predicted impact point (spec 6.4).
5. `core::gating::Evaluate` checks every hard/soft gate (spec section 7)
   against config-driven thresholds — never hardcoded.
6. `app::MissionState` advances the shared state machine (spec 8.1);
   `app::ReleaseManager` drives the countdown cues and, in auto mode, the
   actual `hal::IRelease::Fire()` call, re-checking every hard gate at loop
   rate the entire time (spec 8.3) and treating the hardware abort input as
   an unconditional override (spec 8.3/8.4).
7. The loop renders the floating window text, audio cues, and (if
   available) liveview markers.

## Why the designation sources share one solver

Sources A (Pilot Pin Point) and C (virtual cursor) both need "given a pixel,
where is the ground": `core::raycast::CastPixelToGround`. Source B (gimbal
crosshair) is the degenerate case where the pixel is always the image
centre. Building all three around one function means the mathematically
hard part (spec 3.4's seven-step pixel-to-ground solve) is written and
tested once (`tests/test_projection_raycast.cpp`, regression vectors 7-10),
and if DJI ever exposes Pilot's Pin Point API, `PinPointTargetSource` is a
thin adapter, not a new solver.

## Why the release manager, not PSDK, owns Fire()

`hal::IRelease` is the single safety-critical interface in the system. Only
`app::ReleaseManager` is allowed to call `Fire()`; it does so from exactly
one place in `Update()`, guarded by the mission state, the live gate
evaluation, the auto-mode target-freshness check, and the hardware-abort
check, in that order, every single cycle. This is deliberate: the state
machine (`app::MissionState`) tracks *what the operator asked for*, while
the release manager is the only thing that decides *whether to actually
fire right now*, so a bug in one cannot silently bypass the other.

## Config

`util::config::LoadAll` loads and cross-validates the four YAML files
(`aircraft.yaml`, `payloads.yaml`, `lenses.yaml`, `system.yaml`) at startup,
via `yaml-cpp` (the only third-party dependency in the tree). Every
threshold gating/ballistics/etc. reads comes from these files; nothing is
hardcoded in `core/` or `app/`, per spec section 7's closing requirement.

## Testing strategy

- `tests/` exercises `core/` exclusively, with zero HAL/DJI/OS dependency —
  see `tests/test_framework.h` for the (deliberately dependency-free) test
  harness and `docs/PSDK-RESEARCH.md`-adjacent regression vectors in
  `tests/test_ballistics.cpp` and `tests/test_projection_raycast.cpp`.
- `src/sim/` is what stands in for hardware in `app/`-level testing and in
  `droppoint_sim_main --sim <scenario>`.
- Release-safety properties (fail-safe on crash/disarm/abort, single-shot
  fire, hardware-abort-overrides-everything) are properties of
  `app::ReleaseManager` and are testable today against `sim::SimRelease`
  without any hardware — see the acceptance criteria in spec section 17 and
  `docs/OPEN-QUESTIONS.md` for what remains hardware-only.
