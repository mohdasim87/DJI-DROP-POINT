#!/usr/bin/env bash
# scripts/setup_usb_bulk_jetson.sh
#
# Status: unverified — requires hardware.
#
# Configures the USB Function FS (FFS) bulk endpoints PSDK's USB-bulk link
# mode expects on Jetson-class companion computers (docs/CONNECTION.md step
# 4). This is a known DJI/Jetson integration snag: the gadget driver does
# not create /dev/usb-ffs/bulk1 and bulk2 automatically.
set -euo pipefail

FFS_MOUNT="/dev/usb-ffs"

if [[ $EUID -ne 0 ]]; then
  echo "run as root" >&2
  exit 1
fi

mkdir -p "${FFS_MOUNT}/bulk1" "${FFS_MOUNT}/bulk2"

if ! mountpoint -q "${FFS_MOUNT}/bulk1"; then
  mount -t functionfs bulk1 "${FFS_MOUNT}/bulk1"
fi
if ! mountpoint -q "${FFS_MOUNT}/bulk2"; then
  mount -t functionfs bulk2 "${FFS_MOUNT}/bulk2"
fi

echo "USB FFS bulk endpoints mounted. Reboot required for the USB gadget to bind."
echo "Verify after reboot with: ls ${FFS_MOUNT}"
