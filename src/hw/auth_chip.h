// Probing and reset support for the DJI SDK Certified Chip (docs/SETUP-MANUAL.md
// 5.3): an I2C device at 0x2A on the payload side, required before PSDK works
// on the Matrice 4 series. All of this is DJI-free so it can be unit-tested.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace droppoint::hw {

// Pulse the chip's NRST line low for `reset_pulse_ms`, then drive it high and
// KEEP the GPIO line requested for the rest of the process (the chip's reset
// must stay deasserted). Uses the Linux GPIO character-device v1 ioctl —
// sysfs GPIO numbering no longer matches BCM numbers on current kernels.
// Returns false and fills `err` when the GPIO chip cannot be opened or the
// line cannot be requested; callers may continue (the chip can have its own
// power-on reset circuit).
bool PulseAuthChipReset(const std::string& gpio_chip, int gpio_line,
                        int reset_pulse_ms, std::string* err);

// Minimal I2C bus interface so the probe decision logic is testable on any OS.
class AuthChipBus {
public:
    enum class OpenResult {
        kOk,                // device node opened
        kMissing,           // node does not exist
        kPermissionDenied,  // no read/write permission on the node
        kError,             // any other open failure
    };
    virtual ~AuthChipBus() = default;

    virtual OpenResult Open() = 0;
    // One read transaction to `dev_address`; true when the device ACKs.
    virtual bool ReadOneByte(std::uint16_t dev_address) = 0;
};

enum class AuthChipStatus {
    kFound,
    kNotFound,
    kBusMissing,
    kBusPermission,
    kBusError,
};

AuthChipStatus ProbeAuthChip(AuthChipBus& bus, std::uint16_t dev_address);

// Human-readable outcome; used both on the console and in self-tests.
std::string FormatAuthChipResult(AuthChipStatus status,
                                 const std::string& i2c_device,
                                 std::uint16_t dev_address);

// 0 chip found, 1 no device answered, 2 node missing / permission / error.
int AuthChipExitCode(AuthChipStatus status);

// Production bus backed by /dev/i2c-N + the I2C_RDWR ioctl. Returns nullptr
// on platforms without Linux i2c-dev (Windows builds only compile this for
// the shared logic above).
std::unique_ptr<AuthChipBus> MakeAuthChipBus(const std::string& i2c_device);

}  // namespace droppoint::hw
