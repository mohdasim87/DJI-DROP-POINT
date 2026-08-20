// psdk/platform_linux.cpp
//
// Linux platform HAL registration for the DJI PSDK.
// Status: unverified — compiles on Linux, is a no-op stub on Windows.
//
// This file implements the OSAL, filesystem, socket, and UART handlers that the
// PSDK requires before DjiCore_Init can succeed. USB-bulk and network transport
// are stubbed because DropPoint's default aircraft.yaml uses UART or
// UART+USB-bulk; if you switch to USB-bulk-only or network-only you must
// implement those handlers here (see DJI samples for the exact shape).
#include "platform_linux.h"

#include "psdk_util.h"
#include "util/logging.h"

#include "dji_core.h"
#include "dji_logger.h"
#include "dji_platform.h"

#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <string>

namespace droppoint::psdk {

namespace {

constexpr uint32_t kUartBufferSize = 4096;

struct UartCtx {
    int fd = -1;
    E_DjiHalUartNum num = DJI_HAL_UART_NUM_0;
};

T_DjiReturnCode DjiReturnFromErrno(int err) {
    return err == 0 ? DJI_RETURN_CODE_OK : DJI_RETURN_CODE_ERR_SYSTEM;
}

// ---------------------------------------------------------------------------
// Console logger
// ---------------------------------------------------------------------------
extern "C" T_DjiReturnCode ConsolePrint(const uint8_t* data, uint16_t dataLen) {
    if (!data) return DJI_RETURN_CODE_ERR_PARAM;
    write(STDOUT_FILENO, data, dataLen);
    return DJI_RETURN_CODE_OK;
}

// ---------------------------------------------------------------------------
// OSAL
// ---------------------------------------------------------------------------
extern "C" T_DjiReturnCode Osal_TaskCreate(const char* name, void* (*taskFunc)(void*),
                                            uint32_t stackSize, void* arg,
                                            T_DjiTaskHandle* task) {
    (void)name;
    (void)stackSize;
    auto* handle = new pthread_t;
    if (pthread_create(handle, nullptr, taskFunc, arg) != 0) {
        delete handle;
        return DJI_RETURN_CODE_ERR_SYSTEM;
    }
    *task = handle;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Osal_TaskDestroy(T_DjiTaskHandle task) {
    if (!task) return DJI_RETURN_CODE_ERR_PARAM;
    auto* handle = static_cast<pthread_t*>(task);
    pthread_cancel(*handle);
    pthread_join(*handle, nullptr);
    delete handle;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Osal_TaskSleepMs(uint32_t timeMs) {
    usleep(timeMs * 1000);
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Osal_MutexCreate(T_DjiMutexHandle* mutex) {
    auto* m = new pthread_mutex_t;
    if (pthread_mutex_init(m, nullptr) != 0) {
        delete m;
        return DJI_RETURN_CODE_ERR_SYSTEM;
    }
    *mutex = m;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Osal_MutexDestroy(T_DjiMutexHandle mutex) {
    if (!mutex) return DJI_RETURN_CODE_ERR_PARAM;
    auto* m = static_cast<pthread_mutex_t*>(mutex);
    pthread_mutex_destroy(m);
    delete m;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Osal_MutexLock(T_DjiMutexHandle mutex) {
    if (!mutex) return DJI_RETURN_CODE_ERR_PARAM;
    return DjiReturnFromErrno(pthread_mutex_lock(static_cast<pthread_mutex_t*>(mutex)));
}

extern "C" T_DjiReturnCode Osal_MutexUnlock(T_DjiMutexHandle mutex) {
    if (!mutex) return DJI_RETURN_CODE_ERR_PARAM;
    return DjiReturnFromErrno(pthread_mutex_unlock(static_cast<pthread_mutex_t*>(mutex)));
}

extern "C" T_DjiReturnCode Osal_SemaphoreCreate(uint32_t initValue, T_DjiSemaHandle* semaphore) {
    auto* s = new sem_t;
    if (sem_init(s, 0, initValue) != 0) {
        delete s;
        return DJI_RETURN_CODE_ERR_SYSTEM;
    }
    *semaphore = s;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Osal_SemaphoreDestroy(T_DjiSemaHandle semaphore) {
    if (!semaphore) return DJI_RETURN_CODE_ERR_PARAM;
    auto* s = static_cast<sem_t*>(semaphore);
    sem_destroy(s);
    delete s;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Osal_SemaphoreWait(T_DjiSemaHandle semaphore) {
    if (!semaphore) return DJI_RETURN_CODE_ERR_PARAM;
    return DjiReturnFromErrno(sem_wait(static_cast<sem_t*>(semaphore)));
}

extern "C" T_DjiReturnCode Osal_SemaphoreTimedWait(T_DjiSemaHandle semaphore,
                                                   uint32_t waitTimeMs) {
    if (!semaphore) return DJI_RETURN_CODE_ERR_PARAM;
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += waitTimeMs / 1000;
    ts.tv_nsec += (waitTimeMs % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }
    return DjiReturnFromErrno(sem_timedwait(static_cast<sem_t*>(semaphore), &ts));
}

extern "C" T_DjiReturnCode Osal_SemaphorePost(T_DjiSemaHandle semaphore) {
    if (!semaphore) return DJI_RETURN_CODE_ERR_PARAM;
    return DjiReturnFromErrno(sem_post(static_cast<sem_t*>(semaphore)));
}

extern "C" T_DjiReturnCode Osal_GetTimeMs(uint32_t* ms) {
    if (!ms) return DJI_RETURN_CODE_ERR_PARAM;
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *ms = static_cast<uint32_t>(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Osal_GetTimeUs(uint64_t* us) {
    if (!us) return DJI_RETURN_CODE_ERR_PARAM;
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *us = static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + static_cast<uint64_t>(ts.tv_nsec / 1000);
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Osal_GetRandomNum(uint16_t* randomNum) {
    if (!randomNum) return DJI_RETURN_CODE_ERR_PARAM;
    *randomNum = static_cast<uint16_t>(random() & 0xFFFF);
    return DJI_RETURN_CODE_OK;
}

extern "C" void* Osal_Malloc(uint32_t size) { return malloc(size); }
extern "C" void Osal_Free(void* ptr) { free(ptr); }

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------
extern "C" T_DjiReturnCode Fs_FileOpen(const char* fileName, const char* fileMode,
                                        T_DjiFileHandle* fileObj) {
    if (!fileName || !fileMode || !fileObj) return DJI_RETURN_CODE_ERR_PARAM;
    FILE* f = fopen(fileName, fileMode);
    if (!f) return DJI_RETURN_CODE_ERR_SYSTEM;
    *fileObj = f;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Fs_FileClose(T_DjiFileHandle fileObj) {
    return DjiReturnFromErrno(fclose(static_cast<FILE*>(fileObj)));
}

extern "C" T_DjiReturnCode Fs_FileWrite(T_DjiFileHandle fileObj, const uint8_t* buf,
                                          uint32_t len, uint32_t* realLen) {
    if (!fileObj || !buf || !realLen) return DJI_RETURN_CODE_ERR_PARAM;
    *realLen = static_cast<uint32_t>(fwrite(buf, 1, len, static_cast<FILE*>(fileObj)));
    return *realLen == len ? DJI_RETURN_CODE_OK : DJI_RETURN_CODE_ERR_SYSTEM;
}

extern "C" T_DjiReturnCode Fs_FileRead(T_DjiFileHandle fileObj, uint8_t* buf, uint32_t len,
                                       uint32_t* realLen) {
    if (!fileObj || !buf || !realLen) return DJI_RETURN_CODE_ERR_PARAM;
    *realLen = static_cast<uint32_t>(fread(buf, 1, len, static_cast<FILE*>(fileObj)));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Fs_FileSeek(T_DjiFileHandle fileObj, uint32_t offset) {
    return DjiReturnFromErrno(fseek(static_cast<FILE*>(fileObj), offset, SEEK_SET));
}

extern "C" T_DjiReturnCode Fs_FileSync(T_DjiFileHandle fileObj) {
    return DjiReturnFromErrno(fflush(static_cast<FILE*>(fileObj)));
}

extern "C" T_DjiReturnCode Fs_DirOpen(const char* filePath, T_DjiDirHandle* dirObj) {
    if (!filePath || !dirObj) return DJI_RETURN_CODE_ERR_PARAM;
    DIR* d = opendir(filePath);
    if (!d) return DJI_RETURN_CODE_ERR_SYSTEM;
    *dirObj = d;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Fs_DirClose(T_DjiDirHandle dirObj) {
    return DjiReturnFromErrno(closedir(static_cast<DIR*>(dirObj)));
}

extern "C" T_DjiReturnCode Fs_DirRead(T_DjiDirHandle dirObj, T_DjiFileInfo* fileInfo) {
    if (!dirObj || !fileInfo) return DJI_RETURN_CODE_ERR_PARAM;
    dirent* entry = readdir(static_cast<DIR*>(dirObj));
    if (!entry) return DJI_RETURN_CODE_ERR_SYSTEM;
    memset(fileInfo, 0, sizeof(T_DjiFileInfo));
    strncpy(fileInfo->path, entry->d_name, DJI_FILE_PATH_SIZE_MAX - 1);
    fileInfo->isDir = (entry->d_type == DT_DIR);
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Fs_Mkdir(const char* filePath) {
    return DjiReturnFromErrno(mkdir(filePath, 0755));
}

extern "C" T_DjiReturnCode Fs_Unlink(const char* filePath) {
    return DjiReturnFromErrno(unlink(filePath));
}

extern "C" T_DjiReturnCode Fs_Rename(const char* oldFilePath, const char* newFilePath) {
    return DjiReturnFromErrno(rename(oldFilePath, newFilePath));
}

extern "C" T_DjiReturnCode Fs_Stat(const char* filePath, T_DjiFileInfo* fileInfo) {
    if (!filePath || !fileInfo) return DJI_RETURN_CODE_ERR_PARAM;
    struct stat st {};
    if (stat(filePath, &st) != 0) return DJI_RETURN_CODE_ERR_SYSTEM;
    memset(fileInfo, 0, sizeof(T_DjiFileInfo));
    strncpy(fileInfo->path, filePath, DJI_FILE_PATH_SIZE_MAX - 1);
    fileInfo->size = static_cast<uint32_t>(st.st_size);
    fileInfo->isDir = S_ISDIR(st.st_mode);
    return DJI_RETURN_CODE_OK;
}

// ---------------------------------------------------------------------------
// Socket
// ---------------------------------------------------------------------------
extern "C" T_DjiReturnCode Sock_Socket(E_DjiSocketMode mode, T_DjiSocketHandle* socketHandle) {
    if (!socketHandle) return DJI_RETURN_CODE_ERR_PARAM;
    int domain = (mode == DJI_SOCKET_MODE_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    int fd = ::socket(AF_INET, domain, 0);
    if (fd < 0) return DJI_RETURN_CODE_ERR_SYSTEM;
    *socketHandle = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Sock_Close(T_DjiSocketHandle socketHandle) {
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(socketHandle));
    return DjiReturnFromErrno(::close(fd));
}

extern "C" T_DjiReturnCode Sock_Bind(T_DjiSocketHandle socketHandle, const char* ipAddr,
                                       uint32_t port) {
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(socketHandle));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = ipAddr ? inet_addr(ipAddr) : INADDR_ANY;
    return DjiReturnFromErrno(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
}

extern "C" T_DjiReturnCode Sock_UdpSendData(T_DjiSocketHandle socketHandle, const char* ipAddr,
                                            uint32_t port, const uint8_t* buf, uint32_t len,
                                            uint32_t* realLen) {
    if (!ipAddr || !buf || !realLen) return DJI_RETURN_CODE_ERR_PARAM;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(socketHandle));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr(ipAddr);
    ssize_t sent = ::sendto(fd, buf, len, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (sent < 0) return DJI_RETURN_CODE_ERR_SYSTEM;
    *realLen = static_cast<uint32_t>(sent);
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Sock_UdpRecvData(T_DjiSocketHandle socketHandle, char* ipAddr,
                                            uint32_t* port, uint8_t* buf, uint32_t len,
                                            uint32_t* realLen) {
    if (!buf || !realLen) return DJI_RETURN_CODE_ERR_PARAM;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(socketHandle));
    sockaddr_in addr{};
    socklen_t addrLen = sizeof(addr);
    ssize_t got = ::recvfrom(fd, buf, len, 0, reinterpret_cast<sockaddr*>(&addr), &addrLen);
    if (got < 0) return DJI_RETURN_CODE_ERR_SYSTEM;
    if (ipAddr) inet_ntop(AF_INET, &addr.sin_addr, ipAddr, DJI_IP_ADDR_STR_SIZE_MAX);
    if (port) *port = ntohs(addr.sin_port);
    *realLen = static_cast<uint32_t>(got);
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Sock_TcpListen(T_DjiSocketHandle socketHandle) {
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(socketHandle));
    return DjiReturnFromErrno(::listen(fd, 5));
}

extern "C" T_DjiReturnCode Sock_TcpAccept(T_DjiSocketHandle socketHandle, char* ipAddr,
                                          uint32_t* port, T_DjiSocketHandle* outSocketHandle) {
    if (!outSocketHandle) return DJI_RETURN_CODE_ERR_PARAM;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(socketHandle));
    sockaddr_in addr{};
    socklen_t addrLen = sizeof(addr);
    int client = ::accept(fd, reinterpret_cast<sockaddr*>(&addr), &addrLen);
    if (client < 0) return DJI_RETURN_CODE_ERR_SYSTEM;
    if (ipAddr) inet_ntop(AF_INET, &addr.sin_addr, ipAddr, DJI_IP_ADDR_STR_SIZE_MAX);
    if (port) *port = ntohs(addr.sin_port);
    *outSocketHandle = reinterpret_cast<void*>(static_cast<intptr_t>(client));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Sock_TcpConnect(T_DjiSocketHandle socketHandle, const char* ipAddr,
                                           uint32_t port) {
    if (!ipAddr) return DJI_RETURN_CODE_ERR_PARAM;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(socketHandle));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr(ipAddr);
    return DjiReturnFromErrno(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
}

extern "C" T_DjiReturnCode Sock_TcpSendData(T_DjiSocketHandle socketHandle, const uint8_t* buf,
                                            uint32_t len, uint32_t* realLen) {
    if (!buf || !realLen) return DJI_RETURN_CODE_ERR_PARAM;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(socketHandle));
    ssize_t sent = ::send(fd, buf, len, 0);
    if (sent < 0) return DJI_RETURN_CODE_ERR_SYSTEM;
    *realLen = static_cast<uint32_t>(sent);
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Sock_TcpRecvData(T_DjiSocketHandle socketHandle, uint8_t* buf,
                                            uint32_t len, uint32_t* realLen) {
    if (!buf || !realLen) return DJI_RETURN_CODE_ERR_PARAM;
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(socketHandle));
    ssize_t got = ::recv(fd, buf, len, 0);
    if (got < 0) return DJI_RETURN_CODE_ERR_SYSTEM;
    *realLen = static_cast<uint32_t>(got);
    return DJI_RETURN_CODE_OK;
}

// ---------------------------------------------------------------------------
// UART
// ---------------------------------------------------------------------------
// Default device paths. On a generic Linux companion computer the DJI UART
// usually appears as /dev/ttyUSB0 or /dev/ttyACM0; adjust via udev rules.
const char* UartDevicePath(E_DjiHalUartNum num) {
    return (num == DJI_HAL_UART_NUM_0) ? "/dev/ttyUSB0" : "/dev/ttyACM0";
}

extern "C" T_DjiReturnCode Uart_Init(E_DjiHalUartNum uartNum, uint32_t baudRate,
                                      T_DjiUartHandle* uartHandle) {
    if (!uartHandle) return DJI_RETURN_CODE_ERR_PARAM;
    const char* path = UartDevicePath(uartNum);
    int fd = open(path, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        DP_LOG_ERROR("platform_linux", std::string("failed to open ") + path + ": " +
                                       strerror(errno));
        return DJI_RETURN_CODE_ERR_SYSTEM;
    }

    termios tty {};
    if (tcgetattr(fd, &tty) != 0) {
        close(fd);
        return DJI_RETURN_CODE_ERR_SYSTEM;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, static_cast<speed_t>(baudRate));
    cfsetospeed(&tty, static_cast<speed_t>(baudRate));
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        close(fd);
        return DJI_RETURN_CODE_ERR_SYSTEM;
    }

    auto* ctx = new UartCtx{fd, uartNum};
    *uartHandle = ctx;
    DP_LOG_INFO("platform_linux", std::string("opened UART ") + path);
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Uart_DeInit(T_DjiUartHandle uartHandle) {
    if (!uartHandle) return DJI_RETURN_CODE_ERR_PARAM;
    auto* ctx = static_cast<UartCtx*>(uartHandle);
    close(ctx->fd);
    delete ctx;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Uart_WriteData(T_DjiUartHandle uartHandle, const uint8_t* buf,
                                          uint32_t len, uint32_t* realLen) {
    if (!uartHandle || !buf || !realLen) return DJI_RETURN_CODE_ERR_PARAM;
    auto* ctx = static_cast<UartCtx*>(uartHandle);
    ssize_t wrote = write(ctx->fd, buf, len);
    if (wrote < 0) return DJI_RETURN_CODE_ERR_SYSTEM;
    *realLen = static_cast<uint32_t>(wrote);
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Uart_ReadData(T_DjiUartHandle uartHandle, uint8_t* buf, uint32_t len,
                                         uint32_t* realLen) {
    if (!uartHandle || !buf || !realLen) return DJI_RETURN_CODE_ERR_PARAM;
    auto* ctx = static_cast<UartCtx*>(uartHandle);
    ssize_t got = read(ctx->fd, buf, len);
    if (got < 0) return DJI_RETURN_CODE_ERR_SYSTEM;
    *realLen = static_cast<uint32_t>(got);
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Uart_GetStatus(E_DjiHalUartNum uartNum, T_DjiUartStatus* status) {
    (void)uartNum;
    if (!status) return DJI_RETURN_CODE_ERR_PARAM;
    status->isConnect = true;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode Uart_GetDeviceInfo(T_DjiHalUartDeviceInfo* deviceInfo) {
    if (!deviceInfo) return DJI_RETURN_CODE_ERR_PARAM;
    deviceInfo->pid = 0;
    deviceInfo->vid = 0;
    return DJI_RETURN_CODE_OK;
}

// ---------------------------------------------------------------------------
// USB bulk stub (only used if aircraft.yaml selects a USB-bulk link mode)
// ---------------------------------------------------------------------------
extern "C" T_DjiReturnCode UsbBulk_Init(T_DjiHalUsbBulkInfo, T_DjiUsbBulkHandle* handle) {
    if (!handle) return DJI_RETURN_CODE_ERR_PARAM;
    *handle = nullptr;
    return DJI_RETURN_CODE_ERR_SYSTEM;  // not implemented; UART-only default
}
extern "C" T_DjiReturnCode UsbBulk_DeInit(T_DjiUsbBulkHandle) {
    return DJI_RETURN_CODE_OK;
}
extern "C" T_DjiReturnCode UsbBulk_WriteData(T_DjiUsbBulkHandle, const uint8_t*, uint32_t,
                                             uint32_t*) {
    return DJI_RETURN_CODE_ERR_SYSTEM;
}
extern "C" T_DjiReturnCode UsbBulk_ReadData(T_DjiUsbBulkHandle, uint8_t*, uint32_t, uint32_t*) {
    return DJI_RETURN_CODE_ERR_SYSTEM;
}
extern "C" T_DjiReturnCode UsbBulk_GetDeviceInfo(T_DjiHalUsbBulkDeviceInfo*) {
    return DJI_RETURN_CODE_ERR_SYSTEM;
}

// ---------------------------------------------------------------------------
// Network stub (only used if aircraft.yaml selects a network link mode)
// ---------------------------------------------------------------------------
extern "C" T_DjiReturnCode Network_Init(const char*, const char*, T_DjiNetworkHandle* handle) {
    if (!handle) return DJI_RETURN_CODE_ERR_PARAM;
    *handle = nullptr;
    return DJI_RETURN_CODE_ERR_SYSTEM;
}
extern "C" T_DjiReturnCode Network_DeInit(T_DjiNetworkHandle) { return DJI_RETURN_CODE_OK; }
extern "C" T_DjiReturnCode Network_GetDeviceInfo(T_DjiHalNetworkDeviceInfo*) {
    return DJI_RETURN_CODE_ERR_SYSTEM;
}

// ---------------------------------------------------------------------------
// I2C stub (registered because the sample does, but DropPoint does not use it)
// ---------------------------------------------------------------------------
extern "C" T_DjiReturnCode I2c_Init(T_DjiHalI2cConfig, T_DjiI2cHandle* handle) {
    if (!handle) return DJI_RETURN_CODE_ERR_PARAM;
    *handle = nullptr;
    return DJI_RETURN_CODE_ERR_SYSTEM;
}
extern "C" T_DjiReturnCode I2c_DeInit(T_DjiI2cHandle) { return DJI_RETURN_CODE_OK; }
extern "C" T_DjiReturnCode I2c_WriteData(T_DjiI2cHandle, uint16_t, const uint8_t*, uint32_t,
                                         uint32_t*) {
    return DJI_RETURN_CODE_ERR_SYSTEM;
}
extern "C" T_DjiReturnCode I2c_ReadData(T_DjiI2cHandle, uint16_t, uint8_t*, uint32_t, uint32_t*) {
    return DJI_RETURN_CODE_ERR_SYSTEM;
}

void RegisterTransportHandlers(const util::config::AircraftConfig& cfg) {
    T_DjiHalUartHandler uartHandler{};
    uartHandler.UartInit = Uart_Init;
    uartHandler.UartDeInit = Uart_DeInit;
    uartHandler.UartWriteData = Uart_WriteData;
    uartHandler.UartReadData = Uart_ReadData;
    uartHandler.UartGetStatus = Uart_GetStatus;
    uartHandler.UartGetDeviceInfo = Uart_GetDeviceInfo;

    T_DjiHalUsbBulkHandler usbHandler{};
    usbHandler.UsbBulkInit = UsbBulk_Init;
    usbHandler.UsbBulkDeInit = UsbBulk_DeInit;
    usbHandler.UsbBulkWriteData = UsbBulk_WriteData;
    usbHandler.UsbBulkReadData = UsbBulk_ReadData;
    usbHandler.UsbBulkGetDeviceInfo = UsbBulk_GetDeviceInfo;

    T_DjiHalNetworkHandler networkHandler{};
    networkHandler.NetworkInit = Network_Init;
    networkHandler.NetworkDeInit = Network_DeInit;
    networkHandler.NetworkGetDeviceInfo = Network_GetDeviceInfo;

    using Mode = util::config::PsdkLinkMode;
    switch (cfg.psdk_link_mode) {
        case Mode::kOnlyUart:
            CheckReturn(DjiPlatform_RegHalUartHandler(&uartHandler), "register UART handler");
            break;
        case Mode::kUartAndUsbBulk:
            CheckReturn(DjiPlatform_RegHalUartHandler(&uartHandler), "register UART handler");
            CheckReturn(DjiPlatform_RegHalUsbBulkHandler(&usbHandler), "register USB bulk handler");
            break;
        case Mode::kUartAndNetwork:
            CheckReturn(DjiPlatform_RegHalUartHandler(&uartHandler), "register UART handler");
            CheckReturn(DjiPlatform_RegHalNetworkHandler(&networkHandler), "register network handler");
            break;
        case Mode::kOnlyUsbBulk:
            CheckReturn(DjiPlatform_RegHalUsbBulkHandler(&usbHandler), "register USB bulk handler");
            break;
        case Mode::kOnlyNetwork:
            CheckReturn(DjiPlatform_RegHalNetworkHandler(&networkHandler), "register network handler");
            break;
    }
}

}  // namespace

void PlatformInit(const util::config::AircraftConfig& cfg) {
    T_DjiOsalHandler osalHandler{};
    osalHandler.TaskCreate = Osal_TaskCreate;
    osalHandler.TaskDestroy = Osal_TaskDestroy;
    osalHandler.TaskSleepMs = Osal_TaskSleepMs;
    osalHandler.MutexCreate = Osal_MutexCreate;
    osalHandler.MutexDestroy = Osal_MutexDestroy;
    osalHandler.MutexLock = Osal_MutexLock;
    osalHandler.MutexUnlock = Osal_MutexUnlock;
    osalHandler.SemaphoreCreate = Osal_SemaphoreCreate;
    osalHandler.SemaphoreDestroy = Osal_SemaphoreDestroy;
    osalHandler.SemaphoreWait = Osal_SemaphoreWait;
    osalHandler.SemaphoreTimedWait = Osal_SemaphoreTimedWait;
    osalHandler.SemaphorePost = Osal_SemaphorePost;
    osalHandler.GetTimeMs = Osal_GetTimeMs;
    osalHandler.GetTimeUs = Osal_GetTimeUs;
    osalHandler.GetRandomNum = Osal_GetRandomNum;
    osalHandler.Malloc = Osal_Malloc;
    osalHandler.Free = Osal_Free;
    CheckReturn(DjiPlatform_RegOsalHandler(&osalHandler), "register OSAL handler");

    RegisterTransportHandlers(cfg);

    T_DjiSocketHandler socketHandler{};
    socketHandler.Socket = Sock_Socket;
    socketHandler.Close = Sock_Close;
    socketHandler.Bind = Sock_Bind;
    socketHandler.UdpSendData = Sock_UdpSendData;
    socketHandler.UdpRecvData = Sock_UdpRecvData;
    socketHandler.TcpListen = Sock_TcpListen;
    socketHandler.TcpAccept = Sock_TcpAccept;
    socketHandler.TcpConnect = Sock_TcpConnect;
    socketHandler.TcpSendData = Sock_TcpSendData;
    socketHandler.TcpRecvData = Sock_TcpRecvData;
    CheckReturn(DjiPlatform_RegSocketHandler(&socketHandler), "register socket handler");

    T_DjiFileSystemHandler fileSystemHandler{};
    fileSystemHandler.FileOpen = Fs_FileOpen;
    fileSystemHandler.FileClose = Fs_FileClose;
    fileSystemHandler.FileWrite = Fs_FileWrite;
    fileSystemHandler.FileRead = Fs_FileRead;
    fileSystemHandler.FileSeek = Fs_FileSeek;
    fileSystemHandler.FileSync = Fs_FileSync;
    fileSystemHandler.DirOpen = Fs_DirOpen;
    fileSystemHandler.DirClose = Fs_DirClose;
    fileSystemHandler.DirRead = Fs_DirRead;
    fileSystemHandler.Mkdir = Fs_Mkdir;
    fileSystemHandler.Unlink = Fs_Unlink;
    fileSystemHandler.Rename = Fs_Rename;
    fileSystemHandler.Stat = Fs_Stat;
    CheckReturn(DjiPlatform_RegFileSystemHandler(&fileSystemHandler), "register filesystem handler");

    T_DjiHalI2cHandler i2cHandler{};
    i2cHandler.I2cInit = I2c_Init;
    i2cHandler.I2cDeInit = I2c_DeInit;
    i2cHandler.I2cWriteData = I2c_WriteData;
    i2cHandler.I2cReadData = I2c_ReadData;
    CheckReturn(DjiPlatform_RegHalI2cHandler(&i2cHandler), "register I2C handler");

    T_DjiLoggerConsole console{};
    console.func = ConsolePrint;
    console.consoleLevel = DJI_LOGGER_CONSOLE_LOG_LEVEL_INFO;
    console.isSupportColor = false;
    CheckReturn(DjiLogger_AddConsole(&console), "add PSDK console logger");

    DP_LOG_INFO("platform_linux", "PSDK platform handlers registered");
}

void PlatformDeinit() {
    // Handlers are stateless; nothing to tear down.
}

}  // namespace droppoint::psdk

#else  // __linux__

namespace droppoint::psdk {

void PlatformInit(const util::config::AircraftConfig&) {
    DP_LOG_INFO("platform_linux", "PSDK platform init skipped on non-Linux build");
}

void PlatformDeinit() {}

}  // namespace droppoint::psdk

#endif
