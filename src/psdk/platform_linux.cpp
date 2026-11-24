// psdk/platform_linux.cpp — see header. Status: unverified — requires hardware.
#include "platform_linux.h"

#include <dji_logger.h>
#include <dji_platform.h>

#include <cstdio>
#include <stdexcept>
#include <string>

// DJI sample OSAL + USB-bulk HAL, compiled straight from the PSDK checkout
// (see src/psdk/CMakeLists.txt). These headers are C; the extern "C" blocks
// live inside them.
extern "C" {
#include "osal.h"
#include "osal_fs.h"
#include "osal_socket.h"
#include "hal_usb_bulk.h"
}

#include "hal_uart.h"

namespace droppoint::psdk {

// Defined in hal_uart.cpp (the anonymous namespace there keeps the C
// callbacks private).
void FillUartHandler(T_DjiHalUartHandler* handler);

namespace {

void Check(T_DjiReturnCode code, const char* what) {
    if (code != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error(std::string("PSDK platform init: ") + what +
                                 " failed (0x" + std::to_string(static_cast<unsigned long long>(code)) +
                                 ")");
    }
}

T_DjiReturnCode PrintConsole(const uint8_t* data, uint16_t dataLen) {
    std::fwrite(data, 1, dataLen, stdout);
    std::fflush(stdout);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

}  // namespace

void RegisterPlatformHandlers(const util::config::AircraftConfig& aircraft) {
    // OSAL — everything from the DJI sample's common osal files.
    T_DjiOsalHandler osal{};
    osal.TaskCreate = Osal_TaskCreate;
    osal.TaskDestroy = Osal_TaskDestroy;
    osal.TaskSleepMs = Osal_TaskSleepMs;
    osal.MutexCreate = Osal_MutexCreate;
    osal.MutexDestroy = Osal_MutexDestroy;
    osal.MutexLock = Osal_MutexLock;
    osal.MutexUnlock = Osal_MutexUnlock;
    osal.SemaphoreCreate = Osal_SemaphoreCreate;
    osal.SemaphoreDestroy = Osal_SemaphoreDestroy;
    osal.SemaphoreWait = Osal_SemaphoreWait;
    osal.SemaphoreTimedWait = Osal_SemaphoreTimedWait;
    osal.SemaphorePost = Osal_SemaphorePost;
    osal.Malloc = Osal_Malloc;
    osal.Free = Osal_Free;
    osal.GetTimeMs = Osal_GetTimeMs;
    osal.GetTimeUs = Osal_GetTimeUs;
    osal.GetRandomNum = Osal_GetRandomNum;
    Check(DjiPlatform_RegOsalHandler(&osal), "DjiPlatform_RegOsalHandler");

    T_DjiFileSystemHandler fs{};
    fs.FileOpen = Osal_FileOpen;
    fs.FileClose = Osal_FileClose;
    fs.FileWrite = Osal_FileWrite;
    fs.FileRead = Osal_FileRead;
    fs.FileSync = Osal_FileSync;
    fs.FileSeek = Osal_FileSeek;
    fs.DirOpen = Osal_DirOpen;
    fs.DirClose = Osal_DirClose;
    fs.DirRead = Osal_DirRead;
    fs.Mkdir = Osal_Mkdir;
    fs.Unlink = Osal_Unlink;
    fs.Rename = Osal_Rename;
    fs.Stat = Osal_Stat;
    Check(DjiPlatform_RegFileSystemHandler(&fs), "DjiPlatform_RegFileSystemHandler");

    T_DjiSocketHandler sock{};
    sock.Socket = Osal_Socket;
    sock.Bind = Osal_Bind;
    sock.Close = Osal_Close;
    sock.UdpSendData = Osal_UdpSendData;
    sock.UdpRecvData = Osal_UdpRecvData;
    sock.TcpListen = Osal_TcpListen;
    sock.TcpAccept = Osal_TcpAccept;
    sock.TcpConnect = Osal_TcpConnect;
    sock.TcpSendData = Osal_TcpSendData;
    sock.TcpRecvData = Osal_TcpRecvData;
    Check(DjiPlatform_RegSocketHandler(&sock), "DjiPlatform_RegSocketHandler");

    // Transport: exactly one link per build config.
    switch (aircraft.psdk_link_mode) {
        case util::config::PsdkLinkMode::kOnlyUart: {
            ConfigureUartHal(aircraft.uart_device,
                             static_cast<std::uint16_t>(aircraft.uart_usb_vid),
                             static_cast<std::uint16_t>(aircraft.uart_usb_pid));
            T_DjiHalUartHandler uart{};
            FillUartHandler(&uart);
            Check(DjiPlatform_RegHalUartHandler(&uart), "DjiPlatform_RegHalUartHandler");
            break;
        }
        case util::config::PsdkLinkMode::kOnlyUsbBulk: {
            // DJI's own Manifold 3 functionfs HAL, compiled from the PSDK
            // checkout (CONFIG_HARDWARE_CONNECTION = ONLY_USB_BULK in the
            // manifold3 sample registers only this transport).
            T_DjiHalUsbBulkHandler bulk{};
            bulk.UsbBulkInit = HalUsbBulk_Init;
            bulk.UsbBulkDeInit = HalUsbBulk_DeInit;
            bulk.UsbBulkWriteData = HalUsbBulk_WriteData;
            bulk.UsbBulkReadData = HalUsbBulk_ReadData;
            bulk.UsbBulkGetDeviceInfo = HalUsbBulk_GetDeviceInfo;
            Check(DjiPlatform_RegHalUsbBulkHandler(&bulk), "DjiPlatform_RegHalUsbBulkHandler");
            break;
        }
        default:
            throw std::runtime_error(
                "PSDK platform init: aircraft.yaml psdk_link_mode is not supported by this "
                "build — only 'only_uart' and 'only_usb_bulk' are implemented");
    }

    T_DjiLoggerConsole printConsole{};
    printConsole.func = PrintConsole;
    printConsole.consoleLevel = DJI_LOGGER_CONSOLE_LOG_LEVEL_INFO;
    printConsole.isSupportColor = false;
    Check(DjiLogger_AddConsole(&printConsole), "DjiLogger_AddConsole");
}

}  // namespace droppoint::psdk
