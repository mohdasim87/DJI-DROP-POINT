// psdk/probe_widgets.h
//
// Status: unverified — requires hardware.
//
// Implements `droppoint --probe-widgets <mount_position>`: downloads the
// widget files of the payload on <mount_position> (e.g. a CZI TH4 drop
// kit), prints index/type/name per widget parsed from its
// widget_config.json, then subscribes to the payload's widget states and
// prints every change for 15 s. This is the discovery step before
// configuring release.backend: psdk_widget — see docs/CZI-DROP-KITS.md.
//
// Widget Manager is M400/Linux only; callers validate aircraft.yaml model
// first and this function throws std::runtime_error on failure.
#pragma once

#include <string>

namespace droppoint::psdk {

// out_dir receives the downloaded files (created if absent). Returns 0 on
// success; throws on any unrecoverable error. Call after DjiCore_Init and
// DjiCore_ApplicationStart.
int ProbeWidgets(int mount_position, const std::string& out_dir);

}  // namespace droppoint::psdk
