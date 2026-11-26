# Monocypher (vendored)

Single-file Ed25519 implementation used for DropPoint license
verification (`src/util/license.cpp`) and the vendor-side
`droppoint-license` tool.

- Upstream: https://github.com/LoupVaillant/Monocypher
- Version: 4.0.2 (`src/monocypher.c`, `src/monocypher.h` copied verbatim)
- Licence: BSD-2-Clause OR CC0-1.0 (see LICENCE.md) — DropPoint uses the
  CC0 (public domain) grant.
- Fetched 2026-06 from the 4.0.2 release tag; do not edit in place —
  update the whole vendored copy instead.

Only the `crypto_eddsa_*` functions are used: key-pair generation and
signing in `tools/license`, signature checking in the shipped binary.
