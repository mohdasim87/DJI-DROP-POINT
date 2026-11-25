#include "hw/auth_chip.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#ifndef _WIN32
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace droppoint::hw {
namespace {

char HexAddr(std::uint16_t addr, char* out, std::size_t n) {
    std::snprintf(out, n, "0x%02X", addr);
    return out[0];
}

std::string AddrStr(std::uint16_t addr) {
    char buf[8];
    HexAddr(addr, buf, sizeof(buf));
    return buf;
}

#ifndef _WIN32
// File descriptor kept open for the process lifetime so the GPIO line stays
// requested-high (the chip's reset must remain deasserted).
int g_reset_line_fd = -1;
int g_reset_chip_fd = -1;

// Linux i2c-dev backed bus for the real hardware probe.
class LinuxAuthChipBus final : public AuthChipBus {
public:
    explicit LinuxAuthChipBus(std::string device) : device_(std::move(device)) {}

    OpenResult Open() override {
        fd_ = open(device_.c_str(), O_RDWR);
        if (fd_ >= 0) return OpenResult::kOk;
        if (errno == ENOENT || errno == ENODEV) return OpenResult::kMissing;
        if (errno == EACCES || errno == EPERM) return OpenResult::kPermissionDenied;
        return OpenResult::kError;
    }

    bool ReadOneByte(std::uint16_t dev_address) override {
        if (fd_ < 0) return false;
        std::uint8_t byte = 0;
        struct i2c_msg msg {};
        msg.addr = dev_address;
        msg.flags = I2C_M_RD;
        msg.len = 1;
        msg.buf = &byte;
        struct i2c_rdwr_ioctl_data data {};
        data.msgs = &msg;
        data.nmsgs = 1;
        return ioctl(fd_, I2C_RDWR, &data) >= 0;
    }

    ~LinuxAuthChipBus() override {
        if (fd_ >= 0) close(fd_);
    }

private:
    std::string device_;
    int fd_ = -1;
};
#endif

}  // namespace

bool PulseAuthChipReset(const std::string& gpio_chip, int gpio_line,
                        int reset_pulse_ms, std::string* err) {
#ifdef _WIN32
    (void)gpio_chip;
    (void)gpio_line;
    (void)reset_pulse_ms;
    if (err) *err = "GPIO reset is only supported on Linux";
    return false;
#else
    if (g_reset_line_fd >= 0) return true;  // already reset, line still held

    int chip_fd = open(gpio_chip.c_str(), O_RDONLY);
    if (chip_fd < 0) {
        if (err) {
            *err = "cannot open " + gpio_chip + " (" + std::strerror(errno) + ")";
        }
        return false;
    }

    // GPIO character device v1: request the line as an output driven low.
    struct gpiohandle_request req {};
    req.lineoffsets[0] = static_cast<__u32>(gpio_line);
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    std::strncpy(reinterpret_cast<char*>(req.consumer_label), "droppoint",
                 sizeof(req.consumer_label) - 1);
    if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        if (err) {
            *err = "cannot request " + gpio_chip + " line " +
                   std::to_string(gpio_line) + " (" + std::strerror(errno) + ")";
        }
        close(chip_fd);
        return false;
    }
    g_reset_chip_fd = chip_fd;
    g_reset_line_fd = req.fd;

    // Hold low for the pulse, then drive high and leave it requested.
    usleep(static_cast<useconds_t>(reset_pulse_ms) * 1000u);
    struct gpiohandle_data values {};
    values.values[0] = 1;
    if (ioctl(g_reset_line_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &values) < 0) {
        if (err) {
            *err = "cannot drive " + gpio_chip + " line " +
                   std::to_string(gpio_line) + " high (" + std::strerror(errno) +
                   ")";
        }
        close(g_reset_line_fd);
        g_reset_line_fd = -1;
        close(g_reset_chip_fd);
        g_reset_chip_fd = -1;
        return false;
    }
    return true;
#endif
}

AuthChipStatus ProbeAuthChip(AuthChipBus& bus, std::uint16_t dev_address) {
    switch (bus.Open()) {
        case AuthChipBus::OpenResult::kOk:
            return bus.ReadOneByte(dev_address) ? AuthChipStatus::kFound
                                                : AuthChipStatus::kNotFound;
        case AuthChipBus::OpenResult::kMissing:
            return AuthChipStatus::kBusMissing;
        case AuthChipBus::OpenResult::kPermissionDenied:
            return AuthChipStatus::kBusPermission;
        case AuthChipBus::OpenResult::kError:
        default:
            return AuthChipStatus::kBusError;
    }
}

std::string FormatAuthChipResult(AuthChipStatus status,
                                 const std::string& i2c_device,
                                 std::uint16_t dev_address) {
    const std::string addr = AddrStr(dev_address);
    switch (status) {
        case AuthChipStatus::kFound:
            return "SDK certified chip found at " + addr + " on " + i2c_device;
        case AuthChipStatus::kNotFound:
            return "no device answered at " + addr + " on " + i2c_device +
                   " — check the wiring to the certified chip";
        case AuthChipStatus::kBusMissing:
            return "device node " + i2c_device +
                   " does not exist — the I2C bus the certified chip is wired "
                   "to is not present on this machine";
        case AuthChipStatus::kBusPermission:
            return "permission denied opening " + i2c_device +
                   " — the user running DropPoint needs read/write on the I2C "
                   "bus (add them to the 'i2c' group)";
        case AuthChipStatus::kBusError:
            return "could not open " + i2c_device +
                   " — check that the I2C bus exists and is enabled";
        default:
            return "unknown probe result";
    }
}

int AuthChipExitCode(AuthChipStatus status) {
    switch (status) {
        case AuthChipStatus::kFound:
            return 0;
        case AuthChipStatus::kNotFound:
            return 1;
        default:
            return 2;
    }
}

std::unique_ptr<AuthChipBus> MakeAuthChipBus(const std::string& i2c_device) {
#ifdef _WIN32
    (void)i2c_device;
    return nullptr;
#else
    return std::make_unique<LinuxAuthChipBus>(i2c_device);
#endif
}

}  // namespace droppoint::hw
