#!/usr/bin/env bash
# scripts/package.sh <profile:m4t|m400> <version> <build_dir> <out_dir>
#
# Builds droppoint-<profile>_<version>_arm64.deb with dpkg-deb.
#
# Layout installed on the target:
#   /opt/droppoint/bin/{droppoint,droppoint_sim_main,droppoint_calibrate,
#                       droppoint_replay,droppoint_lenscal}
#   /opt/droppoint/config/*.yaml          (chosen profile + credentials.example.yaml;
#                                        DEBIAN/conffiles -> upgrades keep site edits)
#   /opt/droppoint/assets/widget/         (Pilot 2 widget icons)
#   /opt/droppoint/data/dem/              (empty; drop SRTM .hgt tiles here)
#   /opt/droppoint/dashboard/             (bench dashboard)
#   /lib/systemd/system/droppoint.service
#
# The binary must already be statically linked against libstdc++/libgcc
# (src/psdk/CMakeLists.txt does this) so the only runtime dependency is
# glibc — verified here with ldd before packing.
set -euo pipefail

PROFILE="${1:?usage: package.sh <profile:m4t|m400> <version> <build_dir> <out_dir>}"
VERSION="${2:?missing version}"
BUILD_DIR="${3:?missing build dir}"
OUT_DIR="${4:?missing out dir}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ "$PROFILE" != "m4t" && "$PROFILE" != "m400" ]]; then
    echo "unknown profile '$PROFILE' (expected m4t or m400)" >&2
    exit 1
fi

# Binaries land in different places depending on the generator; find each.
find_bin() {
    local name="$1"
    for cand in \
        "$BUILD_DIR/src/app/$name" "$BUILD_DIR/src/psdk/$name" \
        "$BUILD_DIR/tools/calibrate/$name" "$BUILD_DIR/tools/replay/$name" \
        "$BUILD_DIR/tools/lenscal/$name" \
        "$BUILD_DIR/bin/$name"; do
        if [[ -f "$cand" ]]; then
            echo "$cand"
            return 0
        fi
    done
    return 1
}

MAIN_BIN="$(find_bin droppoint)" || {
    echo "droppoint binary not found under $BUILD_DIR (build with -DDROPPOINT_BUILD_PSDK=ON)" >&2
    exit 1
}

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PKG="droppoint-${PROFILE}_${VERSION}_arm64"
PKGDIR="$STAGE/$PKG"

mkdir -p "$PKGDIR/DEBIAN" \
         "$PKGDIR/opt/droppoint/bin" \
         "$PKGDIR/opt/droppoint/config" \
         "$PKGDIR/opt/droppoint/assets" \
         "$PKGDIR/opt/droppoint/data/dem" \
         "$PKGDIR/opt/droppoint/dashboard" \
         "$PKGDIR/lib/systemd/system"

# --- binaries ---------------------------------------------------------------
cp "$MAIN_BIN" "$PKGDIR/opt/droppoint/bin/droppoint"
for tool in droppoint_sim_main droppoint_calibrate droppoint_replay droppoint_lenscal; do
    if b="$(find_bin "$tool")"; then
        cp "$b" "$PKGDIR/opt/droppoint/bin/$tool"
    else
        echo "warning: $tool not found under $BUILD_DIR — packaging without it" >&2
    fi
done
chmod 755 "$PKGDIR/opt/droppoint/bin/"* || true

# glibc-only dependency check: anything beyond libc/libm/libpthread/
# ld-linux is a packaging bug (static libstdc++/libgcc expected).
if command -v ldd >/dev/null && ldd "$MAIN_BIN" >/dev/null 2>&1; then
    if ldd "$MAIN_BIN" | grep -Eq 'libstdc\+\+|libgcc_s'; then
        echo "error: droppoint still links dynamic libstdc++/libgcc — the .deb would need" >&2
        echo "       a newer libstdc++ than the target ships. Rebuild with static runtime." >&2
        exit 1
    fi
fi

# --- config: the chosen profile + the credentials EXAMPLE (never real) ------
cp "$ROOT/config/profiles/$PROFILE"/*.yaml "$PKGDIR/opt/droppoint/config/"
cp "$ROOT/config/credentials.example.yaml" "$PKGDIR/opt/droppoint/config/"

# --- assets + placeholders ---------------------------------------------------
cp -r "$ROOT/assets/widget" "$PKGDIR/opt/droppoint/assets/widget"
if [[ -d "$ROOT/tools/dashboard" ]]; then
    cp -r "$ROOT/tools/dashboard/." "$PKGDIR/opt/droppoint/dashboard/"
fi
cp "$ROOT/scripts/droppoint.service" "$PKGDIR/lib/systemd/system/"

# --- DEBIAN metadata -----------------------------------------------------------
INSTALLED_SIZE=$(du -sk "$PKGDIR" | cut -f1)
cat > "$PKGDIR/DEBIAN/control" <<EOF
Package: droppoint-$PROFILE
Version: $VERSION
Section: utils
Priority: optional
Architecture: arm64
Installed-Size: $INSTALLED_SIZE
Maintainer: DropPoint <maintainer@example.invalid>
Description: DropPoint payload-drop guidance system ($PROFILE profile)
 Precision drop-into-place guidance for DJI Matrice aircraft, profile $PROFILE.
EOF

# conffiles: dpkg keeps the admin's edited versions on upgrade.
{
    for f in "$PKGDIR/opt/droppoint/config/"*.yaml; do
        echo "/opt/droppoint/config/$(basename "$f")"
    done
} > "$PKGDIR/DEBIAN/conffiles"

cat > "$PKGDIR/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if ! getent group droppoint >/dev/null; then
    addgroup --system droppoint 2>/dev/null || groupadd -r droppoint
fi
if ! getent passwd droppoint >/dev/null; then
    adduser --system --ingroup droppoint --no-create-home --shell /usr/sbin/nologin droppoint \
        2>/dev/null || useradd -r -g droppoint -s /usr/sbin/nologin droppoint
fi
# dialout: serial access to the E-Port USB-UART adapter.
usermod -aG dialout droppoint 2>/dev/null || true

mkdir -p /var/log/droppoint
chown -R droppoint:droppoint /var/log/droppoint
mkdir -p /etc/droppoint

# Placeholder credentials only when absent — never overwrite a real file.
if [ ! -f /etc/droppoint/credentials.yaml ]; then
    cp /opt/droppoint/config/credentials.example.yaml /etc/droppoint/credentials.yaml
    chmod 600 /etc/droppoint/credentials.yaml
    chown droppoint:droppoint /etc/droppoint/credentials.yaml
    echo "droppoint: wrote PLACEHOLDER credentials to /etc/droppoint/credentials.yaml —" \
         "fill in real DJI developer values before flying."
fi

chown -R droppoint:droppoint /opt/droppoint/data/dem 2>/dev/null || true

if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || true
    systemctl enable droppoint.service || true
fi
exit 0
EOF

cat > "$PKGDIR/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
if [ -d /run/systemd/system ]; then
    systemctl stop droppoint.service 2>/dev/null || true
fi
exit 0
EOF

chmod 755 "$PKGDIR/DEBIAN/postinst" "$PKGDIR/DEBIAN/prerm"
chmod 644 "$PKGDIR/DEBIAN/control" "$PKGDIR/DEBIAN/conffiles"

mkdir -p "$OUT_DIR"
dpkg-deb --build "$PKGDIR" "$OUT_DIR/$PKG.deb"
echo "built $OUT_DIR/$PKG.deb"
