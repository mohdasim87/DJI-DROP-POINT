#!/usr/bin/env bash
# scripts/rollback.sh
#
# Status: unverified — requires hardware.
#
# One-command rollback to the previously installed version (spec 15.3).
# Assumes install.sh keeps the previous package under
# /opt/droppoint-releases/<version>/ and /opt/droppoint is a symlink to the
# active one; credentials, config, calibration and drop log all live outside
# /opt/droppoint/bin so they are untouched by switching the symlink.
set -euo pipefail

RELEASES_DIR="/opt/droppoint-releases"
ACTIVE_LINK="/opt/droppoint"

if [[ $EUID -ne 0 ]]; then
  echo "run as root" >&2
  exit 1
fi

current=$(readlink -f "${ACTIVE_LINK}" || true)
mapfile -t versions < <(ls -1 "${RELEASES_DIR}" | sort -V)

if [[ ${#versions[@]} -lt 2 ]]; then
  echo "no previous version to roll back to" >&2
  exit 1
fi

previous="${versions[-2]}"
echo "Rolling back from $(basename "${current}") to ${previous}..."
systemctl stop droppoint.service
ln -sfn "${RELEASES_DIR}/${previous}" "${ACTIVE_LINK}"
systemctl start droppoint.service
echo "Rollback complete. Active version: ${previous}"
