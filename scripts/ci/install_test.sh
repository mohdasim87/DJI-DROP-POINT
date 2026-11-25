#!/usr/bin/env bash
# In-container install test for one DropPoint .deb profile (install-test-arm64).
# Runs inside arm64v8/ubuntu:20.04 with the repo's dist/, scripts/ci/ and
# tests/data/ mounted at /dist, /ci and /creds.
#
# Usage: install_test.sh <profile> <deb path in container> [credentials path]
#
# DP_TEST_ROOT (test-only): when set, every absolute path the script touches
# is redirected under it so the control flow can be dry-run off the arm64
# container with stub commands on PATH. CI never sets it.
set -euo pipefail
set -x

profile="$1"
DEB="$2"
CREDS="${3:-}"
ROOT="${DP_TEST_ROOT:-}"
CI_DIR="$(cd "$(dirname "$0")" && pwd)"

BIN="$ROOT/opt/droppoint/bin"
CFGDIR="$ROOT/opt/droppoint/config"
LOGD="$ROOT/var/log/droppoint"
ETC_CREDS="$ROOT/etc/droppoint/credentials.yaml"
TMPD="$ROOT/tmp"
mkdir -p "$TMPD"

export DEBIAN_FRONTEND=noninteractive

dpkg -i "$DEB"
"$BIN/droppoint" --version
"$BIN/droppoint" --list-kits
"$BIN/droppoint" --selftest --no-link --config "$CFGDIR" || {
  echo '::error::selftest failed — dumping report log if present'
  cat "$LOGD/selftest.txt" ./data/logs/selftest.txt 2>/dev/null || true
  exit 1
}
# the service runs as user 'droppoint' — the selftest must pass without root;
# widget files are per-process so log-dir ownership must not matter for them
runuser -u droppoint -- "$BIN/droppoint" \
  --selftest --no-link --config "$CFGDIR" | tee "$TMPD/st-$profile.log"
test "${PIPESTATUS[0]}" -eq 0 || {
  echo '::error::non-root selftest failed — dumping report log'
  cat "$LOGD/selftest.txt" 2>/dev/null || true
  exit 1
}
grep -q '\[OK\] widget files generated' "$TMPD/st-$profile.log"
grep -q '\[INFO\] drop kit:' "$TMPD/st-$profile.log"
# m4t expects the SDK certified chip on /dev/i2c-1: in a container with no I2C
# node the probe must exit 2 naming the missing device node
if [ "$profile" = m4t ]; then
  set +e
  "$BIN/droppoint" --check-auth-chip \
    --config "$CFGDIR" > "$TMPD/chip.log" 2>&1
  CHIP_RC=$?
  set -e
  test "$CHIP_RC" -eq 2
  grep -q 'device node' "$TMPD/chip.log"
fi
# the report the ROOT run wrote must have been adopted into the log dir's
# ownership — the service user can overwrite it
runuser -u droppoint -- sh -c "test -w '$LOGD/selftest.txt'"
# alternating owners must keep working: root again, then the service user
"$BIN/droppoint" --selftest --no-link --config "$CFGDIR"
runuser -u droppoint -- "$BIN/droppoint" \
  --selftest --no-link --config "$CFGDIR"
"$BIN/droppoint_sim_main" --sim hover --profile "$CFGDIR" | tee "$TMPD/sim-$profile.log"
test "${PIPESTATUS[0]}" -eq 0
grep -Eq 'SUMMARY.*fired=1' "$TMPD/sim-$profile.log" || {
  echo '::error::hover sim did not fire for profile'
  cat "$TMPD/sim-$profile.log"
  exit 1
}
# the Manifold 3 customer flow as an unprivileged user: unpack the .deb,
# build the .dpk with the bundled tool, check it
if [ "$profile" = m400 ]; then
  echo '=== make-dpk ==='
  useradd -m customer 2>/dev/null || true
  MKD="$TMPD/mkdpk"
  mkdir -p "$MKD" && chown customer "$MKD"
  cp "$DEB" "$MKD/"
  runuser -u customer -- bash "$CI_DIR/make_dpk_customer.sh" "$MKD" "$CREDS"
  echo '=== end make-dpk ==='
fi
# upgrade-marker survival: site config edit, real credentials, and the log
# dir must all survive a reinstall
echo '# site edit marker' >> "$CFGDIR/payloads.yaml"
echo 'marker: survives' >> "$ETC_CREDS"
touch "$LOGD/marker_file"
dpkg -i "$DEB"
grep -q 'site edit marker' "$CFGDIR/payloads.yaml"
grep -q 'marker: survives' "$ETC_CREDS"
test -f "$LOGD/marker_file"
# removal: credentials + logs are site data, never removed
dpkg -r "droppoint-$profile"
test -f "$ETC_CREDS"
test -d "$LOGD"
echo "INSTALL TEST $profile PASSED"
