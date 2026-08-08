# Calibration

**Status: unverified — requires hardware and real flight/bench data.**
Every procedure below is implementable with the tools in this repository
today; none has been run against a real payload or lens.

## 1. Ballistic calibration: still-air drops at 30/60/90 m

Fly (or bench-drop from a controlled rig) each payload profile at three
release heights — 30 m, 60 m, and 90 m AGL — with **five drops per
height** (spec section 11's `tools/calibrate` refuses fewer than five drops
total; five per height gives a healthier fit across the full envelope).
Conditions: still air if at all possible — wind contributes its own error
source that the fit will otherwise absorb into the CdA estimate.

For each drop, record:

- Release AGL, ground speed, heading, ambient temperature
- Measured wind speed/bearing (even in "still air," record what your
  station reads)
- The measured impact point's East/North offset from the intended release
  point, and the measured fall time if you can time it (a stopwatch on the
  release-to-impact interval is adequate)

Assemble these into a CSV matching `tools/calibrate/main.cpp`'s expected
columns:

```
mass_kg,cda_nominal,release_agl_m,gs_mps,heading_deg,wind_mps,wind_from_deg,temp_c,measured_dE_m,measured_dN_m,measured_fall_time_s
```

Run:

```
./build/tools/calibrate/Release/droppoint_calibrate drops.csv
```

The tool reports the residual RMS before/after a fitted CdA scale factor
and mechanism delay, and prints the exact `payloads.yaml` fields to update.
**Apply them by hand** (the tool deliberately does not auto-write
`payloads.yaml` — a human should look at the residual and the fit before it
becomes flight configuration) and set `calibrated: true` with today's date.

## 2. Lens calibration

Two paths:

### 2a. Manual entry (implemented, no new dependency)

If you already have measured intrinsics (from an external OpenCV/MATLAB
checkerboard calibration run, or a previously validated value), write them
with:

```
./build/tools/lenscal/Release/droppoint_lenscal \
  --aircraft M300_RTK --source H20T_wide \
  --width 1920 --height 1080 --hfov-deg 82.1 --vfov-deg 51.9 \
  --k1 -0.021 --k2 0.004 --p1 0.0 --p2 0.0 \
  --calibrated 2026-01-15 \
  --out lens_fragment.yaml
```

Append the output to `config/lenses.yaml`'s `lenses:` list.

### 2b. OpenCV checkerboard procedure (external, not vendored here)

`tools/lenscal` does not itself run an OpenCV checkerboard calibration
(see `tools/lenscal/main.cpp`'s header comment and
`docs/OPEN-QUESTIONS.md` for why OpenCV is not vendored in this
repository). Standard procedure using any OpenCV calibration script or
MATLAB's Camera Calibrator:

1. Print a checkerboard of known square size (e.g. 25 mm squares, 9x6
   internal corners).
2. Record 15-20 stills of the checkerboard filling different regions of the
   frame, at the zoom level (1x) you intend to calibrate, from the actual
   camera/stream source you'll fly with.
3. Run an OpenCV `calibrateCamera` script against the images to obtain the
   focal lengths (convert to HFOV/VFOV using the sensor width/height in
   pixels), principal point, and `k1,k2,k3,p1,p2` distortion coefficients.
4. Feed the results into `droppoint_lenscal` (2a above) to produce the
   `lenses.yaml` fragment, with `--calibrated <today's date>`.

**Never present datasheet FOV as measured truth** (spec 6.7) — the
placeholders shipped in `config/lenses.yaml` are explicitly marked
uncalibrated and the self-test surfaces that state until this procedure has
been run.

## 3. Video-latency sweep (timestamp alignment)

Spec section 6.7 calls this "a first-class feature — almost every unstable
overlay is a timestamp failure, not a maths error."

Procedure:

1. Point the camera at a fixed, high-contrast ground feature.
2. Command a slow, steady yaw sweep (gimbal or aircraft, per your
   `gimbal_yaw_source` setting).
3. Watch the liveview marker (or, without the marker channel, log the
   projected pixel coordinate) tracking that fixed feature.
4. Adjust `video_telemetry_offset_ms` in `config/system.yaml` up/down until
   the marker stops sliding relative to the feature during the sweep.
5. Record the converged value and the date in this file's revision history
   (or a project log) — it is a property of the specific
   camera/companion-computer/link combination, not a universal constant.

`tests/` includes a scripted-yaw timestamp alignment test
(`docs/../tests/` — see spec section 14) validating the ring-buffer
interpolation logic itself; the sweep above calibrates the *offset value*
for your specific hardware, which no unit test can determine.
