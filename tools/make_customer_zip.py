#!/usr/bin/env python3
# tools/make_customer_zip.py
#
# Builds dist/DropPoint-Customer_<version>.zip — the complete customer
# deliverable: step-by-step docs, both .deb installers, and the license
# how-to. Compiled binaries only: a binary cannot be made unreadable or
# uncopyable, so the license binding (not obfuscation) is the protection —
# the docs say this plainly.
#
# Layout inside the zip:
#   START-HERE.txt
#   docs/DropPoint-Setup-Manual.pdf
#   docs/DropPoint-How-To.pdf
#   docs/Compatible-drop-kits.md
#   docs/Matrice-4T-guide.md            (copy of docs/aircraft/M4T.md)
#   docs/Matrice-400-guide.md           (copy of docs/aircraft/M400.md)
#   install/droppoint-m4t_*_arm64.deb
#   install/droppoint-m400_*_arm64.deb
#   license/HOW-TO-GET-A-KEY.txt
#
# Usage:
#   python tools/make_customer_zip.py --dist <dir-with-debs> [--out dist]

import argparse
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FIXED_DATE = (2026, 1, 1, 0, 0, 0)  # deterministic zip: fixed timestamps

START_HERE = """\
DropPoint — customer package
============================

1. Pick the chapter for your aircraft in docs/DropPoint-Setup-Manual.pdf:
     - Matrice 4T ............... chapter 5  (see docs/Matrice-4T-guide.md)
     - Matrice 400 + Manifold 3 . chapter 6  (see docs/Matrice-400-guide.md)
     - Matrice 400 + own computer chapter 7
2. Install the matching package from install/:
     - Matrice 4T:    sudo dpkg -i droppoint-m4t_*_arm64.deb
     - Matrice 400:   sudo dpkg -i droppoint-m400_*_arm64.deb
       (Manifold 3 customers then build the .dpk with the bundled
        droppoint-make-dpk tool — manual chapter 6.2)
3. Fill in your DJI developer credentials:
     /etc/droppoint/credentials.yaml  (see manual chapter 4)
4. Get your license device code:
     droppoint --device-id
   (it also shows for the first 60 s in the Pilot 2 status window)
5. Send the code to your vendor; install the returned file:
     sudo droppoint --license-install <file>.lic
6. Bench test (manual chapter 11), then fly.

You have a 30-day trial from first start — everything works while you sort
the license. Full license details: manual chapter 16.
"""

LICENSE_HOWTO = """\
How to get your DropPoint license key
=====================================

1. On the companion computer (Raspberry Pi or Manifold 3), run:
       droppoint --device-id
   It prints a code like  DJI-XXXXX-XXXXX-XXXXX .
   The same code appears in the DJI Pilot 2 status window for the first
   60 seconds after DropPoint starts.

2. Send that code to your vendor. You receive a small .lic file
   containing a DPL1-... string, bound to that exact computer.

3. Install it:
       sudo droppoint --license-install yourfile.lic
   (Manifold 3: the same command inside the app files, or copy it to
   license.key next to config/ before installing the .dpk)

4. Check:  droppoint --license-status   should say  status: licensed .

Notes
-----
- The license binds to the companion computer, NOT the aircraft.
- A license for a different computer is refused — install refuses and
  prints why.
- The package ships compiled binaries only. A binary cannot be made
  uncopyable; the license binding is the protection.
- The 30-day trial clock lives in /var/lib/droppoint/license.state
  (mirror: /var/log/droppoint/.lic_state). Deleting both resets it —
  an accepted, documented limitation.
"""


def repo_version() -> str:
    for line in (REPO / "CMakeLists.txt").read_text().splitlines():
        if line.startswith("project(droppoint VERSION"):
            return line.split()[2]
    return "0.0.0"


def add_bytes(zf: zipfile.ZipFile, arcname: str, data: bytes) -> None:
    info = zipfile.ZipInfo(arcname, FIXED_DATE)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    zf.writestr(info, data)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dist", required=True, help="directory containing the .deb files")
    ap.add_argument("--out", default=str(REPO / "dist"), help="output directory")
    args = ap.parse_args()

    dist = Path(args.dist)
    debs = {}
    for profile in ("m4t", "m400"):
        matches = sorted(dist.glob(f"droppoint-{profile}_*_arm64.deb"))
        if not matches:
            print(f"error: no droppoint-{profile}_*_arm64.deb in {dist}", file=sys.stderr)
            return 1
        debs[profile] = matches[-1]

    version = repo_version()
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / f"DropPoint-Customer_{version}.zip"

    entries = [
        ("START-HERE.txt", START_HERE.encode()),
        ("docs/DropPoint-Setup-Manual.pdf",
         (REPO / "docs/DropPoint-Setup-Manual.pdf").read_bytes()),
        ("docs/DropPoint-How-To.pdf",
         (REPO / "docs/DropPoint-How-To.pdf").read_bytes()),
        ("docs/Compatible-drop-kits.md",
         (REPO / "docs/DROP-KITS.md").read_bytes()),
        ("docs/Matrice-4T-guide.md",
         (REPO / "docs/aircraft/M4T.md").read_bytes()),
        ("docs/Matrice-400-guide.md",
         (REPO / "docs/aircraft/M400.md").read_bytes()),
        (f"install/{debs['m4t'].name}", debs["m4t"].read_bytes()),
        (f"install/{debs['m400'].name}", debs["m400"].read_bytes()),
        ("license/HOW-TO-GET-A-KEY.txt", LICENSE_HOWTO.encode()),
    ]
    entries.sort(key=lambda e: e[0])

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for arcname, data in entries:
            add_bytes(zf, arcname, data)

    print(f"built {zip_path}")
    print("manifest:")
    for arcname, data in entries:
        print(f"  {len(data):>10}  {arcname}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
