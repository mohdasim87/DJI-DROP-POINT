// hw/sd_notify.cpp
#include "sd_notify.h"

#include <cstdlib>
#include <cstring>

#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace droppoint::hw {

bool SdNotify(const std::string& state) {
#ifdef __linux__
    const char* socket_path = std::getenv("NOTIFY_SOCKET");
    if (socket_path == nullptr || socket_path[0] == '\0') return false;

    const int fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path[0] == '@') {
        // Abstract namespace socket: leading '@' maps to a NUL byte.
        addr.sun_path[0] = '\0';
        std::strncpy(addr.sun_path + 1, socket_path + 1, sizeof(addr.sun_path) - 2);
    } else {
        std::strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    }

    const auto sent = ::sendto(fd, state.data(), state.size(), 0,
                               reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    ::close(fd);
    return sent == static_cast<ssize_t>(state.size());
#else
    (void)state;
    return false;
#endif
}

}  // namespace droppoint::hw
