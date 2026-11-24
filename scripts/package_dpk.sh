#!/usr/bin/env bash
# scripts/package_dpk.sh <profile> <build_dir> <credentials.yaml> <out_dir> [psdk_root]
#
# Stages a Manifold 3 .dpk (installed through DJI Pilot 2 into /open_app/<app>/)
# and runs <psdk_root>/tools/build_dpk/build_dpk.sh on it.
#
# Staging layout (app.json at the root, per build_dpk.sh's relative paths):
#   app.json            generated from packaging/manifold3/app.json.in
#   bin/droppoint       the PSDK binary
#   config/*.yaml       the chosen profile + the GIVEN credentials.yaml (chmod 600)
#   config/dem/*.hgt    SRTM tiles, if data/dem/ has any
#   assets/widget/      Pilot 2 widget icons
#   (build_dpk.sh itself adds data/, data/logs/, data/media/)
#
# user_app_id is read out of the credentials file with a minimal regex parse
# (no yaml dependency); credential values are NEVER echoed. firmware_version
# is read from the built binary's version.h values (project VERSION) so the
# DPK installer check matches DjiCore_SetFirmwareVersion.
#
# Refuses to run when the credentials file still contains the example
# placeholders, and warns loudly that the .dpk CONTAINS the DJI credentials.
set -euo pipefail

PROFILE="${1:?usage: package_dpk.sh <profile> <build_dir> <credentials.yaml> <out_dir> [psdk_root]}"
BUILD_DIR="${2:?missing build dir}"
CREDS="${3:?missing credentials.yaml}"
OUT_DIR="${4:?missing out dir}"
PSDK_ROOT="${5:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ "$PROFILE" != "m4t" && "$PROFILE" != "m400" ]]; then
    echo "unknown profile '$PROFILE' (expected m4t or m400)" >&2
    exit 1
fi
if [[ ! -f "$CREDS" ]]; then
    echo "credentials file not found: $CREDS" >&2
    exit 1
fi
if [[ -z "$PSDK_ROOT" ]]; then
    for cand in "$ROOT/third_party/psdk" "$ROOT/../psdk-upstream"; do
        if [[ -x "$cand/tools/build_dpk/build_dpk.sh" ]]; then
            PSDK_ROOT="$cand"
            break
        fi
    done
fi
if [[ ! -x "$PSDK_ROOT/tools/build_dpk/build_dpk.sh" ]]; then
    echo "build_dpk.sh not found under '$PSDK_ROOT' — pass the PSDK checkout as arg 5" >&2
    exit 1
fi

# --- refuse example credentials ------------------------------------------------
if grep -Eq 'your_app_name|your_app_id|your_app_key|your_app_license|your_developer_account_email' "$CREDS"; then
    echo "error: $CREDS still contains the credentials.example.yaml placeholders —" >&2
    echo "       a .dpk without working credentials cannot be installed by Pilot 2." >&2
    exit 1
fi

# Minimal parse of `app_id: <value>` — app_id is a plain scalar, so a regex is
# sufficient and avoids a yaml dependency. Value is never echoed.
APP_ID="$(grep -E '^[[:space:]]*app_id[[:space:]]*:' "$CREDS" | head -1 \
           | sed -E 's/^[[:space:]]*app_id[[:space:]]*:[[:space:]]*"?([^"[:space:]]+)"?.*/\1/')"
if [[ -z "$APP_ID" ]]; then
    echo "error: no app_id found in $CREDS" >&2
    exit 1
fi

# --- firmware version: read the version baked into the build -------------------
# The single version source is project(VERSION) -> util/version.h. Look for the
# generated header in the build dir; fall back to the root CMakeLists.
VERSION_H="$(find "$BUILD_DIR" -name version.h -path '*generated*' | head -1)"
if [[ -n "$VERSION_H" ]]; then
    MAJ="$(grep -oE 'kVersionMajor = [0-9]+' "$VERSION_H" | grep -oE '[0-9]+')"
    MIN="$(grep -oE 'kVersionMinor = [0-9]+' "$VERSION_H" | grep -oE '[0-9]+')"
    PAT="$(grep -oE 'kVersionPatch = [0-9]+' "$VERSION_H" | grep -oE '[0-9]+')"
else
    MAJ="$(grep -oE 'project\(droppoint VERSION [0-9]+' "$ROOT/CMakeLists.txt" | grep -oE '[0-9]+' | head -1)"
    MIN="$(grep -oE 'project\(droppoint VERSION [0-9]+\.[0-9]+' "$ROOT/CMakeLists.txt" | grep -oE '[0-9]+' | tail -1)"
    PAT="$(grep -oE 'project\(droppoint VERSION [0-9]+\.[0-9]+\.[0-9]+' "$ROOT/CMakeLists.txt" | grep -oE '[0-9]+' | tail -1)"
fi
FW_VERSION="$(printf '%02d.%02d.%02d.00' "$MAJ" "$MIN" "$PAT")"

# --- find the binary -------------------------------------------------------------
MAIN_BIN=""
for cand in "$BUILD_DIR/src/psdk/droppoint" "$BUILD_DIR/src/app/droppoint" "$BUILD_DIR/bin/droppoint"; do
    if [[ -f "$cand" ]]; then MAIN_BIN="$cand"; break; fi
done
if [[ -z "$MAIN_BIN" ]]; then
    echo "droppoint binary not found under $BUILD_DIR (build with -DDROPPOINT_BUILD_PSDK=ON)" >&2
    exit 1
fi

NAME_EN="DropPoint-$(echo "$PROFILE" | tr 'a-z' 'A-Z')"
MAINTAINER="DropPoint Maintainer"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/bin" "$STAGE/config/dem" "$STAGE/assets"
cp "$MAIN_BIN" "$STAGE/bin/droppoint"
chmod 755 "$STAGE/bin/droppoint"
cp "$ROOT/config/profiles/$PROFILE"/*.yaml "$STAGE/config/"
cp "$CREDS" "$STAGE/config/credentials.yaml"
chmod 600 "$STAGE/config/credentials.yaml"
if [[ -d "$ROOT/data/dem" ]]; then
    cp "$ROOT/data/dem/"*.hgt "$STAGE/config/dem/" 2>/dev/null || true
fi
if [[ -d "$ROOT/assets/widget" ]]; then
    cp -r "$ROOT/assets/widget" "$STAGE/assets/widget"
fi

sed -e "s|@USER_APP_ID@|$APP_ID|g" \
    -e "s|@FIRMWARE_VERSION@|$FW_VERSION|g" \
    -e "s|@NAME_EN@|$NAME_EN|g" \
    -e "s|@MAINTAINER@|$MAINTAINER|g" \
    "$ROOT/packaging/manifold3/app.json.in" > "$STAGE/app.json"

mkdir -p "$OUT_DIR"
"$PSDK_ROOT/tools/build_dpk/build_dpk.sh" -i "$STAGE/app.json" -o "$OUT_DIR"

DPK="$OUT_DIR/${NAME_EN}_v${FW_VERSION}.dpk"
if [[ ! -f "$DPK" ]]; then
    echo "expected $DPK was not produced" >&2
    exit 1
fi

cat <<EOF
built $DPK

WARNING: this .dpk CONTAINS the DJI developer credentials from
         $(basename "$CREDS"). Do NOT publish it, attach it to releases, or
         commit it — distribute it only to the operators who will install
         it through DJI Pilot 2.
EOF
