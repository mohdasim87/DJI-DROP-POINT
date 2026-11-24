// psdk/hal_uart.cpp — see header. Status: unverified — requires hardware.
#include "hal_uart.h"

#include <dji_platform.h>
#include <dji_typedef.h>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace droppoint::psdk {

namespace {

std::mutex g_mu;
std::string g_device_path = "/dev/ttyUSB0";
std::uint16_t g_usb_vid = 0x0403;
std::uint16_t g_usb_pid = 0x6001;
std::map<T_DjiUartHandle, int> g_open_fds;  // handle -> fd, for UartGetStatus

struct UartHandleStruct {
    int uart_fd = -1;
    E_DjiHalUartNum uart_num = DJI_HAL_UART_NUM_0;
};

speed_t BaudConstant(std::uint32_t baud_rate) {
    switch (baud_rate) {
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        case 1000000: return B1000000;
        default: return B0;
    }
}

extern "C" {

T_DjiReturnCode HalUart_Init(E_DjiHalUartNum uartNum, uint32_t baudRate,
                             T_DjiUartHandle* uartHandle) {
    if (uartHandle == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }

    speed_t speed = BaudConstant(baudRate);
    if (speed == B0) {
        std::fprintf(stderr, "droppoint hal_uart: unsupported baud rate %u\n", baudRate);
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }

    std::string path;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        path = g_device_path;
    }

    auto* handle = static_cast<UartHandleStruct*>(std::malloc(sizeof(UartHandleStruct)));
    if (handle == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_MEMORY_ALLOC_FAILED;
    }
    handle->uart_num = uartNum;

    handle->uart_fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (handle->uart_fd == -1) {
        std::fprintf(stderr, "droppoint hal_uart: open %s failed: %s\n", path.c_str(),
                     std::strerror(errno));
        std::free(handle);
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    // Forbid a second PSDK program holding the same serial port.
    struct flock lock {};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    if (fcntl(handle->uart_fd, F_SETLKW, &lock) < 0) {
        std::fprintf(stderr, "droppoint hal_uart: %s is locked by another process\n", path.c_str());
        close(handle->uart_fd);
        std::free(handle);
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    struct termios options {};
    if (tcgetattr(handle->uart_fd, &options) != 0) {
        close(handle->uart_fd);
        std::free(handle);
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    // 8N1, raw, no flow control — mirrors the DJI raspberry_pi sample.
    options.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
    options.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
    options.c_cflag &= ~static_cast<tcflag_t>(CSIZE);
    options.c_cflag |= static_cast<tcflag_t>(CS8);
    options.c_cflag &= ~static_cast<tcflag_t>(PARENB);
    options.c_cflag &= ~static_cast<tcflag_t>(CSTOPB);
    options.c_iflag &= ~static_cast<tcflag_t>(INPCK);
    options.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    options.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | ISTRIP | IXON);
    options.c_cc[VTIME] = 0;
    options.c_cc[VMIN] = 0;

    tcflush(handle->uart_fd, TCIFLUSH);
    if (tcsetattr(handle->uart_fd, TCSANOW, &options) != 0) {
        close(handle->uart_fd);
        std::free(handle);
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    {
        std::lock_guard<std::mutex> g(g_mu);
        g_open_fds[handle] = handle->uart_fd;
    }
    *uartHandle = handle;
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalUart_DeInit(T_DjiUartHandle uartHandle) {
    if (uartHandle == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
    auto* handle = static_cast<UartHandleStruct*>(uartHandle);
    {
        std::lock_guard<std::mutex> g(g_mu);
        g_open_fds.erase(uartHandle);
    }
    if (close(handle->uart_fd) < 0) {
        std::free(handle);
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }
    std::free(handle);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalUart_WriteData(T_DjiUartHandle uartHandle, const uint8_t* buf,
                                  uint32_t len, uint32_t* realLen) {
    if (uartHandle == nullptr || buf == nullptr || len == 0 || realLen == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
    auto* handle = static_cast<UartHandleStruct*>(uartHandle);
    ssize_t ret = write(handle->uart_fd, buf, len);
    if (ret < 0) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }
    *realLen = static_cast<uint32_t>(ret);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalUart_ReadData(T_DjiUartHandle uartHandle, uint8_t* buf, uint32_t len,
                                 uint32_t* realLen) {
    if (uartHandle == nullptr || buf == nullptr || len == 0 || realLen == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
    auto* handle = static_cast<UartHandleStruct*>(uartHandle);
    ssize_t ret = read(handle->uart_fd, buf, len);
    if (ret < 0) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }
    *realLen = static_cast<uint32_t>(ret);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalUart_GetStatus(E_DjiHalUartNum uartNum, T_DjiUartStatus* status) {
    (void)uartNum;
    if (status == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> g(g_mu);
    status->isConnect = !g_open_fds.empty();
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalUart_GetDeviceInfo(T_DjiHalUartDeviceInfo* deviceInfo) {
    if (deviceInfo == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> g(g_mu);
    deviceInfo->vid = g_usb_vid;
    deviceInfo->pid = g_usb_pid;
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

}  // extern "C"

}  // namespace

void ConfigureUartHal(const std::string& device_path, std::uint16_t usb_vid,
                      std::uint16_t usb_pid) {
    std::lock_guard<std::mutex> g(g_mu);
    g_device_path = device_path;
    g_usb_vid = usb_vid;
    g_usb_pid = usb_pid;
}

// Fills the handler struct; called by platform_linux.cpp only.
void FillUartHandler(T_DjiHalUartHandler* handler) {
    handler->UartInit = HalUart_Init;
    handler->UartDeInit = HalUart_DeInit;
    handler->UartWriteData = HalUart_WriteData;
    handler->UartReadData = HalUart_ReadData;
    handler->UartGetStatus = HalUart_GetStatus;
    handler->UartGetDeviceInfo = HalUart_GetDeviceInfo;
}

}  // namespace droppoint::psdk
