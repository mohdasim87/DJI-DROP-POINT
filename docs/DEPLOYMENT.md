# Deployment

**Status: unverified — requires hardware for the actual install/upgrade/
rollback cycle.** The packaging shape and scripts below are complete and
buildable; they have not run on a real companion computer.

## Read this before packaging: there is no APK

The customer's original ask mentioned deploying an APK onto the DJI RC
controller. **That is not how this installs, and DropPoint does not build
one.**

- PSDK applications are Linux C/C++ binaries running on the **companion
  computer**, not the RC controller. There is no APK involved anywhere in
  this architecture.
- Nothing installs on the RC controller. DJI Pilot 2 is already there and
  is the only app running on it — hard constraint 1 requires it to stay
  that way.
- The controller screen still shows DropPoint's interface: widgets, the
  floating window, audio, and the video marker are all *rendered by Pilot*
  from data the companion computer publishes over the normal aircraft
  radio link (see `docs/CONNECTION.md`'s topology diagram). Nothing is
  pushed to or installed on the controller's filesystem.
- The only route to a real APK on the RC controller is DJI Mobile SDK,
  which requires replacing Pilot. That is explicitly ruled out by hard
  constraint 1 and is not built, offered, or hinted at anywhere in this
  repository.

**One package, yes. An APK, no.** The package below targets the companion
computer only.

## What the package contains

Per spec 15.2/15.3, both packaging formats below include:

- The `droppoint` binary (PSDK-linked) plus `droppoint_calibrate`,
  `droppoint_replay`, `droppoint_lenscal`
- Default config (`config/*.yaml`, all clearly marked uncalibrated where
  applicable)
- Offline DEM tiles for the deployment's operating area
  (`data/dem/`) — vendored at package-build time, no internet needed on the
  aircraft
- `scripts/droppoint.service` (systemd unit, restart-on-failure, watchdog)
- A log directory with correct permissions (`/var/log/droppoint`, owned by
  a dedicated `droppoint` service user)
- `tools/dashboard` (bench/calibration web dashboard — plain HTML/JS, no
  build step, no CDN, works fully offline; **development tool only, never
  presented as a flight interface**)

## Packaging formats

### 1. `.deb` (arm64)

Standard Debian packaging: `scripts/install.sh` is the logical equivalent
of a postinst script; wrap it with `dpkg-deb` once the PSDK cross-build
produces the `droppoint` binary. Vendor every dependency — the built
package must install with **no internet access** on the aircraft.

### 2. DPK (Manifold 3, and confirmed likely for M400/M4/M4D — see
`docs/OPEN-QUESTIONS.md` #5)

Manifold 3's liveview AI-metadata overlay requires DPK packaging
specifically (docs/PSDK-RESEARCH.md section 9). Use the upstream
Payload-SDK's `tools/build_dpk/build_dpk.sh` against an `app.json` manifest
and a static-only build (DPK does not support bundled `.so` files —
statically link `yaml-cpp`, matching what this repository already does by
default via `FetchContent`).

### 3. Flashable SD-card image

For the reference companion computer, ship a flashable image (e.g. built
with `dd`/`balenaEtcher`-compatible tooling from a base OS image plus the
`.deb` pre-installed and `droppoint.service` pre-enabled) so a spare unit
is field-replaceable by swapping a card, no laptop required in the field.
**Not built in this environment** — assembling this requires the actual
reference hardware image as a base, which is outside what can be produced
without hardware.

## Install

```
sudo ./scripts/install.sh
```

- Auto-starts on boot, comes up with aircraft power (systemd, `WantedBy=multi-user.target`).
- No login, no terminal needed for normal operation.
- Writes a placeholder `credentials.yaml` if one doesn't already exist and
  warns loudly — this is the only place a human must intervene before first
  flight (fill in real DJI developer credentials).

## Credentials

Live at `/etc/droppoint/credentials.yaml` — **outside** `/opt/droppoint`
(the install/upgrade target), so upgrades never touch them, they are never
baked into the package, and they are never committed to version control
(see `config/credentials.example.yaml` for the schema with no real values).

## Upgrade

Re-run `scripts/install.sh` with a new build's `bin/` directory in the
current working directory; it overwrites `/opt/droppoint/bin` and
`/opt/droppoint/config` (only if the config files don't already exist —
**status: the current install.sh script always ships defaults into
config/, an upgrade-safe merge that preserves site-specific edits to
config/*.yaml is a documented follow-on**, tracked here rather than
silently claimed as done) while leaving `/etc/droppoint/credentials.yaml`,
the drop log (`/var/log/droppoint/*.csv`), and payload calibration
(`config/payloads.yaml`, once a human has edited it post-calibration and
that edit is preserved by the upgrade-safe merge above) untouched once that
follow-on lands.

**Status: unverified — the "upgrade preserves credentials, config,
calibration and drop log" acceptance criterion (spec 17.11) requires the
upgrade-safe config merge noted above; today `install.sh` is safe for
credentials and the drop log (both live outside `/opt/droppoint`) but would
overwrite a hand-edited `config/payloads.yaml` unless that file is
protected. Treat this as an open packaging task, not a proven property,
until the merge logic and a real upgrade test exist.**

## Rollback

```
sudo ./scripts/rollback.sh
```

One command, per spec 15.3. Assumes releases are unpacked under
`/opt/droppoint-releases/<version>/` with `/opt/droppoint` as a symlink to
the active one (see the script's header comment) — this layout must be
adopted by whatever CI/release pipeline produces the package; it is not yet
wired into an actual release pipeline in this repository.

## Version and build hash

**Status: not yet implemented.** Spec 15.3 requires the version and build
hash to appear in the startup log, self-test report, and floating window on
request. This needs a `git describe`-derived string baked in at build time
(e.g. via a CMake-generated header) — tracked here as a concrete next step
rather than silently omitted.
