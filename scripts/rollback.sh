#!/usr/bin/env bash
# scripts/rollback.sh <previous-package.deb>
#
# One-command rollback: reinstalls the previous .deb over the current one
# (dpkg handles the downgrade; site edits in /opt/droppoint/config survive
# because the yaml files are dpkg conffiles, and /etc/droppoint/credentials.yaml
# is never touched by a package).
#
# Usage: rollback.sh /path/to/droppoint-<profile>_<previous-version>_arm64.deb
set -euo pipefail

DEB="${1:?usage: rollback.sh <previous package.deb>}"

if [[ $EUID -ne 0 ]]; then
    echo "run as root" >&2
    exit 1
fi
if [[ ! -f "$DEB" ]]; then
    echo "package not found: $DEB" >&2
    exit 1
fi

if [[ -d /run/systemd/system ]]; then
    systemctl stop droppoint.service || true
fi

dpkg -i "$DEB"

if [[ -d /run/systemd/system ]]; then
    systemctl start droppoint.service || true
fi
echo "Rollback complete. Installed version: $(dpkg-deb -f "$DEB" Version 2>/dev/null || echo '?')"
