// tests/test_sd_notify.cpp
//
// sd_notify: a no-op when NOTIFY_SOCKET is unset (always the case on CI /
// Windows), and on Linux a round-trip through a real AF_UNIX datagram
// socket bound to a filesystem path.
#include <cstdlib>
#include <string>

#include "test_framework.h"

#include "hw/sd_notify.h"

TEST_CASE("SdNotify: false when NOTIFY_SOCKET is unset") {
    REQUIRE(!droppoint::hw::SdNotifyReady());
    REQUIRE(!droppoint::hw::SdNotify("STATUS=hello"));
}

#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>

TEST_CASE("SdNotify: delivers a datagram to a bound unix socket") {
    const auto path = (std::filesystem::temp_directory_path() / "dp_notify.sock").string();
    ::unlink(path.c_str());

    const int fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    ::setenv("NOTIFY_SOCKET", path.c_str(), 1);
    REQUIRE(droppoint::hw::SdNotify("WATCHDOG=1"));
    ::unsetenv("NOTIFY_SOCKET");

    char buf[64] = {0};
    timeval tv{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    const auto n = ::recv(fd, buf, sizeof(buf) - 1, 0);
    REQUIRE(n > 0);
    REQUIRE(std::string(buf) == "WATCHDOG=1");
    ::close(fd);
    ::unlink(path.c_str());
}
#endif
