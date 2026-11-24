#!/usr/bin/env bash
# scripts/install.sh <droppoint-<profile>_<version>_arm64.deb>
#
# Installs the DropPoint .deb produced by scripts/package.sh on the target
# companion computer. Run as root. Offline — the package is self-contained
# (the binary links only glibc).
set -euo pipefail

DEB="${1:?usage: install.sh <package.deb>}"

if [[ $EUID -ne 0 ]]; then
    echo "run as root" >&2
    exit 1
fi
if [[ ! -f "$DEB" ]]; then
    echo "package not found: $DEB" >&2
    exit 1
fi

dpkg -i "$DEB"

cat <<'EOF'

DropPoint installed. Next steps:

  1. Put REAL DJI developer credentials in /etc/droppoint/credentials.yaml
     (a placeholder was created on first install; see
     /opt/droppoint/config/credentials.example.yaml for the field list).
  2. Drop SRTM .hgt tiles into /opt/droppoint/data/dem/ if you want
     home-tied DEM elevations (optional).
  3. Check the install as the SERVICE USER (a root run works too, but run it
     this way to see exactly what the service sees):
         sudo -u droppoint /opt/droppoint/bin/droppoint --selftest --no-link \
           --config /opt/droppoint/config
  4. Start the service: systemctl start droppoint.service
     (only needed on a generic companion — on Manifold 3 the app is
     installed through DJI Pilot 2 as a .dpk instead, see docs/).
EOF
