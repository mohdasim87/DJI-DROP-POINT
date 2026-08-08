#!/usr/bin/env bash
# scripts/install.sh
#
# Status: unverified — requires hardware.
#
# Takes a companion computer from factory state to a working DropPoint
# install with a single command and no internet access (spec 15.2). Run as
# root on the target companion computer after copying the built package
# there (see docs/DEPLOYMENT.md for the full procedure).
set -euo pipefail

PREFIX="/opt/droppoint"
LOG_DIR="/var/log/droppoint"
CREDENTIALS_DIR="/etc/droppoint"
SYSTEMD_UNIT="/etc/systemd/system/droppoint.service"

if [[ $EUID -ne 0 ]]; then
  echo "run as root" >&2
  exit 1
fi

echo "Installing DropPoint to ${PREFIX}..."
mkdir -p "${PREFIX}/bin" "${PREFIX}/config" "${PREFIX}/data/dem" "${PREFIX}/dashboard" "${LOG_DIR}"
mkdir -p "${CREDENTIALS_DIR}"

# Binary, default config, offline DEM tiles, bench dashboard (spec 15.2).
cp -v ./bin/droppoint "${PREFIX}/bin/droppoint"
cp -v ./bin/droppoint_calibrate ./bin/droppoint_replay ./bin/droppoint_lenscal "${PREFIX}/bin/"
cp -rv ./config/*.yaml "${PREFIX}/config/" 2>/dev/null || true
cp -rv ./data/dem/* "${PREFIX}/data/dem/" 2>/dev/null || true
cp -rv ./tools/dashboard/* "${PREFIX}/dashboard/" 2>/dev/null || true

chown -R droppoint:droppoint "${LOG_DIR}" 2>/dev/null || useradd -r -s /usr/sbin/nologin droppoint || true
chown -R droppoint:droppoint "${LOG_DIR}"

# Credentials live outside the package, survive upgrades, never committed
# (spec 15.3).
if [[ ! -f "${CREDENTIALS_DIR}/credentials.yaml" ]]; then
  cp -v "${PREFIX}/config/credentials.example.yaml" "${CREDENTIALS_DIR}/credentials.yaml"
  chmod 600 "${CREDENTIALS_DIR}/credentials.yaml"
  echo "WARNING: wrote a placeholder credentials file to ${CREDENTIALS_DIR}/credentials.yaml."
  echo "         Fill in real values from https://developer.dji.com/user/apps/ before flying."
fi

cp -v ./scripts/droppoint.service "${SYSTEMD_UNIT}"
systemctl daemon-reload
systemctl enable droppoint.service

echo "Running first-boot self-test..."
"${PREFIX}/bin/droppoint" --selftest || {
  echo "Self-test reported problems; see ${LOG_DIR}/selftest.txt before flying." >&2
}

echo "Install complete. Start with: systemctl start droppoint.service"
