// psdk/platform_linux.h
//
// Status: unverified — requires hardware.
//
// Registers every platform handler the PSDK core needs, mirroring the DJI
// sample init order (samples/sample_c++ /platform/linux/*/application/
// application.cpp): OSAL + filesystem + socket handlers compiled straight
// from the DJI checkout, the console logger, then ONE transport handler
// selected by aircraft.yaml `psdk_link_mode`:
//
//   only_uart       -> our hal_uart (termios, config device + VID/PID)
//   only_usb_bulk   -> DJI's manifold3 hal_usb_bulk.c (compiled from the
//                      PSDK root, /dev/usb-ffs functionfs endpoints)
//
// Any other link mode is rejected here with a clear message rather than
// silently failing inside DjiCore_Init.
#pragma once

#include "util/config.h"

namespace droppoint::psdk {

// Throws std::runtime_error on registration failure or an unsupported link
// mode. Must be called before DjiCore_Init.
void RegisterPlatformHandlers(const util::config::AircraftConfig& aircraft);

}  // namespace droppoint::psdk
