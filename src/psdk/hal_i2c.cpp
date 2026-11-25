// psdk/hal_i2c.cpp — see header. Status: unverified — requires hardware.
#include "hal_i2c.h"

#include <dji_platform.h>
#include <dji_typedef.h>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "hw/auth_chip.h"

namespace droppoint::psdk {

namespace {

std::mutex g_mu;
std::string g_device_path = "/dev/i2c-1";
std::string g_reset_gpio_chip = "/dev/gpiochip0";
int g_reset_gpio_line = 4;
int g_reset_pulse_ms = 25;

struct I2cHandleStruct {
    int i2c_fd = -1;
};

extern "C" {

T_DjiReturnCode HalI2c_Init(T_DjiHalI2cConfig i2cConfig,
                            T_DjiI2cHandle* i2cHandle) {
    (void)i2cConfig;  // bus speed/address are the chip's fixed values
    if (i2cHandle == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }

    std::string path, gpio_chip;
    int gpio_line, pulse_ms;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        path = g_device_path;
        gpio_chip = g_reset_gpio_chip;
        gpio_line = g_reset_gpio_line;
        pulse_ms = g_reset_pulse_ms;
    }

    // The certified chip needs NRST pulsed before it answers (the DJI sample
    // does the same via sysfs). A missing GPIO chip is not fatal — the chip
    // can have its own power-on reset circuit — so log and continue.
    std::string reset_err;
    if (!hw::PulseAuthChipReset(gpio_chip, gpio_line, pulse_ms, &reset_err)) {
        std::fprintf(stderr,
                     "droppoint hal_i2c: certified-chip reset failed: %s "
                     "(continuing — the chip may have its own reset circuit)\n",
                     reset_err.c_str());
    }

    auto* handle = static_cast<I2cHandleStruct*>(std::malloc(sizeof(I2cHandleStruct)));
    if (handle == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_MEMORY_ALLOC_FAILED;
    }
    handle->i2c_fd = open(path.c_str(), O_RDWR);
    if (handle->i2c_fd < 0) {
        std::fprintf(stderr, "droppoint hal_i2c: open %s failed: %s\n", path.c_str(),
                     std::strerror(errno));
        std::free(handle);
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }
    *i2cHandle = handle;
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalI2c_DeInit(T_DjiI2cHandle i2cHandle) {
    if (i2cHandle == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
    auto* handle = static_cast<I2cHandleStruct*>(i2cHandle);
    if (close(handle->i2c_fd) < 0) {
        std::free(handle);
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }
    std::free(handle);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalI2c_WriteData(T_DjiI2cHandle i2cHandle, uint16_t devAddress,
                                 const uint8_t* buf, uint32_t len,
                                 uint32_t* realLen) {
    if (i2cHandle == nullptr || buf == nullptr || len == 0 || realLen == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
    auto* handle = static_cast<I2cHandleStruct*>(i2cHandle);
    struct i2c_msg msg {};
    msg.addr = devAddress;
    msg.flags = 0;
    msg.len = static_cast<__u16>(len);
    msg.buf = const_cast<uint8_t*>(buf);
    struct i2c_rdwr_ioctl_data data {};
    data.msgs = &msg;
    data.nmsgs = 1;
    if (ioctl(handle->i2c_fd, I2C_RDWR, &data) < 0) {
        *realLen = 0;
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }
    *realLen = len;
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalI2c_ReadData(T_DjiI2cHandle i2cHandle, uint16_t devAddress,
                                uint8_t* buf, uint32_t len, uint32_t* realLen) {
    if (i2cHandle == nullptr || buf == nullptr || len == 0 || realLen == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
    auto* handle = static_cast<I2cHandleStruct*>(i2cHandle);
    struct i2c_msg msg {};
    msg.addr = devAddress;
    msg.flags = I2C_M_RD;
    msg.len = static_cast<__u16>(len);
    msg.buf = buf;
    struct i2c_rdwr_ioctl_data data {};
    data.msgs = &msg;
    data.nmsgs = 1;
    if (ioctl(handle->i2c_fd, I2C_RDWR, &data) < 0) {
        *realLen = 0;
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }
    *realLen = len;
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

}  // extern "C"

}  // namespace

void ConfigureI2cHal(const std::string& device_path,
                     const std::string& reset_gpio_chip, int reset_gpio_line,
                     int reset_pulse_ms) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_device_path = device_path;
    g_reset_gpio_chip = reset_gpio_chip;
    g_reset_gpio_line = reset_gpio_line;
    g_reset_pulse_ms = reset_pulse_ms;
}

// Fills the handler struct; called by platform_linux.cpp only.
void FillI2cHandler(T_DjiHalI2cHandler* handler) {
    handler->I2cInit = HalI2c_Init;
    handler->I2cDeInit = HalI2c_DeInit;
    handler->I2cWriteData = HalI2c_WriteData;
    handler->I2cReadData = HalI2c_ReadData;
}

}  // namespace droppoint::psdk
