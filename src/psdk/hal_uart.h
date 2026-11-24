// psdk/hal_uart.h
//
// Status: unverified — requires hardware.
//
// UART transport HAL for the PSDK link (aircraft.yaml
// psdk_link_mode: only_uart). Modelled on the DJI raspberry_pi sample
// (samples/sample_c/platform/linux/raspberry_pi/hal/hal_uart.c) with two
// deliberate differences: the device path comes from aircraft.yaml
// `uart_device` instead of a compile-time LINUX_UART_DEV*, and
// UartGetDeviceInfo reports the configured `uart_usb_vid`/`uart_usb_pid`
// (the VID/PID of the USB-serial adapter the FC sees — required on the
// M400 E-Port V2, which has no UART pins at all).
#pragma once

#include <cstdint>
#include <string>

namespace droppoint::psdk {

// Called once at startup before DjiCore_Init. The handler functions are
// registered via RegisterUartHandler() in platform_linux.cpp.
void ConfigureUartHal(const std::string& device_path, std::uint16_t usb_vid, std::uint16_t usb_pid);

}  // namespace droppoint::psdk
