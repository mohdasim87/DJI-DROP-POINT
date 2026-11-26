#!/usr/bin/env bash
# Customer-side Manifold 3 flow, run as an unprivileged user inside the
# install-test container: unpack the .deb, build the .dpk with the bundled
# droppoint-make-dpk tool, verify contents, verify it refuses placeholder
# credentials.
#
# Usage: make_dpk_customer.sh <workdir containing the m400 .deb> <credentials path>
set -euo pipefail
set -x

cd "$1"
CREDS="$2"

dpkg-deb -x droppoint-m400_*.deb dp
dp/opt/droppoint/bin/droppoint-make-dpk \
  --credentials "$CREDS" --out .
dpkg-deb -c DropPoint-M400_v*.dpk | tee listing.txt
test "${PIPESTATUS[0]}" -eq 0
grep -q "bin/droppoint" listing.txt
grep -q "config/aircraft.yaml" listing.txt
grep -q "config/credentials.yaml" listing.txt
grep -q "assets/widget/" listing.txt
# a license.key — even one left lying in the staged deb tree — must never
# ship inside a customer .dpk (customers install their own key later)
touch dp/opt/droppoint/config/license.key
dp/opt/droppoint/bin/droppoint-make-dpk \
  --credentials "$CREDS" --out . >/dev/null
dpkg-deb -c DropPoint-M400_v*.dpk | tail -n +2 > listing2.txt
if grep -q 'license\.key' listing2.txt; then
  echo "make-dpk packed a license.key into the .dpk" >&2
  exit 1
fi
if dp/opt/droppoint/bin/droppoint-make-dpk \
    --credentials dp/opt/droppoint/config/credentials.example.yaml \
    --out . 2>refuse.log; then
  echo "make-dpk accepted placeholder credentials" >&2
  exit 1
fi
grep -qi placeholder refuse.log
