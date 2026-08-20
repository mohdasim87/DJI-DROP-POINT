// psdk/platform_linux.h
//
// Linux platform HAL registration for the DJI PSDK.
#pragma once

#include "util/config.h"

namespace droppoint::psdk {

// Registers OSAL, filesystem, socket, and transport (UART/USB/network) handlers
// with the PSDK. Throws std::runtime_error on any registration failure.
// This is a no-op on non-Linux platforms because the handlers are POSIX-based.
void PlatformInit(const util::config::AircraftConfig& cfg);

// Currently a no-op; the handlers are stateless closures over file descriptors.
void PlatformDeinit();

}  // namespace droppoint::psdk
