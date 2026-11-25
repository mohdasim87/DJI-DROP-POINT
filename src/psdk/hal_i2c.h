// psdk/hal_i2c.h
//
// Status: unverified — requires hardware.
//
// I2C HAL for the DJI SDK Certified Chip (aircraft.yaml `auth_chip:`),
// required on the Matrice 4 series before PSDK works. Modelled on the DJI
// raspberry_pi sample
// (samples/sample_c/platform/linux/raspberry_pi/hal/hal_i2c.c) with two
// deliberate differences: the device path comes from aircraft.yaml
// `i2c_device` instead of a compile-time LINUX_I2C_DEV*, and the NRST pulse
// goes through the GPIO character device (hw::PulseAuthChipReset) instead of
// sysfs GPIO — sysfs numbering no longer matches BCM numbers on current
// Raspberry Pi kernels.
#pragma once

#include <string>

namespace droppoint::psdk {

// Called once at startup before DjiCore_Init. The handler functions are
// registered via RegisterPlatformHandlers() in platform_linux.cpp.
void ConfigureI2cHal(const std::string& device_path,
                     const std::string& reset_gpio_chip, int reset_gpio_line,
                     int reset_pulse_ms);

}  // namespace droppoint::psdk
